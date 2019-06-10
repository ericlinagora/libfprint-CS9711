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

#ifndef _USB_TRANSPORT_H_
#define _USB_TRANSPORT_H_

#include "bmkt_internal.h"
#include "libusb-1.0/libusb.h"



#define BMKT_MAX_TRANSFER_LEN 				263 + 1 /* SPI Header */ + 2 /* VCSFW header */

#define BMKT_XPORT_INT_NONE					0x0
#define BMKT_XPORT_INT_RESPONSE				0x1
#define BMKT_XPORT_INT_FINGER				0x2
#define BMKT_XPORT_INT_ASYNC				0x4

#define USB_DEFAULT_CONFIGURATION		0
#define USB_DEFAULT_INTERFACE			0
#define USB_DEFAULT_ALT_SETTING			0

#define USB_EP_REQUEST					0x01
#define USB_EP_REPLY					0x81
#define USB_EP_FINGERPRINT				0x82
#define USB_EP_INTERRUPT				0x83

#define USB_INTERRUPT_DATA_SIZE			7


typedef struct bmkt_usb_transport
{
	libusb_context *ctx;
	libusb_device *device;
	libusb_device_handle *handle;
	uint8_t interrupt_data[USB_INTERRUPT_DATA_SIZE];
	bmkt_sensor_t *sensor;
	uint8_t transfer[BMKT_MAX_TRANSFER_LEN];
} bmkt_usb_transport_t;


int usb_release_command_buffer(bmkt_usb_transport_t *xport);
int usb_release_response_buffer(bmkt_usb_transport_t *xport);




int usb_open(bmkt_usb_transport_t *xport);
int usb_close(bmkt_usb_transport_t *xport);
int usb_send_command(bmkt_usb_transport_t *xport, int len);
int usb_get_command_buffer(bmkt_usb_transport_t *xport, uint8_t **cmd, int *len);
int usb_get_response_buffer(bmkt_usb_transport_t *xport, uint8_t **resp, int *len);
int usb_receive_resp(bmkt_usb_transport_t *xport, int *len);

int usb_send_command_sync(bmkt_usb_transport_t *xport, int len, uint8_t **resp_buf,
						int *resp_len);
int usb_receive_resp_async(bmkt_usb_transport_t *usb_xport, int *len);
int usb_check_interrupt(bmkt_usb_transport_t *usb_xport);


#endif /* _USB_TRANSPORT_H_ */