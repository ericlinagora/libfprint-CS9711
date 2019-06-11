/*
 * Virtual match-in-sensor device with internal storage
 *
 * Copyright (C) 2019 Benjamin Berg <bberg@redhat.com>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
 */

/*
 * This is a virtual driver to debug features that are relevant for
 * match-in-sensor (MIS) devices that store data on the sensor itself.
 * In this case data needs to be deleted both locally and from the device
 * and we should support garbage collection.
 *
 * The protocol is line based, when a verify/enroll/etc. command is started
 * (or is active when connecting) then we send the command and the UUID
 * terminated by a newline.
 *
 *   IDLE\n
 *   VERIFY UUID\n
 *   ENROLL UUID\n
 *   DELETE UUID\n (planned)
 *   LIST (planned)
 *
 * The other end simply responds with an integer (terminated by newline)
 * that matches the internal fprint return codes.
 */

#define FP_COMPONENT "virtual_misdev"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <stdio.h>
#include "drivers_api.h"
#include "fpi-async.h"

#define VIRT_ENROLL_STAGES 1

enum virtdev_state {
	STATE_IDLE = 0,
	STATE_VERIFY,
	STATE_ENROLL,
	STATE_DELETE,
};

struct virt_dev {
	enum virtdev_state state;

	gchar *curr_uuid;

	fpi_io_condition *socket_io_cond;
	fpi_io_condition *client_io_cond;

	gint socket_fd;
	gint client_fd;

	gssize recv_len;
	guchar *recv_buf;
};

static void send_status(struct fp_dev *dev);

static void
handle_response (struct fp_dev *dev, guchar *buf, gssize len)
{
	struct virt_dev *virt = FP_INSTANCE_DATA(dev);
	gint result = atoi ((gchar*) buf);

	switch (virt->state) {
	case STATE_IDLE:
		fp_info ("Received unexpected status code %i\n", virt->state);
		break;
	case STATE_VERIFY:
		fp_info ("Reporting verify results back %i\n", result);
		fpi_drvcb_report_verify_result (dev, result, NULL);
		break;
	case STATE_ENROLL: {
		struct fp_print_data * fdata = NULL;

		fp_info ("Reporting enroll results back %i\n", result);

		/* If the enroll is "done", then report back the UUID for the print. */
		if (result == FP_ENROLL_COMPLETE) {
			struct fp_print_data_item *item = NULL;

			fdata = fpi_print_data_new (dev);

			item = fpi_print_data_item_new(strlen(virt->curr_uuid));
			memcpy(item->data, virt->curr_uuid, strlen(virt->curr_uuid));
			fpi_print_data_add_item(fdata, item);
		}

		fpi_drvcb_enroll_stage_completed (dev, result, fdata, NULL);
		break;
	}
	case STATE_DELETE:
		fp_info ("Reporting delete results back %i\n", result);

		virt->state = STATE_IDLE;
		g_free (virt->curr_uuid);
		virt->curr_uuid = NULL;

		fpi_drvcb_delete_complete (dev, result);

		send_status(dev);
		break;
	default:
		g_assert_not_reached();
	}
}

static void
send_status(struct fp_dev *dev)
{
	struct virt_dev *virt = FP_INSTANCE_DATA(dev);
	gchar *msg = NULL;

	if (virt->client_fd < 0)
		return;

	switch (virt->state) {
	case STATE_IDLE:
		msg = g_strdup ("IDLE\n");
		break;
	case STATE_ENROLL:
		msg = g_strdup_printf ("ENROLL %s\n", virt->curr_uuid);
		break;
	case STATE_VERIFY:
		msg = g_strdup_printf ("VERIFY %s\n", virt->curr_uuid);
		break;
	case STATE_DELETE:
		msg = g_strdup_printf ("DELETE %s\n", virt->curr_uuid);
		break;
	}

	send(virt->client_fd, msg, strlen(msg), MSG_NOSIGNAL);

	g_free (msg);
}

static void
client_socket_cb (struct fp_dev *dev, int fd, short int events, void *data)
{
	struct virt_dev *virt = FP_INSTANCE_DATA(dev);
	guchar *pos;
	guchar buf[512];
	gssize len;

	len = read(fd, buf, sizeof(buf));
	fp_dbg("Received %zi bytes from client!", len);

	if (len > 0) {
		virt->recv_buf = g_realloc(virt->recv_buf, virt->recv_len + len);
		memcpy(virt->recv_buf + virt->recv_len, buf, len);
		virt->recv_len += len;

		while ((pos = memmem(virt->recv_buf, virt->recv_len, "\n", 1))) {
			/* Found a newline, parse the command */
			fp_dbg("got a command response! %p %p", virt->recv_buf, pos);
			*pos = '\0';
			handle_response(dev, virt->recv_buf, pos - virt->recv_buf);

			/* And remove the parsed part from the buffer */
			virt->recv_len = virt->recv_len - (pos - virt->recv_buf) - 1;
			memmove(pos, virt->recv_buf, virt->recv_len);
			virt->recv_buf = realloc(virt->recv_buf, virt->recv_len);
		}
	} else {
		fp_dbg("Client disconnected!");
		close (virt->client_fd);
		virt->client_fd = -1;

		fpi_io_condition_remove (virt->client_io_cond);
		virt->client_io_cond = NULL;

		g_free(virt->recv_buf);
		virt->recv_buf = NULL;
		virt->recv_len = 0;
	}
}

static void
new_connection_cb (struct fp_dev *dev, int fd, short int events, void *data)
{
	struct virt_dev *virt = FP_INSTANCE_DATA(dev);
	int new_client_fd;

	new_client_fd = accept4(fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);

	fp_dbg("Got a new connection!");

	/* Already have a connection, reject this one */
	if (virt->client_fd >= 0) {
		fp_warn("Rejecting new connection as we already have one!");
		close (new_client_fd);
		return;
	}

	virt->client_fd = new_client_fd;
	virt->client_io_cond = fpi_io_condition_add (virt->client_fd, POLL_IN, client_socket_cb, dev, NULL);

	send_status(dev);
}

static int
dev_init(struct fp_dev *dev, unsigned long driver_data)
{
	struct virt_dev *virt;
	const char *env;
	struct sockaddr_un addr = {
		.sun_family = AF_UNIX
	};
	G_DEBUG_HERE();

	fpi_dev_set_nr_enroll_stages(dev, VIRT_ENROLL_STAGES);

	virt = g_new0(struct virt_dev, 1);
	fp_dev_set_instance_data(dev, virt);

	virt->client_fd = -1;

	env = fpi_dev_get_virtual_env (dev);

	virt->socket_fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
	if (virt->socket_fd < 0) {
		fp_err("Could not create socket: %m");
		return virt->socket_fd;
	}
	strncpy (addr.sun_path, env, sizeof(addr.sun_path) - 1);

	unlink(env);
	if (bind(virt->socket_fd, &addr, sizeof(struct sockaddr_un)) < 0) {
		fp_err("Could not bind address '%s': %m", addr.sun_path);
		close (virt->socket_fd);
		virt->socket_fd = -1;
		return -1;
	}

	if (listen (virt->socket_fd, 1) < 0) {
		fp_err("Could not open socket for listening: %m");
		close (virt->socket_fd);
		virt->socket_fd = -1;
		return -1;
	}

	virt->socket_io_cond = fpi_io_condition_add (virt->socket_fd, POLL_IN, new_connection_cb, dev, NULL);

	fpi_drvcb_open_complete(dev, 0);
	return 0;
}

static void dev_deinit(struct fp_dev *dev)
{
	struct virt_dev *virt = FP_INSTANCE_DATA(dev);

	G_DEBUG_HERE();

	if (virt->client_fd >= 0) {
		fpi_io_condition_remove (virt->client_io_cond);
		close (virt->client_fd);
	}

	if (virt->socket_fd >= 0) {
		fpi_io_condition_remove (virt->socket_io_cond);
		close (virt->socket_fd);
	}

	g_free (virt->curr_uuid);
	virt->curr_uuid = NULL;

	g_free(virt);
	fpi_drvcb_close_complete(dev);
}

static int enroll_start(struct fp_dev *dev)
{
	struct virt_dev *virt = FP_INSTANCE_DATA(dev);
	G_DEBUG_HERE();

	if (virt->state != STATE_IDLE)
		return -1;

	g_assert (virt->curr_uuid == NULL);

	virt->state = STATE_ENROLL;

	virt->curr_uuid = g_uuid_string_random ();
	send_status(dev);

	fpi_drvcb_enroll_started(dev, 0);
	return 0;
}

static int enroll_stop(struct fp_dev *dev)
{
	struct virt_dev *virt = FP_INSTANCE_DATA(dev);
	G_DEBUG_HERE();

	if (virt->state != STATE_ENROLL)
		return -1;

	virt->state = STATE_IDLE;
	g_free (virt->curr_uuid);
	virt->curr_uuid = NULL;
	send_status(dev);

	fpi_drvcb_enroll_stopped(dev);
	return 0;
}

static int verify_start(struct fp_dev *dev)
{
	struct virt_dev *virt = FP_INSTANCE_DATA(dev);
	struct fp_print_data *print;
	struct fp_print_data_item *item;

	G_DEBUG_HERE();

	if (virt->state != STATE_IDLE)
		return -1;

	g_assert (virt->curr_uuid == NULL);

	virt->state = STATE_VERIFY;

	print = fpi_dev_get_verify_data(dev);
	item = fpi_print_data_get_item(print);

	/* We expecte a UUID, that means 36 bytes. */
	g_assert(item->length == 36);

	virt->curr_uuid = g_malloc(37);
	virt->curr_uuid[36] = '\0';
	memcpy(virt->curr_uuid, item->data, 36);

	g_assert(g_uuid_string_is_valid (virt->curr_uuid));

	send_status(dev);

	fpi_drvcb_verify_started(dev, 0);
	return 0;
}

static int verify_stop(struct fp_dev *dev, gboolean iterating)
{
	struct virt_dev *virt = FP_INSTANCE_DATA(dev);
	G_DEBUG_HERE();

	if (virt->state != STATE_VERIFY)
		return -1;

	virt->state = STATE_IDLE;
	g_free (virt->curr_uuid);
	virt->curr_uuid = NULL;

	send_status(dev);

	fpi_drvcb_verify_stopped(dev);
	return 0;
}

static int delete_finger(struct fp_dev *dev)
{
	struct virt_dev *virt = FP_INSTANCE_DATA(dev);
	struct fp_print_data *print;
	struct fp_print_data_item *item;

	G_DEBUG_HERE();

	if (virt->state != STATE_IDLE)
		return -1;

	g_assert (virt->curr_uuid == NULL);

	virt->state = STATE_DELETE;

	print = fpi_dev_get_delete_data(dev);
	item = fpi_print_data_get_item(print);

	/* We expecte a UUID, that means 36 bytes. */
	g_assert(item->length == 36);

	virt->curr_uuid = g_malloc(37);
	virt->curr_uuid[36] = '\0';
	memcpy(virt->curr_uuid, item->data, 36);

	g_assert(g_uuid_string_is_valid (virt->curr_uuid));

	send_status(dev);

	return 0;
}

struct fp_driver virtual_misdev_driver = {
	.id = VIRTUAL_MIS_ID,
	.name = FP_COMPONENT,
	.full_name = "Virtual match-in-sensor device with internal storage",
	.bus = BUS_TYPE_VIRTUAL,
	.id_table.virtual_envvar = "FP_VIRTUAL_MISDEV",
	.scan_type = FP_SCAN_TYPE_PRESS,

	.open = dev_init,
	.close = dev_deinit,
	.enroll_start = enroll_start,
	.enroll_stop = enroll_stop,
	.verify_start = verify_start,
	.verify_stop = verify_stop,
	.delete_finger = delete_finger,
};
