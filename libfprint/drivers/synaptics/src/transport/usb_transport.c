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
#include "usb_transport.h"
#include "sensor.h"

#define USB_ASYNC_MESSAGE_PENDING		0x4

#ifdef WIN32
static void WINAPI usb_xfer_callback(struct libusb_transfer *transfer)
#else
static void usb_xfer_callback(struct libusb_transfer *transfer)
#endif /* WIN32 */
{
	bmkt_transport_t *xport = (bmkt_transport_t *)transfer->user_data;
	bmkt_usb_transport_t *usb_xport = &xport->xport_data.usb_xport;

#ifdef TRANSPORT_DEBUG
	bmkt_dbg_log("INTERRUPT: (%d) ", transfer->actual_length);
	print_buffer(transfer->buffer, transfer->actual_length);
#endif

	usb_xport->interrupt_mask = BMKT_XPORT_INT_NONE;

	if (transfer->buffer[5] & USB_ASYNC_MESSAGE_PENDING)
	{
		usb_xport->interrupt_mask |= BMKT_XPORT_INT_ASYNC;
	}

	//always send async read for now.
	usb_xport->interrupt_mask |= BMKT_XPORT_INT_ASYNC;

	bmkt_event_set(&xport->sensor->interrupt_event);

	libusb_submit_transfer(transfer);
}

#ifdef WIN32
static DWORD WINAPI usb_interrupt_thread(LPVOID ctx)
#else
static void *usb_interrupt_thread(void *ctx)
#endif
{
	int ret;
	bmkt_transport_t *xport = (bmkt_transport_t *)ctx;
	bmkt_usb_transport_t *usb_xport = &xport->xport_data.usb_xport;
	struct libusb_transfer *interrupt_xfer;

	interrupt_xfer = libusb_alloc_transfer(0);
	if (interrupt_xfer == NULL)
	{
		return (void *)BMKT_GENERAL_ERROR;
	}

	libusb_fill_interrupt_transfer(interrupt_xfer, usb_xport->handle, USB_EP_INTERRUPT,
				usb_xport->interrupt_data, sizeof(usb_xport->interrupt_data), usb_xfer_callback, xport, 0);

	ret = libusb_submit_transfer(interrupt_xfer);
	if (ret != LIBUSB_SUCCESS)
	{
		libusb_free_transfer(interrupt_xfer);
		if (ret == LIBUSB_ERROR_NO_DEVICE)
		{
			return (void *)BMKT_SENSOR_MALFUNCTION;
		}
		else
		{
			return (void *)BMKT_GENERAL_ERROR;
		}
	}

	for (;;)
	{
		ret = libusb_handle_events_completed(usb_xport->ctx, &usb_xport->completed);
		if (ret)
		{
			if (ret == LIBUSB_ERROR_INTERRUPTED)
			{
				continue;
			}
			bmkt_err_log("Failed to handle event timeout: %d\n", ret);
		}
		if(usb_xport->completed)
		{
			bmkt_err_log("interrupt thread going to terminated. \n");
			break;
		}
	}

	libusb_free_transfer(interrupt_xfer);

	return (void *)BMKT_SUCCESS;
}

static int find_fps_device(bmkt_usb_transport_t *usb_xport, const bmkt_usb_config_t *usb_config)
{
    int ret = BMKT_GENERAL_ERROR;
	int count;
	int i;
	libusb_device **devs;
	struct libusb_device_descriptor desc;

	count = libusb_get_device_list(usb_xport->ctx, &devs);
	if (count < 0)
	{
		return ret;
	}

	for (i = 0; i < count; i++)
	{
		ret = libusb_get_device_descriptor(devs[i], &desc);
		if (ret < 0)
		{
			ret = BMKT_GENERAL_ERROR;
			goto cleanup;
		}

		if (desc.idVendor != 0x06CB)
		{
			continue;
		}

		if (desc.idProduct != usb_config->product_id)
		{
			continue;
		}

		// Add additional checks to make sure we have the right FPS?
		usb_xport->device = libusb_ref_device(devs[i]);

		ret = BMKT_SUCCESS;
		break;
	}

cleanup:
	libusb_free_device_list(devs, 1);
	return ret;
}

static int usb_open(bmkt_transport_t *xport, const bmkt_transport_config_t *xport_config)
{
	int ret;
	bmkt_usb_transport_t *usb_xport = &xport->xport_data.usb_xport;
	xport->xport_config.usb_config = xport_config->usb_config;
	bmkt_usb_config_t *usb_config = &xport->xport_config.usb_config;
	struct libusb_config_descriptor *configDesc;
	const struct libusb_interface *iface;
	const struct libusb_interface_descriptor *ifaceDesc;
	const struct libusb_endpoint_descriptor *endpointDesc;
	int config;
	int i;
	usb_xport->completed = 0;
	ret = libusb_init(&usb_xport->ctx);
	if (ret)
	{
		return BMKT_GENERAL_ERROR;
	}

	ret = find_fps_device(usb_xport, usb_config);
	if (ret != BMKT_SUCCESS)
	{
		return ret;
	}

	if (usb_xport->device == NULL)
	{
		return BMKT_GENERAL_ERROR;
	}

	ret = libusb_open(usb_xport->device, &usb_xport->handle);
	if (ret)
	{
		return BMKT_SENSOR_MALFUNCTION;
	}

	ret = libusb_reset_device(usb_xport->handle);
	if (ret)
	{
		bmkt_dbg_log("Failed to reset device\n");
	}

	ret = libusb_get_config_descriptor(usb_xport->device, USB_DEFAULT_CONFIGURATION, &configDesc);
	if (ret)
	{
		ret = BMKT_SENSOR_MALFUNCTION;
		goto close_handle;
	}

	ret = libusb_get_configuration(usb_xport->handle, &config);
	if (ret)
	{
		ret = BMKT_SENSOR_MALFUNCTION;
		goto free_config;
	}

	if (configDesc->bConfigurationValue != config)
	{
		ret = libusb_set_configuration(usb_xport->handle, config);
		if (ret)
		{
			ret = BMKT_SENSOR_MALFUNCTION;
			goto free_config;
		}
	}

	ret = libusb_kernel_driver_active(usb_xport->handle, 0);
	if (ret == 1)
	{
		bmkt_err_log("Failed to detect kernel driver\n");
		ret = BMKT_SENSOR_MALFUNCTION;
		goto free_config;
	}

	ret = libusb_claim_interface(usb_xport->handle, USB_DEFAULT_INTERFACE);
	if (ret)
	{
		ret = BMKT_SENSOR_MALFUNCTION;
		goto free_config;
	}

	iface = configDesc->interface + USB_DEFAULT_INTERFACE;
	ifaceDesc = iface->altsetting + USB_DEFAULT_ALT_SETTING;
	endpointDesc = ifaceDesc->endpoint;

	for (i = 0; i < ifaceDesc->bNumEndpoints; i++)
	{
		ret = libusb_clear_halt(usb_xport->handle, endpointDesc->bEndpointAddress);
		if (ret)
		{
			ret = BMKT_SENSOR_MALFUNCTION;
			goto free_config;
		}
		++endpointDesc;
	}

	ret = bmkt_event_init(&xport->sensor->interrupt_event);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_err_log("Failed to initialize interrupt event: %d\n", ret);
		return ret;
	}

	ret = bmkt_thread_create(&usb_xport->interrupt_thread, usb_interrupt_thread, xport);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_err_log("Failed to create interrupt thread: %d\n", ret);
		return ret;
	}

free_config:
	libusb_free_config_descriptor(configDesc);
close_handle:
	if (ret)
	{
		libusb_close(usb_xport->handle);
	}

	return ret;
}

static int usb_close(bmkt_transport_t *xport)
{
	int ret;
	bmkt_usb_transport_t *usb_xport = &xport->xport_data.usb_xport;
	usb_xport->completed = 1;
	//set completed to 1 instead of thread destroy

	ret = bmkt_event_destroy(&xport->sensor->interrupt_event);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_err_log("Failed to initialize interrupt event: %d\n", ret);
		return ret;
	}

	if (usb_xport->handle)
	{
		libusb_release_interface(usb_xport->handle, USB_DEFAULT_INTERFACE);
		libusb_close(usb_xport->handle);
	}
	libusb_exit(usb_xport->ctx);

	return BMKT_SUCCESS;
}

static int bulk_transfer(bmkt_usb_transport_t *usb_xport, uint8_t *buf, int size, uint8_t endpoint,
						int *transferred, uint32_t timeout)
{
	int ret;

#ifdef TRANSPORT_DEBUG
	if (!(endpoint & 0x80))
	{
		bmkt_dbg_log("TX: (%d) ", size);
		print_buffer(buf, size);
	}
#endif

	ret = libusb_bulk_transfer(usb_xport->handle, endpoint, buf, size, transferred, timeout);
	if (ret)
	{
		bmkt_warn_log("libusb_bulk_transfer: bulk transfer failed: %d\n", ret);
		if (ret == LIBUSB_ERROR_TIMEOUT)
		{
			return BMKT_OP_TIME_OUT;
		}
		else
		{
			return BMKT_SENSOR_MALFUNCTION;
		}
	}
	bmkt_dbg_log("transferred: %d\n", *transferred);

#ifdef TRANSPORT_DEBUG
	if (endpoint & 0x80)
	{
		bmkt_dbg_log("RX: (%d) ", *transferred);
		print_buffer(buf, *transferred);
	}
#endif

	return BMKT_SUCCESS;
}

static int usb_send_command(bmkt_transport_t *xport, int len)
{
	int ret;
	int tx_len = 0;
	bmkt_usb_transport_t *usb_xport = &xport->xport_data.usb_xport;

	ret = bulk_transfer(usb_xport, xport->transfer, len, USB_EP_REQUEST, &tx_len, 0);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_dbg_log("Failed to send usb command\n");
		return ret;
	}

	usb_xport->interrupt_mask = BMKT_XPORT_INT_RESPONSE;
	bmkt_event_set(&xport->sensor->interrupt_event);

	return BMKT_SUCCESS;
}

static int usb_get_command_buffer(bmkt_transport_t *xport, uint8_t **cmd, int *len)
{
	*len = BMKT_MAX_TRANSFER_LEN;
	*cmd = xport->transfer;

	return BMKT_SUCCESS;
}

static int usb_get_response_buffer(bmkt_transport_t *xport, uint8_t **resp, int *len)
{
	*len = BMKT_MAX_TRANSFER_LEN;
	*resp = xport->transfer;

	return BMKT_SUCCESS;
}

static int usb_receive_resp(bmkt_transport_t *xport, int *len)
{
	int ret;
	bmkt_usb_transport_t *usb_xport = &xport->xport_data.usb_xport;

	*len = BMKT_MAX_TRANSFER_LEN;

	// Check to make sure the buffer is clear
	memset(xport->transfer, 0, BMKT_MAX_TRANSFER_LEN);

	ret = bulk_transfer(usb_xport, xport->transfer, *len, USB_EP_REPLY, len, 0);

	usb_xport->interrupt_mask &= ~BMKT_XPORT_INT_RESPONSE;

	if (ret != BMKT_SUCCESS)
	{
		bmkt_dbg_log("Failed to send usb command\n");
		return ret;
	}
	
	return BMKT_SUCCESS;
}

static int usb_send_command_sync(bmkt_transport_t *xport, int len, uint8_t **resp_buf,
						int *resp_len)
{
	int ret;
	int tx_len = 0;
	bmkt_usb_transport_t *usb_xport = &xport->xport_data.usb_xport;

	ret = bulk_transfer(usb_xport, xport->transfer, len, USB_EP_REQUEST, &tx_len, 0);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_dbg_log("Failed to send usb command\n");
		return ret;
	}

	// Check to make sure the buffer is clear
	memset(xport->transfer, 0, BMKT_MAX_TRANSFER_LEN);

	ret = bulk_transfer(usb_xport, xport->transfer, *resp_len, USB_EP_REPLY, resp_len, 0);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_dbg_log("Failed to send usb command\n");
		return ret;
	}

	*resp_buf = xport->transfer;

	return BMKT_SUCCESS;
}

static int usb_reset(bmkt_transport_t *xport)
{
	return BMKT_OPERATION_DENIED;
}

static int usb_read_interrupt_status(bmkt_transport_t *xport, int *interrupt_mask)
{
	bmkt_usb_transport_t *usb_xport = &xport->xport_data.usb_xport;

	*interrupt_mask = usb_xport->interrupt_mask;

	return BMKT_SUCCESS;
}

const bmkt_transport_drv_t usb_xport_drv = {
	.open = usb_open,
	.close = usb_close,
	.send_command = usb_send_command,
	.send_command_sync = usb_send_command_sync,
	.receive_response = usb_receive_resp,
	.get_command_buffer = usb_get_command_buffer,
	.get_response_buffer = usb_get_response_buffer,
	.reset = usb_reset,
	.read_interrupt_status = usb_read_interrupt_status,
};
