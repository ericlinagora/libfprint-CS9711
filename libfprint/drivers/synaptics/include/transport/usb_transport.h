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

#include "libusb-1.0/libusb.h"
#include "thread.h"

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
	bmkt_thread_t interrupt_thread;
	int interrupt_mask;
	uint8_t interrupt_data[USB_INTERRUPT_DATA_SIZE];
	int completed;
} bmkt_usb_transport_t;

#endif /* _USB_TRANSPORT_H_ */