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

int bmkt_enroll(bmkt_sensor_t *sensor, const uint8_t *user_id, uint32_t user_id_len,
				uint8_t finger_id, bmkt_resp_cb_t resp_cb, void *cb_ctx)
{
	int ret;
	uint8_t payload[BMKT_MAX_USER_ID_LEN + 1];
	uint8_t payload_len;

	if (sensor == NULL)
	{
		return BMKT_INVALID_PARAM;
	}

	if (user_id_len > BMKT_MAX_USER_ID_LEN)
	{
		return BMKT_INVALID_PARAM;
	}

	payload_len = user_id_len + 1;
	payload[0] = finger_id;
	memcpy(&payload[1], user_id, user_id_len);

	ret = bmkt_sensor_send_message(sensor, BMKT_CMD_ENROLL_USER, payload_len, payload, resp_cb,
				cb_ctx);
	if (ret != BMKT_SUCCESS)
	{
		return ret;
	}

	return BMKT_SUCCESS;
}

int bmkt_enroll_pause(bmkt_sensor_t *sensor)
{
	int ret;

	if (sensor == NULL)
	{
		return BMKT_INVALID_PARAM;
	}

	ret = bmkt_sensor_send_message(sensor, BMKT_CMD_ENROLL_PAUSE, 0, NULL, NULL, NULL);
	if (ret != BMKT_SUCCESS)
	{
		return ret;
	}

	return BMKT_SUCCESS;
}

int bmkt_enroll_resume(bmkt_sensor_t *sensor)
{
	int ret;

	if (sensor == NULL)
	{
		return BMKT_INVALID_PARAM;
	}

	ret = bmkt_sensor_send_message(sensor, BMKT_CMD_ENROLL_RESUME, 0, NULL, NULL, NULL);
	if (ret != BMKT_SUCCESS)
	{
		return ret;
	}

	return BMKT_SUCCESS;
}