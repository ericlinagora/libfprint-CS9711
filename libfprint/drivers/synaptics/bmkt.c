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
#include "bmkt_message.h"
#include "sensor.h"

struct bmkt_ctx
{
	bmkt_sensor_t sensor;
};

bmkt_ctx_t g_ctx;

int bmkt_init(bmkt_ctx_t **ctx)
{
	if (ctx == NULL)
	{
		return BMKT_INVALID_PARAM;
	}

	memset(&g_ctx, 0, sizeof(bmkt_ctx_t));
	*ctx = &g_ctx;

	bmkt_dbg_log("%s: context size: %ld", __func__, sizeof(bmkt_ctx_t));
	return BMKT_SUCCESS;
}

void bmkt_exit(bmkt_ctx_t *ctx)
{

	if (ctx == NULL)
	{
		return;
	}
}

int bmkt_open(bmkt_ctx_t *ctx, bmkt_sensor_t **sensor,
				bmkt_general_error_cb_t err_cb, void *err_cb_ctx, libusb_device_handle *usb_handle)
{
	int ret;

	if (ctx == NULL || sensor == NULL)
	{
		return BMKT_INVALID_PARAM;
	}

	*sensor = &ctx->sensor;

	memset(*sensor, 0, sizeof(bmkt_sensor_t));

	(*sensor)->usb_xport.handle = usb_handle;

	ret = bmkt_sensor_open(*sensor, err_cb, err_cb_ctx);
	if (ret != BMKT_SUCCESS)
	{
		return ret;
	}

	return BMKT_SUCCESS;
}

int bmkt_init_fps(bmkt_sensor_t *sensor)
{
	int ret;
	uint8_t *resp_buf;
	int resp_len;
	bmkt_response_t resp;

	if (sensor->sensor_state != BMKT_SENSOR_STATE_UNINIT)
    {
        //sensor is already initialized
        return BMKT_OPERATION_DENIED;
    }
	ret = bmkt_sensor_send_message_sync(sensor, BMKT_CMD_FPS_INIT, 0, NULL, &resp_buf, &resp_len, &resp);
	if (ret != BMKT_SUCCESS)
	{
		return ret;
	}

	if (resp.result != BMKT_SUCCESS)
	{
		return resp.result;
	}

	return bmkt_sensor_init_fps(sensor);
}

int bmkt_close(bmkt_sensor_t *sensor)
{
	if (sensor == NULL)
	{
		return BMKT_INVALID_PARAM;
	}

	return bmkt_sensor_close(sensor);
}


int bmkt_delete_enrolled_user(bmkt_sensor_t *sensor, uint8_t finger_id, const char *user_id, uint32_t user_id_len, 
                            bmkt_resp_cb_t resp_cb, void *cb_ctx)
{
	int ret;
	uint8_t payload[BMKT_MAX_USER_ID_LEN + sizeof(finger_id)];
	uint8_t payload_len;

	if (sensor == NULL)
	{
		return BMKT_INVALID_PARAM;
	}

	if (user_id_len > BMKT_MAX_USER_ID_LEN)
	{
		return BMKT_INVALID_PARAM;
	}

	memset(payload, 0, sizeof(payload));
	payload_len = user_id_len + sizeof(finger_id);
	payload[0] = finger_id;
	memcpy(&payload[1], user_id, user_id_len);

	ret = bmkt_sensor_send_message(sensor, BMKT_CMD_DEL_USER_FP, payload_len, payload, resp_cb, cb_ctx);
	if (ret != BMKT_SUCCESS)
	{
		return ret;
	}

	return BMKT_SUCCESS;
}

int bmkt_enroll(bmkt_sensor_t *sensor, const uint8_t *user_id, uint32_t user_id_len,
				uint8_t finger_id, bmkt_resp_cb_t resp_cb, void *cb_ctx)
{
	int ret = BMKT_GENERAL_ERROR;
	/* Payload data for enroll_user [1 byte<backup option> 1 byte<finger Id> maximum length: 100 bytes]*/
	uint8_t payload[BMKT_MAX_USER_ID_LEN + 2];
	uint8_t payload_len = 0;
	/* Backup options is not supported for Prometheus. */
	uint8_t backup_opt	= 0;

	if (sensor == NULL || user_id == NULL)
	{
		return BMKT_INVALID_PARAM;
	}

	if (user_id_len > BMKT_MAX_USER_ID_LEN)
	{
		return BMKT_INVALID_PARAM;
	}

	payload_len = user_id_len + 2;
	payload[0] = backup_opt;
	payload[1] = finger_id;
	memcpy(&payload[2], user_id, user_id_len);

	ret = bmkt_sensor_send_message(sensor, BMKT_CMD_ENROLL_USER, payload_len, payload, resp_cb, cb_ctx);
	if (ret != BMKT_SUCCESS)
	{
		return ret;
	}

	return BMKT_SUCCESS;
}


int bmkt_verify(bmkt_sensor_t *sensor, bmkt_user_id_t *user, 
	bmkt_resp_cb_t resp_cb, void *cb_ctx)
{
	int ret;
	uint8_t payload[BMKT_MAX_USER_ID_LEN + 1];
	uint8_t payload_len;

	if (sensor == NULL || user == NULL || user->user_id == NULL)
	{
		return BMKT_INVALID_PARAM;
	}

	if (user->user_id_len == 0 || user->user_id_len > BMKT_MAX_USER_ID_LEN)
	{
		return BMKT_INVALID_PARAM;
	}

	payload_len = user->user_id_len;
	memset(payload, 0, sizeof(payload));
	memcpy(&payload[0], user->user_id, user->user_id_len);

	ret = bmkt_sensor_send_message(sensor, BMKT_CMD_VERIFY_USER, payload_len, payload, resp_cb,
				cb_ctx);
	if (ret != BMKT_SUCCESS)
	{
		return ret;
	}

	return BMKT_SUCCESS;
}

void bmkt_op_set_state(bmkt_sensor_t* sensor, bmkt_op_state_t state)
{
	sensor->op_state = state;
}

void bmkt_op_sm(bmkt_sensor_t *sensor)
{
	int ret;
	int len = 0;
	bmkt_dbg_log("bmkt_op_sm state = %d", sensor->op_state);
	switch(sensor->op_state)
	{
		case BMKT_OP_STATE_GET_RESP:
			ret = usb_receive_resp_async(&sensor->usb_xport, &len);
			if (ret != BMKT_SUCCESS)
			{
				bmkt_dbg_log("bmkt_op_sm: usb_receive_resp_async failed %d", ret);
			}
			break;
		case BMKT_OP_STATE_WAIT_INTERRUPT:
			ret = usb_check_interrupt(&sensor->usb_xport);
			if (ret != BMKT_SUCCESS)
			{
				bmkt_dbg_log("bmkt_op_sm: check_interrupt failed %d", ret);
			}
			break;
		case BMKT_OP_STATE_SEND_ASYNC:
			ret = bmkt_sensor_send_async_read_command(sensor);
			if (ret != BMKT_SUCCESS)
			{
				bmkt_dbg_log("bmkt_op_sm: bmkt_sensor_send_async_read_command failed %d", ret);
			}
			break;
		case BMKT_OP_STATE_COMPLETE:
			break;
		default:
			break;
	}
}

void bmkt_op_next_state(bmkt_sensor_t* sensor)
{
	if(sensor->op_state != BMKT_OP_STATE_COMPLETE)
		sensor->op_state = (sensor->op_state + 1) % BMKT_OP_STATE_COMPLETE;
	bmkt_op_sm(sensor);
}



