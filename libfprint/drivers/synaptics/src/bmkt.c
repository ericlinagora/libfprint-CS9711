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

	bmkt_log_open("bmkt.log");
	bmkt_dbg_log("%s: context size: %ld\n", __func__, sizeof(bmkt_ctx_t));
	return BMKT_SUCCESS;
}

void bmkt_exit(bmkt_ctx_t *ctx)
{
	bmkt_log_close();

	if (ctx == NULL)
	{
		return;
	}
}

int bmkt_open(bmkt_ctx_t *ctx, const bmkt_sensor_desc_t *desc, bmkt_sensor_t **sensor,
				bmkt_general_error_cb_t err_cb, void *err_cb_ctx)
{
	int ret;

	if (ctx == NULL || sensor == NULL)
	{
		return BMKT_INVALID_PARAM;
	}

	*sensor = &ctx->sensor;

	memset(*sensor, 0, sizeof(bmkt_sensor_t));

	ret = bmkt_sensor_open(*sensor, desc, err_cb, err_cb_ctx);
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

int bmkt_cancel_op(bmkt_sensor_t *sensor, bmkt_resp_cb_t resp_cb, void *cb_ctx)
{
	int ret;

	if (sensor == NULL)
	{
		return BMKT_INVALID_PARAM;
	}

	ret = bmkt_sensor_send_message(sensor, BMKT_CMD_CANCEL_OP, 0, NULL, resp_cb, cb_ctx);
	if (ret != BMKT_SUCCESS)
	{
		return ret;
	}

	return BMKT_SUCCESS;
}

int bmkt_get_fps_mode(bmkt_sensor_t *sensor, bmkt_resp_cb_t resp_cb, void *cb_ctx)
{
	int ret;

	if (sensor == NULL)
	{
		return BMKT_INVALID_PARAM;
	}

	ret = bmkt_sensor_send_message(sensor, BMKT_CMD_GET_FPS_MODE, 0, NULL, resp_cb, cb_ctx);
	if (ret != BMKT_SUCCESS)
	{
		return ret;
	}

	return BMKT_SUCCESS;
}

int bmkt_get_security_level(bmkt_sensor_t *sensor, bmkt_resp_cb_t resp_cb, void *cb_ctx)
{
	int ret;

	if (sensor == NULL)
	{
		return BMKT_INVALID_PARAM;
	}

	ret = bmkt_sensor_send_message(sensor, BMKT_CMD_GET_SECURITY_LEVEL, 0, NULL, resp_cb, cb_ctx);
	if (ret != BMKT_SUCCESS)
	{
		return ret;
	}

	return BMKT_SUCCESS;
}

int bmkt_set_security_level(bmkt_sensor_t *sensor, bmkt_sec_level_t level, bmkt_resp_cb_t resp_cb,
							void *cb_ctx)
{
	int ret;

	if (sensor == NULL)
	{
		return BMKT_INVALID_PARAM;
	}

	if(level != BMKT_SECURITY_LEVEL_LOW && level != BMKT_SECURITY_LEVEL_MEDIUM &&
		level != BMKT_SECURITY_LEVEL_HIGH)
	{
		return BMKT_INVALID_PARAM;
	}

	ret = bmkt_sensor_send_message(sensor, BMKT_CMD_SET_SECURITY_LEVEL, 1, (uint8_t*)&level,
							resp_cb, cb_ctx);
	if (ret != BMKT_SUCCESS)
	{
		return ret;
	}

	return BMKT_SUCCESS;
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

int bmkt_delete_all_enrolled_users(bmkt_sensor_t *sensor, bmkt_resp_cb_t resp_cb, void *cb_ctx)
{
	int ret;

	if (sensor == NULL)
	{
		return BMKT_INVALID_PARAM;
	}

	ret = bmkt_sensor_send_message(sensor, BMKT_CMD_DEL_FULL_DB, 0, NULL, resp_cb, cb_ctx);
	if (ret != BMKT_SUCCESS)
	{
		return ret;
	}

	return BMKT_SUCCESS;
}

int bmkt_db_capacity(bmkt_sensor_t *sensor,	bmkt_resp_cb_t resp_cb, void *cb_ctx)
{
	int ret;

	if (sensor == NULL)
	{
		return BMKT_INVALID_PARAM;
	}

	ret = bmkt_sensor_send_message(sensor, BMKT_CMD_GET_DATABASE_CAPACITY, 0, NULL, resp_cb,
							cb_ctx);
	if (ret != BMKT_SUCCESS)
	{
		return ret;
	}

	return BMKT_SUCCESS;
}

int bmkt_get_enrolled_users(bmkt_sensor_t *sensor, bmkt_resp_cb_t resp_cb, void *cb_ctx)
{
    int ret;

    if (sensor == NULL)
    {
        return BMKT_INVALID_PARAM;
    }

    ret = bmkt_sensor_send_message(sensor, BMKT_CMD_GET_TEMPLATE_RECORDS, 0, NULL, resp_cb, cb_ctx);
    if (ret != BMKT_SUCCESS)
    {
        return ret;
    }

    return BMKT_SUCCESS;
}

int bmkt_get_enrolled_fingers(bmkt_sensor_t *sensor, const char *user_id, uint32_t user_id_len,
                bmkt_resp_cb_t resp_cb, void *cb_ctx)
{
	int ret;
	uint8_t payload[BMKT_MAX_USER_ID_LEN];
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
	payload_len = user_id_len;
	memcpy(&payload[0], user_id, user_id_len);

	ret = bmkt_sensor_send_message(sensor, BMKT_CMD_GET_ENROLLED_FINGERS, payload_len, payload, 
		resp_cb, cb_ctx);
	if (ret != BMKT_SUCCESS)
	{
		return ret;
	}

	return BMKT_SUCCESS;
}

int bmkt_get_version(bmkt_sensor_t *sensor, bmkt_resp_cb_t resp_cb, void *cb_ctx)
{
	int ret;

	if (sensor == NULL)
	{
		return BMKT_INVALID_PARAM;
	}

	ret = bmkt_sensor_send_message(sensor, BMKT_CMD_GET_VERSION, 0, NULL, resp_cb,
							cb_ctx);
	if (ret != BMKT_SUCCESS)
	{
		return ret;
	}

	return BMKT_SUCCESS;
}

int bmkt_process_pending_interrupts(bmkt_sensor_t *sensor)
{
	if (sensor == NULL)
	{
		return BMKT_INVALID_PARAM;
	}

	return bmkt_sensor_process_pending_interrupts(sensor);
}
