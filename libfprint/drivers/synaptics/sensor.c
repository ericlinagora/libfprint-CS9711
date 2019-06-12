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

#define SENSOR_CMD_GET_VERSION				1
#define SENSOR_CMD_ACE_COMMAND				167
#define SENSOR_CMD_ASYNCMSG_READ			168

#define SENSOR_FW_CMD_HEADER_LEN			1
#define SENSOR_FW_REPLY_HEADER_LEN		2

static int get_version(bmkt_sensor_t *sensor, bmkt_sensor_version_t *mis_version)
{
	int ret;
	uint8_t *resp = NULL;
	int resp_len = 40;
	uint16_t status = 0;
	uint8_t *cmd;
	int cmd_len = 0;
	int cmd_buf_len;
	int offset = 0;

	ret = usb_get_command_buffer(&sensor->usb_xport, &cmd, &cmd_buf_len);
	if (ret != BMKT_SUCCESS)
	{
		return BMKT_OUT_OF_MEMORY;
	}

	if (cmd_buf_len < SENSOR_FW_CMD_HEADER_LEN)
	{
		return BMKT_OUT_OF_MEMORY;
	}

	cmd[0] = SENSOR_CMD_GET_VERSION;
	cmd_len = 1;
	ret = usb_send_command_sync(&sensor->usb_xport, cmd_len, &resp, &resp_len);
	if (ret != BMKT_SUCCESS)
	{
		return ret;
	}

	status = extract16(resp, &offset);
	if (status)
	{
		bmkt_err_log("The sensor reported an error when sending get version command: 0x%x",
					status);
		return BMKT_SENSOR_MALFUNCTION;
	}

	if (resp_len < 38)
	{
		return BMKT_SENSOR_MALFUNCTION;
	}

	mis_version->build_time = extract32(resp, &offset);
	mis_version->build_num = extract32(resp, &offset);
	mis_version->version_major = extract8(resp, &offset);
	mis_version->version_minor = extract8(resp, &offset);
	mis_version->target = extract8(resp, &offset);
	mis_version->product = extract8(resp, &offset);

	ret = usb_release_command_buffer(&sensor->usb_xport);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_dbg_log("%s: failed to release command buffer: %d", __func__, ret);
		return ret;
	}

	return BMKT_SUCCESS;
}

static bmkt_session_ctx_t *get_empty_session_ctx(bmkt_sensor_t *sensor)
{
	bmkt_session_ctx_t *ctx;
	int i;
	int idx;

	for (i = 0; i < BMKT_MAX_PENDING_SESSIONS; i++)
	{
		idx = (sensor->empty_session_idx + i) % BMKT_MAX_PENDING_SESSIONS;
		ctx = &sensor->pending_sessions[idx];
		if (ctx->seq_num == 0)
		{
			sensor->empty_session_idx = (idx + 1) % BMKT_MAX_PENDING_SESSIONS;
			return ctx;
		}
	}

	return NULL;
}

static bmkt_session_ctx_t *get_session_ctx(bmkt_sensor_t *sensor, int seq_num)
{
	int i;
	bmkt_session_ctx_t *ctx;

	/* Sequence number of 0 is not valid for a response to
	 a command.*/
	if (seq_num == 0)
	{
		return NULL;
	}

	for (i = 0; i < BMKT_MAX_PENDING_SESSIONS; i++)
	{
		ctx = &sensor->pending_sessions[i];
		if (ctx->seq_num == seq_num)
		{
			return ctx;
		}
	}

	return NULL;
}

static int release_session_ctx(bmkt_sensor_t *sensor, bmkt_session_ctx_t *ctx)
{

	memset(ctx, 0, sizeof(bmkt_session_ctx_t));

	return BMKT_SUCCESS;
}

int bmkt_sensor_open(bmkt_sensor_t *sensor, bmkt_general_error_cb_t err_cb, void *err_cb_ctx)
{
	int ret;

	sensor->seq_num = 1;

	sensor->sensor_state = BMKT_SENSOR_STATE_UNINIT;
	sensor->usb_xport.sensor = sensor;
	ret = usb_open(&sensor->usb_xport);

	if (ret != BMKT_SUCCESS)
	{
		bmkt_err_log("Failed to open transport: %d", ret);
		return ret;
	}

	sensor->gen_err_cb = err_cb;
	sensor->gen_err_cb_ctx = err_cb_ctx;

	ret = get_version(sensor, &sensor->version);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_err_log("Failed to get version info: %d", ret);
		return ret;
	}

	bmkt_dbg_log("Build Time: %d", sensor->version.build_time);
	bmkt_dbg_log("Build Num: %d", sensor->version.build_num);
	bmkt_dbg_log("Version: %d.%d", sensor->version.version_major, sensor->version.version_minor);
	bmkt_dbg_log("Target: %d", sensor->version.target);
	bmkt_dbg_log("Product: %d", sensor->version.product);

	return BMKT_SUCCESS;
}

int bmkt_sensor_close(bmkt_sensor_t *sensor)
{
	int ret;

	sensor->sensor_state = BMKT_SENSOR_STATE_EXIT;

	ret = usb_close(&sensor->usb_xport);
	if (ret != BMKT_SUCCESS)
	{
		return ret;
	}

	sensor->sensor_state = BMKT_SENSOR_STATE_EXIT;
	return BMKT_SUCCESS;
}

int bmkt_sensor_init_fps(bmkt_sensor_t *sensor)
{
	sensor->sensor_state = BMKT_SENSOR_STATE_INIT;

	return BMKT_SUCCESS;
}

int bmkt_sensor_send_message(bmkt_sensor_t *sensor, uint8_t msg_id, uint8_t payload_size,
								uint8_t *payload, bmkt_resp_cb_t resp_cb, void *cb_ctx)
{
	int ret;
	uint8_t *cmd;
	int cmd_buf_len = 0;
	int msg_len;
	int seq_num = 0;
	bmkt_session_ctx_t *session_ctx = get_empty_session_ctx(sensor);

	if (session_ctx == NULL)
	{
		return BMKT_OPERATION_DENIED;
	}

	if (sensor->seq_num > 255) {
		/* seq. number is in range [1 – 255]. After it reaches 255, it rolls over to 1 and starts over again. 
		 (0 is reserved for special purposes) */
		sensor->seq_num = 1;
	}
	session_ctx->seq_num = sensor->seq_num++;
	session_ctx->resp_cb = resp_cb;
	session_ctx->cb_ctx = cb_ctx;

	bmkt_dbg_log("session_ctx->seq_num=%d, sensor->seq_num=%d", session_ctx->seq_num, sensor->seq_num);

	bmkt_op_set_state(sensor, BMKT_OP_STATE_START);

	ret = usb_get_command_buffer(&sensor->usb_xport, &cmd, &cmd_buf_len);
	if (ret != BMKT_SUCCESS)
	{
		return BMKT_OUT_OF_MEMORY;
	}

	/* MIS sensors send ACE commands encapsulated in FW commands*/
	cmd[0] = SENSOR_CMD_ACE_COMMAND;
	msg_len = cmd_buf_len - SENSOR_FW_CMD_HEADER_LEN;

	if (session_ctx != NULL)
	{
		seq_num = session_ctx->seq_num;
	}

	ret = bmkt_compose_message(&cmd[1], &msg_len, msg_id, seq_num, payload_size, payload);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_dbg_log("Failed to compose ace message: %d", ret);
		goto cleanup;
	}

	ret = usb_send_command(&sensor->usb_xport, msg_len + SENSOR_FW_CMD_HEADER_LEN);

	if (ret != BMKT_SUCCESS)
	{
		bmkt_dbg_log("%s: failed to send ACE command: %d", __func__, ret);
		goto cleanup;
	}

cleanup:
	usb_release_command_buffer(&sensor->usb_xport);
	if (ret != BMKT_SUCCESS)
	{
		release_session_ctx(sensor, session_ctx);
	}

	return ret;
}

int bmkt_sensor_send_async_read_command(bmkt_sensor_t *sensor)
{
	int ret;
	uint8_t *cmd;
	int cmd_buf_len = 0;

	ret = usb_get_command_buffer(&sensor->usb_xport, &cmd, &cmd_buf_len);
	if (ret != BMKT_SUCCESS)
	{
		return BMKT_OUT_OF_MEMORY;
	}

	/* MIS sensors send ACE commands encapsulated in FW commands */
	cmd[0] = SENSOR_CMD_ASYNCMSG_READ;

	ret = usb_send_command(&sensor->usb_xport, SENSOR_FW_CMD_HEADER_LEN);
	if (ret == BMKT_SENSOR_RESPONSE_PENDING)
	{
		/* The caller needs to handle the response before we can send this command */
		goto cleanup;
	}
	else if (ret != BMKT_SUCCESS)
	{
		if (ret != BMKT_SENSOR_NOT_READY)
		{
			bmkt_dbg_log("%s: failed to send ACE ASYNC READ command: %d", __func__, ret);
		}
		goto cleanup;
	}

cleanup:
	usb_release_command_buffer(&sensor->usb_xport);

	return ret;
}

int bmkt_sensor_send_message_sync(bmkt_sensor_t *sensor, uint8_t msg_id, uint8_t payload_size,
					uint8_t *payload, uint8_t **resp_buf, int *resp_len, bmkt_response_t *resp)
{
	int ret;
	uint8_t *cmd;
	int cmd_buf_len = 0;
	int msg_len;
	bmkt_msg_resp_t msg_resp;

	*resp_len = BMKT_MAX_TRANSFER_LEN;

	ret = usb_get_command_buffer(&sensor->usb_xport, &cmd, &cmd_buf_len);
	if (ret != BMKT_SUCCESS)
	{
		return BMKT_OUT_OF_MEMORY;
	}

	cmd[0] = SENSOR_CMD_ACE_COMMAND;
	msg_len = cmd_buf_len - SENSOR_FW_CMD_HEADER_LEN;

	ret = bmkt_compose_message(&cmd[1], &msg_len, msg_id, sensor->seq_num++, payload_size,
						payload);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_dbg_log("Failed to compose ace message: %d", ret);
		goto cleanup;
	}

	ret = usb_send_command_sync(&sensor->usb_xport, msg_len + SENSOR_FW_CMD_HEADER_LEN,
						resp_buf, resp_len);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_dbg_log("%s: failed to send ACE command: %d", __func__, ret);
		goto cleanup;
	}

	ret = bmkt_parse_message_header(&(*resp_buf)[2], *resp_len - 2, &msg_resp);
	if (ret != BMKT_SUCCESS)
	{
		goto cleanup;
	}

	ret = bmkt_parse_message_payload(&msg_resp, resp);
	if (ret != BMKT_SUCCESS)
	{
		goto cleanup;
	}

cleanup:
	ret = usb_release_command_buffer(&sensor->usb_xport);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_dbg_log("%s: failed to release command buffer: %d", __func__, ret);
		return ret;
	}
	return ret;
}

int bmkt_sensor_handle_response(bmkt_sensor_t *sensor, uint8_t *resp_buf, int resp_len, bmkt_msg_resp_t *msg_resp)
{
	int ret;
	bmkt_session_ctx_t *session_ctx;
	bmkt_response_t resp;
	int i;

	ret = bmkt_parse_message_header(&resp_buf[2], resp_len - 2, msg_resp);
	if (ret == BMKT_CORRUPT_MESSAGE)
	{
		bmkt_warn_log("Corrupt Message Received");
		return ret;
	}
	else if (ret != BMKT_SUCCESS)
	{
		return ret;
	}

	if (msg_resp->msg_id == BMKT_EVT_FINGER_REPORT)
	{
		/* finger event message */
		bmkt_info_log("Finger event!");
		bmkt_finger_event_t finger_event;

		if (msg_resp->payload_len != 1)
		{
			return BMKT_UNRECOGNIZED_MESSAGE;
		}

		if (msg_resp->payload[0] == 0x01)
		{
			finger_event.finger_state = BMKT_FINGER_STATE_ON_SENSOR;
		}
		else
		{
			finger_event.finger_state = BMKT_FINGER_STATE_NOT_ON_SENSOR;
		}

		if (sensor->finger_event_cb != NULL)
		{
			sensor->finger_event_cb(&finger_event, sensor->finger_cb_ctx);
		}
		return BMKT_SUCCESS;
	}

	if (msg_resp->seq_num == 0)
	{

		if (msg_resp->msg_id == BMKT_RSP_GENERAL_ERROR)
		{
			/* report general error */
			bmkt_info_log("General Error!");
			uint16_t err;

			if (sensor->gen_err_cb != NULL)
			{
				err = (msg_resp->payload[0] << 8) | msg_resp->payload[1];
				sensor->gen_err_cb(err, sensor->gen_err_cb_ctx);
			}
			return BMKT_SUCCESS;
		}
	}

	ret = bmkt_parse_message_payload(msg_resp, &resp);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_warn_log("Failed to process response: %d", ret);
		return ret;
	}

	session_ctx = get_session_ctx(sensor, msg_resp->seq_num);
	if (session_ctx == NULL)
	{
		bmkt_warn_log("Response received with invalid sequence number: %d, return BMKT_UNRECOGNIZED_MESSAGE(112)", msg_resp->seq_num);
		return BMKT_UNRECOGNIZED_MESSAGE;
	}

	if (session_ctx->resp_cb != NULL)
	{
		ret = session_ctx->resp_cb(&resp, session_ctx->cb_ctx);
		if (ret != BMKT_SUCCESS)
		{
			bmkt_warn_log("response callback failed: %d", ret);
		}
	}

	if (resp.complete == 1)
	{
		ret = release_session_ctx(sensor, session_ctx);
		if (ret != BMKT_SUCCESS)
		{
			return ret;
		}
	}

	if (resp.response_id == BMKT_RSP_CANCEL_OP_OK && resp.result == BMKT_SUCCESS)
	{
		/* The previous commands have been canceled. Release all session ctx */
		for (i = 0; i < BMKT_MAX_PENDING_SESSIONS; i++)
		{
			release_session_ctx(sensor, &sensor->pending_sessions[i]);
		}
	}

	return BMKT_SUCCESS;
}

int bmkt_register_finger_event_notification(bmkt_sensor_t *sensor, bmkt_event_cb_t cb, void *cb_ctx)
{
	if (sensor == NULL || cb == NULL)
	{
		return BMKT_INVALID_PARAM;
	}

	sensor->finger_event_cb = cb;
	sensor->finger_cb_ctx = cb_ctx;

	return BMKT_SUCCESS;
}
