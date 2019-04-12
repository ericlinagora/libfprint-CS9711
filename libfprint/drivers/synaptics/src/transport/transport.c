/*
 * Copyright (C) 2019 Synaptics Inc
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

#include "bmkt_internal.h"
#include "transport.h"
#include "bmkt_message.h"
#include "crc.h"

#define BMKT_VCSFW_COMMAND_LEN		1

extern const bmkt_transport_drv_t usb_xport_drv;

int bmkt_transport_open(bmkt_transport_t *xport, bmkt_transport_type_t xport_type,
				const bmkt_transport_config_t *config, bmkt_sensor_t *sensor)
{
	int ret;

	xport->xport_type = xport_type;
	switch (xport->xport_type)
	{
		case BMKT_TRANSPORT_TYPE_USB:
			xport->drv = &usb_xport_drv;
            break;
		break;
	}

	xport->sensor = sensor;

#ifdef THREAD_SUPPORT
	ret = bmkt_mutex_init(&xport->transfer_buffer_mutex);
	if (ret != BMKT_SUCCESS)
	{
		return ret;
	}
#endif

	return xport->drv->open(xport, config);
}

int bmkt_transport_close(bmkt_transport_t *xport)
{
#ifdef THREAD_SUPPORT
	bmkt_mutex_destroy(&xport->transfer_buffer_mutex);
#endif
	return xport->drv->close(xport);
}

int bmkt_transport_get_command_buffer(bmkt_transport_t *xport, uint8_t **buf, int *len)
{
#ifdef THREAD_SUPPORT
	int ret;

	ret = bmkt_mutex_lock(&xport->transfer_buffer_mutex);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_err_log("Failed to lock mutex: %d\n", ret);
		return ret;
	}
#endif /* THREAD_SUPPORT */
	return xport->drv->get_command_buffer(xport, buf, len);
}

int bmkt_transport_release_command_buffer(bmkt_transport_t *xport)
{
#ifdef THREAD_SUPPORT
	int ret;

	ret = bmkt_mutex_unlock(&xport->transfer_buffer_mutex);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_err_log("Failed to release mutex: %d\n", ret);
		return ret;
	}
#endif /* THREAD_SUPPORT */

	return BMKT_SUCCESS;
}

int bmkt_transport_get_response_buffer(bmkt_transport_t *xport, uint8_t **resp, int *len)
{
#ifdef THREAD_SUPPORT
	int ret;

	ret = bmkt_mutex_lock(&xport->transfer_buffer_mutex);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_err_log("Failed to lock mutex: %d\n", ret);
		return ret;
	}
#endif /* THREAD_SUPPORT */
	return xport->drv->get_response_buffer(xport, resp, len);
}

int bmkt_transport_release_response_buffer(bmkt_transport_t *xport)
{
#ifdef THREAD_SUPPORT
	int ret;

	ret = bmkt_mutex_unlock(&xport->transfer_buffer_mutex);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_err_log("Failed to unlock mutex: %d\n", ret);
		return ret;
	}
#endif /* THREAD_SUPPORT */

	return BMKT_SUCCESS;
}

int bmkt_transport_send_command(bmkt_transport_t *xport, int len)
{
	return xport->drv->send_command(xport, len);
}

int bmkt_transport_send_command_sync(bmkt_transport_t *xport, int len, uint8_t **resp_buf,
						int *resp_len)
{
	return xport->drv->send_command_sync(xport, len, resp_buf, resp_len);
}

int bmkt_transport_receive_response(bmkt_transport_t *xport, int *len)
{
	return xport->drv->receive_response(xport, len);
}

int bmkt_transport_read_interrupt_status(bmkt_transport_t *xport, int *interupt_mask)
{
	return xport->drv->read_interrupt_status(xport, interupt_mask);
}