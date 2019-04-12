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

#ifndef _TRANSPORT_H_
#define _TRANSPORT_H_

#include "bmkt_internal.h"

#ifdef THREAD_SUPPORT
#include "mutex.h"
#endif /* THREAD_SUPPORT */


#include "usb_transport.h"

#define BMKT_MAX_TRANSFER_LEN 				263 + 1 /* SPI Header */ + 2 /* VCSFW header */

#define BMKT_XPORT_INT_NONE					0x0
#define BMKT_XPORT_INT_RESPONSE				0x1
#define BMKT_XPORT_INT_FINGER				0x2
#define BMKT_XPORT_INT_ASYNC				0x4

typedef struct bmkt_transport_drv bmkt_transport_drv_t;

typedef union
{
	bmkt_usb_transport_t usb_xport;
} bmtk_transport_data_t;

typedef struct bmkt_transport
{
	bmkt_transport_type_t xport_type;
	bmkt_transport_config_t xport_config;
	const bmkt_transport_drv_t *drv;
	bmkt_sensor_t *sensor;
#ifdef THREAD_SUPPORT
	bmkt_mutex_t transfer_buffer_mutex;
#endif /* THREAD_SUPPORT */
	uint8_t transfer[BMKT_MAX_TRANSFER_LEN];
	bmtk_transport_data_t xport_data;
} bmkt_transport_t;

struct bmkt_transport_drv
{
	int (*open)(bmkt_transport_t *xport, const bmkt_transport_config_t *xport_config);
	int (*close)(bmkt_transport_t *xport);

	int (*send_command)(bmkt_transport_t *xport, int len);
	int (*receive_response)(bmkt_transport_t *xport, int *len);
	int (*send_command_sync)(bmkt_transport_t *xport, int len, uint8_t **resp_buf, int *resp_len);
	int (*get_command_buffer)(bmkt_transport_t *xport, uint8_t **cmd, int *len);
	int (*get_response_buffer)(bmkt_transport_t *xport, uint8_t **resp, int *len);
	int (*reset)(bmkt_transport_t *xport);
	int (*read_interrupt_status)(bmkt_transport_t *xport, int *interupt_mask);
};

int bmkt_transport_open(bmkt_transport_t *xport, bmkt_transport_type_t xport_type,
						const bmkt_transport_config_t *config, bmkt_sensor_t *sensor);
int bmkt_transport_close(bmkt_transport_t *xport);

int bmkt_transport_get_command_buffer(bmkt_transport_t *xport, uint8_t **buf, int *len);
int bmkt_transport_release_command_buffer(bmkt_transport_t *xport);

int bmkt_transport_get_response_buffer(bmkt_transport_t *xport, uint8_t **resp, int *len);
int bmkt_transport_release_response_buffer(bmkt_transport_t *xport);

int bmkt_transport_send_command(bmkt_transport_t *xport, int len);
int bmkt_transport_send_command_sync(bmkt_transport_t *xport, int len, uint8_t **resp_buf, int *resp_len);

int bmkt_transport_receive_response(bmkt_transport_t *xport, int *len);

int bmkt_transport_read_interrupt_status(bmkt_transport_t *xport, int *interupt_mask);

#endif /* _TRANSPORT_H_ */