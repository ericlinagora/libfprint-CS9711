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
#include "sensor.h"
#include "drivers_api.h"


#define USB_ASYNC_MESSAGE_PENDING		0x4

static void usb_int_callback(struct libusb_transfer *transfer)
{
	bmkt_usb_transport_t *usb_xport = (bmkt_usb_transport_t *)transfer->user_data;

#ifdef TRANSPORT_DEBUG
	bmkt_dbg_log("INTERRUPT: (%d) ", transfer->actual_length);
	print_buffer(transfer->buffer, transfer->actual_length);
#endif

	if (transfer->buffer[0] & USB_ASYNC_MESSAGE_PENDING)
	{
		libusb_free_transfer(transfer);
		bmkt_op_next_state(usb_xport->sensor);
	}
	else
		libusb_submit_transfer(transfer);
}

int usb_check_interrupt(bmkt_usb_transport_t *usb_xport)
{
	int ret;
	struct libusb_transfer *interrupt_xfer;
	interrupt_xfer = libusb_alloc_transfer(0);
	if (interrupt_xfer == NULL)
	{
		return (void *)BMKT_GENERAL_ERROR;
	}

	libusb_fill_interrupt_transfer(interrupt_xfer, usb_xport->handle, USB_EP_INTERRUPT,
			usb_xport->interrupt_data, sizeof(usb_xport->interrupt_data), usb_int_callback, usb_xport, 0);

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
	return;
}

int usb_open(bmkt_usb_transport_t *usb_xport)
{
	int ret;
	struct libusb_config_descriptor *configDesc;
	const struct libusb_interface *iface;
	const struct libusb_interface_descriptor *ifaceDesc;
	const struct libusb_endpoint_descriptor *endpointDesc;
	int config;
	int i;

	usb_xport->device = libusb_get_device(usb_xport->handle);
	
	ret = libusb_reset_device(usb_xport->handle);
	if (ret)
	{
		bmkt_dbg_log("Failed to reset device\n");
	}

	ret = libusb_get_config_descriptor(usb_xport->device, USB_DEFAULT_CONFIGURATION, &configDesc);
	if (ret)
	{
		ret = BMKT_SENSOR_MALFUNCTION;
		return ret;
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

free_config:
	libusb_free_config_descriptor(configDesc);

	return ret;
}

int usb_close(bmkt_usb_transport_t *usb_xport)
{
	if (usb_xport->handle)
	{
		libusb_release_interface(usb_xport->handle, USB_DEFAULT_INTERFACE);
	}

	return BMKT_SUCCESS;
}

void usb_in_cb(struct libusb_transfer *transfer)
{
	uint8_t *resp_buf;
	int resp_len;
	bmkt_msg_resp_t msg_resp;
	bmkt_usb_transport_t *usb_xport = (bmkt_usb_transport_t *)transfer->user_data;

#ifdef TRANSPORT_DEBUG
	bmkt_dbg_log("RX_ASYNC: (%d) ", transfer->actual_length);
	print_buffer(transfer->buffer, transfer->actual_length);
#endif

	resp_buf = transfer->buffer;
	resp_len = transfer->actual_length;
	bmkt_sensor_handle_response(usb_xport->sensor, resp_buf, resp_len, &msg_resp);
	libusb_free_transfer(transfer);

	bmkt_op_next_state(usb_xport->sensor);
}

void usb_out_cb(struct libusb_transfer *transfer)
{

	bmkt_usb_transport_t *usb_xport = (bmkt_usb_transport_t *)transfer->user_data;

	libusb_free_transfer(transfer);
	bmkt_op_next_state(usb_xport->sensor);
	
}

static int bulk_transfer_async(bmkt_usb_transport_t *usb_xport, uint8_t *buf, int size, uint8_t endpoint,
						int *transferred, uint32_t timeout, libusb_transfer_cb_fn callback)
{
	int ret;
	struct libusb_transfer *transfer;

#ifdef TRANSPORT_DEBUG
	if (!(endpoint & 0x80))
	{
		bmkt_dbg_log("TX2: (%d) ", size);
		print_buffer(buf, size);
	}
#endif
	
	transfer = libusb_alloc_transfer(0);
	libusb_fill_bulk_transfer( transfer, usb_xport->handle, endpoint,
		buf, size, callback, usb_xport, 0);

	ret = libusb_submit_transfer(transfer);
	if (ret != LIBUSB_SUCCESS)
	{
		libusb_free_transfer(transfer);
		if (ret == LIBUSB_ERROR_NO_DEVICE)
		{
			return BMKT_SENSOR_MALFUNCTION;
		}
		else
		{
			return BMKT_GENERAL_ERROR;
		}
	}
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

int usb_send_command(bmkt_usb_transport_t *usb_xport, int len)
{
	int ret;
	int tx_len = 0;

	ret = bulk_transfer_async(usb_xport, usb_xport->transfer, len, USB_EP_REQUEST, &tx_len, 0, usb_out_cb);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_dbg_log("Failed to send usb command\n");
		return ret;
	}

	return BMKT_SUCCESS;
}


int usb_get_command_buffer(bmkt_usb_transport_t *usb_xport, uint8_t **cmd, int *len)
{

	*len = BMKT_MAX_TRANSFER_LEN;
	*cmd = usb_xport->transfer;

	return BMKT_SUCCESS;
}

int usb_get_response_buffer(bmkt_usb_transport_t *usb_xport, uint8_t **resp, int *len)
{
	*len = BMKT_MAX_TRANSFER_LEN;
	*resp = usb_xport->transfer;

	return BMKT_SUCCESS;
}

int usb_receive_resp_async(bmkt_usb_transport_t *usb_xport, int *len)
{
	int ret;

	*len = BMKT_MAX_TRANSFER_LEN;

	/* Check to make sure the buffer is clear */
	memset(usb_xport->transfer, 0, BMKT_MAX_TRANSFER_LEN);
	
	ret = bulk_transfer_async(usb_xport, usb_xport->transfer, *len, USB_EP_REPLY, len, 0, usb_in_cb);

	if (ret != BMKT_SUCCESS)
	{
		bmkt_dbg_log("Failed to send usb command\n");
		return ret;
	}
	
	return BMKT_SUCCESS;
}


int usb_receive_resp(bmkt_usb_transport_t *usb_xport, int *len)
{
	int ret;

	*len = BMKT_MAX_TRANSFER_LEN;

	/* Check to make sure the buffer is clear */
	memset(usb_xport->transfer, 0, BMKT_MAX_TRANSFER_LEN);

	ret = bulk_transfer(usb_xport, usb_xport->transfer, *len, USB_EP_REPLY, len, 0);


	if (ret != BMKT_SUCCESS)
	{
		bmkt_dbg_log("Failed to send usb command\n");
		return ret;
	}
	
	return BMKT_SUCCESS;
}

int usb_send_command_sync(bmkt_usb_transport_t *usb_xport, int len, uint8_t **resp_buf,
						int *resp_len)
{
	int ret;
	int tx_len = 0;

	ret = bulk_transfer(usb_xport, usb_xport->transfer, len, USB_EP_REQUEST, &tx_len, 0);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_dbg_log("Failed to send usb command\n");
		return ret;
	}

	/* Check to make sure the buffer is clear */
	memset(usb_xport->transfer, 0, BMKT_MAX_TRANSFER_LEN);

	ret = bulk_transfer(usb_xport, usb_xport->transfer, *resp_len, USB_EP_REPLY, resp_len, 0);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_dbg_log("Failed to send usb command\n");
		return ret;
	}

	*resp_buf = usb_xport->transfer;

	return BMKT_SUCCESS;
}

int usb_reset(bmkt_usb_transport_t *usb_xport)
{
	return BMKT_OPERATION_DENIED;
}

int usb_release_command_buffer(bmkt_usb_transport_t *usb_xport)
{
	return BMKT_SUCCESS;
}
int usb_release_response_buffer(bmkt_usb_transport_t *usb_xport)
{
	return BMKT_SUCCESS;
}



