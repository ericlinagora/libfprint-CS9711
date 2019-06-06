/*
 * Virtual driver for image device debugging
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
 * This is a virtual driver to debug the image based drivers. A small
 * python script is provided to connect to it via a socket, allowing
 * prints to be sent to this device programatically.
 * Using this it is possible to test libfprint and fprintd.
 */

#define FP_COMPONENT "virtual_imgdev"

#include <sys/types.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <fcntl.h>
#include <stdio.h>
#include "drivers_api.h"

struct virt_dev {
	fpi_io_condition *socket_io_cond;
	fpi_io_condition *client_io_cond;

	gint socket_fd;
	gint client_fd;

	struct fp_img *recv_img;
	gssize recv_img_data_bytes;
	gssize recv_img_hdr_bytes;
	gint recv_img_hdr[2];
};

static void
client_socket_cb  (struct fp_dev *dev, int fd, short int events, void *data)
{
	struct virt_dev *virt = FP_INSTANCE_DATA(dev);
	gboolean nodata = FALSE;
	gssize len;

	if (!virt->recv_img) {
		/* Reading the header, i.e. width/height. */
		len = read(fd,
			   (guint8*)virt->recv_img_hdr + virt->recv_img_hdr_bytes,
			   sizeof(virt->recv_img_hdr) - virt->recv_img_hdr_bytes);
		fp_dbg("Received %zi bytes from client!", len);

		if (len > 0) {
			virt->recv_img_hdr_bytes += len;
			/* Got the full header, create an image for further processing. */
			if (virt->recv_img_hdr_bytes == sizeof(virt->recv_img_hdr)) {
				virt->recv_img_data_bytes = 0;
				virt->recv_img = fpi_img_new (virt->recv_img_hdr[0] * virt->recv_img_hdr[1]);
				virt->recv_img->width = virt->recv_img_hdr[0];
				virt->recv_img->height = virt->recv_img_hdr[1];
				virt->recv_img->flags = 0;
			}
		}
	} else {
		len = read(fd,
			   (guint8*)virt->recv_img->data + virt->recv_img_data_bytes,
			   virt->recv_img->length - virt->recv_img_data_bytes);
		fp_dbg("Received %zi bytes from client!", len);

		if (len > 0) {
			virt->recv_img_data_bytes += len;
			if (virt->recv_img_data_bytes == virt->recv_img->length) {
				/* Submit received image to frontend */
				fpi_imgdev_report_finger_status (FP_IMG_DEV (dev), TRUE);
				fpi_imgdev_image_captured(FP_IMG_DEV (dev), virt->recv_img);
				virt->recv_img = NULL;
				fpi_imgdev_report_finger_status (FP_IMG_DEV (dev), FALSE);
			}
		}
	}

	if (len <= 0) {
		fp_dbg("Client disconnected!");
		close (virt->client_fd);
		virt->client_fd = -1;

		virt->recv_img_hdr_bytes = 0;
		if (virt->recv_img)
			fp_img_free (virt->recv_img);
		virt->recv_img = NULL;

		fpi_io_condition_remove (virt->client_io_cond);
		virt->client_io_cond = NULL;
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
}

static int
dev_init(struct fp_img_dev *dev, unsigned long driver_data)
{
	struct virt_dev *virt;
	const char *env;
	struct sockaddr_un addr = {
		.sun_family = AF_UNIX
	};
	G_DEBUG_HERE();

	virt = g_new0(struct virt_dev, 1);
	fp_dev_set_instance_data(FP_DEV(dev), virt);

	virt->client_fd = -1;

	env = fpi_dev_get_virtual_env (FP_DEV (dev));

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

	virt->socket_io_cond = fpi_io_condition_add (virt->socket_fd, POLL_IN, new_connection_cb, FP_DEV (dev), NULL);

	fpi_imgdev_open_complete(dev, 0);
	return 0;
}

static void dev_deinit(struct fp_img_dev *dev)
{
	struct virt_dev *virt = FP_INSTANCE_DATA(FP_DEV(dev));

	G_DEBUG_HERE();

	if (virt->client_fd >= 0) {
		fpi_io_condition_remove (virt->client_io_cond);
		close (virt->client_fd);
	}

	if (virt->socket_fd >= 0) {
		fpi_io_condition_remove (virt->socket_io_cond);
		close (virt->socket_fd);
	}

	g_free(virt);
	fpi_imgdev_close_complete(dev);
}

static int dev_activate(struct fp_img_dev *dev, enum fp_imgdev_state state)
{
	G_DEBUG_HERE();

	fpi_imgdev_activate_complete (dev, 0);
	return 0;
}

static void dev_deactivate(struct fp_img_dev *dev)
{
	G_DEBUG_HERE();

	fpi_imgdev_deactivate_complete (dev);
}

struct fp_img_driver virtual_imgdev_driver = {
	.driver = {
		   .id = VIRTUAL_IMG_ID,
		   .name = FP_COMPONENT,
		   .full_name = "Virtual image device for debugging",
		   .bus = BUS_TYPE_VIRTUAL,
		   .id_table.virtual_envvar = "FP_VIRTUAL_IMGDEV",
		   .scan_type = FP_SCAN_TYPE_PRESS,
		   },
	.flags = 0,

	.open = dev_init,
	.close = dev_deinit,
	.activate = dev_activate,
	.deactivate = dev_deactivate,
};
