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

	ret = bmkt_transport_get_command_buffer(&sensor->xport, &cmd, &cmd_buf_len);
	if (ret != BMKT_SUCCESS)
	{
		return BMKT_OUT_OF_MEMORY;
	}

	if (cmd_buf_len < SENSOR_FW_CMD_HEADER_LEN)
	{
		return BMKT_OUT_OF_MEMORY;
	}

	encode8(SENSOR_CMD_GET_VERSION, cmd, &cmd_len, BYTE_ORDER_SENSOR);

	ret = bmkt_transport_send_command_sync(&sensor->xport, cmd_len, &resp, &resp_len);
	if (ret != BMKT_SUCCESS)
	{
		return ret;
	}

	status = extract16(resp, &offset, BYTE_ORDER_SENSOR);
	if (status)
	{
		bmkt_err_log("The sensor reported an error when sending get version command: 0x%x\n",
					status);
		return BMKT_SENSOR_MALFUNCTION;
	}

	if (resp_len < 38)
	{
		return BMKT_SENSOR_MALFUNCTION;
	}

	mis_version->build_time = extract32(resp, &offset, BYTE_ORDER_SENSOR);
	mis_version->build_num = extract32(resp, &offset, BYTE_ORDER_SENSOR);
	mis_version->version_major = extract8(resp, &offset, BYTE_ORDER_SENSOR);
	mis_version->version_minor = extract8(resp, &offset, BYTE_ORDER_SENSOR);
	mis_version->target = extract8(resp, &offset, BYTE_ORDER_SENSOR);
	mis_version->product = extract8(resp, &offset, BYTE_ORDER_SENSOR);

	ret = bmkt_transport_release_command_buffer(&sensor->xport);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_dbg_log("%s: failed to release command buffer: %d\n", __func__, ret);
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

	// Sequence number of 0 is not valid for a response to
	// a command.
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

int bmkt_sensor_open(bmkt_sensor_t *sensor, const bmkt_sensor_desc_t *desc,
						bmkt_general_error_cb_t err_cb, void *err_cb_ctx)
{
	int ret;

	sensor->seq_num = 1;
	sensor->flags = desc->flags;

	sensor->sensor_state = BMKT_SENSOR_STATE_UNINIT;

	ret = bmkt_transport_open(&sensor->xport, desc->xport_type, &desc->xport_config, sensor);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_err_log("Failed to open transport: %d\n", ret);
		return ret;
	}

	sensor->gen_err_cb = err_cb;
	sensor->gen_err_cb_ctx = err_cb_ctx;

	ret = get_version(sensor, &sensor->version);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_err_log("Failed to get version info: %d\n", ret);
		return ret;
	}

	bmkt_dbg_log("Build Time: %d\n", sensor->version.build_time);
	bmkt_dbg_log("Build Num: %d\n", sensor->version.build_num);
	bmkt_dbg_log("Version: %d.%d\n", sensor->version.version_major, sensor->version.version_minor);
	bmkt_dbg_log("Target: %d\n", sensor->version.target);
	bmkt_dbg_log("Product: %d\n", sensor->version.product);

	ret = bmkt_event_init(&sensor->interrupt_event);
	if (ret != BMKT_SUCCESS)
	{
		return ret;
	}

#ifdef THREAD_SUPPORT
	ret = bmkt_mutex_init(&sensor->interrupt_mutex);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_err_log("Failed to initialize interrupt mutex: %d\n", ret);
		return ret;
	}

	ret = bmkt_thread_create(&sensor->interrupt_thread, bmkt_interrupt_thread, sensor);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_err_log("Failed to start interrupt thread: %d\n", ret);
		return ret;
	}
#endif /* THREAD_SUPPORT */


	return BMKT_SUCCESS;
}

int bmkt_sensor_close(bmkt_sensor_t *sensor)
{
	int ret;

	sensor->sensor_state = BMKT_SENSOR_STATE_EXIT;
	bmkt_event_set(&sensor->interrupt_event);

#ifdef THREAD_SUPPORT
	ret = bmkt_thread_destroy(&sensor->interrupt_thread);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_err_log("Failed to destroy interrupt thread: %d\n", ret);
		return ret;
	}

	bmkt_mutex_destroy(&sensor->interrupt_mutex);
#endif /* THREAD_SUPPORT */

	ret = bmkt_event_destroy(&sensor->interrupt_event);
	if (ret != BMKT_SUCCESS)
	{
		// warn
	}

	ret = bmkt_transport_close(&sensor->xport);
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
	bmkt_event_set(&sensor->interrupt_event);

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
    // seq. number is in range [1 – 255]. After it reaches 255, it rolls over to 1 and starts over again. 
    // (0 is reserved for special purposes)
    sensor->seq_num = 1;
  }
	session_ctx->seq_num = sensor->seq_num++;
	session_ctx->resp_cb = resp_cb;
	session_ctx->cb_ctx = cb_ctx;

  bmkt_dbg_log("session_ctx->seq_num=%d, sensor->seq_num=%d\n", session_ctx->seq_num, sensor->seq_num);

	for (;;)
	{
		ret = bmkt_transport_get_command_buffer(&sensor->xport, &cmd, &cmd_buf_len);
		if (ret != BMKT_SUCCESS)
		{
			return BMKT_OUT_OF_MEMORY;
		}

		// MIS sensors send ACE commands encapsulated in FW commands
		cmd[0] = SENSOR_CMD_ACE_COMMAND;
		msg_len = cmd_buf_len - SENSOR_FW_CMD_HEADER_LEN;

		if (session_ctx != NULL)
		{
			seq_num = session_ctx->seq_num;
		}

		ret = bmkt_compose_message(&cmd[1], &msg_len, msg_id, seq_num, payload_size, payload);
		if (ret != BMKT_SUCCESS)
		{
			bmkt_dbg_log("Failed to compose ace message: %d\n", ret);
			goto cleanup;
		}

		ret = bmkt_transport_send_command(&sensor->xport, msg_len + SENSOR_FW_CMD_HEADER_LEN);
		if (ret == BMKT_SENSOR_RESPONSE_PENDING)
		{
			bmkt_transport_release_command_buffer(&sensor->xport);
			ret = bmkt_sensor_handle_interrupt(sensor);
			if (ret != BMKT_SUCCESS)
			{
				bmkt_dbg_log("bmkt_sensor_send_message: Failed to handle interrupt: %d\n", ret);
				goto cleanup;
			}
			continue;
		}
		else if (ret != BMKT_SUCCESS)
		{
			bmkt_dbg_log("%s: failed to send ACE command: %d\n", __func__, ret);
			goto cleanup;
		}
		break;
	}

	sensor->expect_response = 1;

cleanup:
	bmkt_transport_release_command_buffer(&sensor->xport);
	if (ret != BMKT_SUCCESS)
	{
		release_session_ctx(sensor, session_ctx);
	}

	return ret;
}

static int bmkt_sensor_send_async_read_command(bmkt_sensor_t *sensor)
{
	int ret;
	uint8_t *cmd;
	int cmd_buf_len = 0;

	ret = bmkt_transport_get_command_buffer(&sensor->xport, &cmd, &cmd_buf_len);
	if (ret != BMKT_SUCCESS)
	{
		return BMKT_OUT_OF_MEMORY;
	}

	// MIS sensors send ACE commands encapsulated in FW commands
	cmd[0] = SENSOR_CMD_ASYNCMSG_READ;

	ret = bmkt_transport_send_command(&sensor->xport, SENSOR_FW_CMD_HEADER_LEN);
	if (ret == BMKT_SENSOR_RESPONSE_PENDING)
	{
		// The caller needs to handle the response before we can send this command
		goto cleanup;
	}
	else if (ret != BMKT_SUCCESS)
	{
		if (ret != BMKT_SENSOR_NOT_READY)
		{
			bmkt_dbg_log("%s: failed to send ACE ASYNC READ command: %d\n", __func__, ret);
		}
		goto cleanup;
	}

	sensor->expect_response = 1;

cleanup:
	bmkt_transport_release_command_buffer(&sensor->xport);

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

	ret = bmkt_transport_get_command_buffer(&sensor->xport, &cmd, &cmd_buf_len);
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
		bmkt_dbg_log("Failed to compose ace message: %d\n", ret);
		goto cleanup;
	}

	ret = bmkt_transport_send_command_sync(&sensor->xport, msg_len + SENSOR_FW_CMD_HEADER_LEN,
						resp_buf, resp_len);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_dbg_log("%s: failed to send ACE command: %d\n", __func__, ret);
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
	ret = bmkt_transport_release_command_buffer(&sensor->xport);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_dbg_log("%s: failed to release command buffer: %d\n", __func__, ret);
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

	sensor->expect_response = 0;
	ret = bmkt_parse_message_header(&resp_buf[2], resp_len - 2, msg_resp);
	if (ret == BMKT_CORRUPT_MESSAGE)
	{
		bmkt_warn_log("Corrupt Message Received\n");
		return ret;
	}
	else if (ret != BMKT_SUCCESS)
	{
		return ret;
	}

#if 0 // invalid finger event seq num hack!!
	if (msg_resp->seq_num == 0)
	{
#endif // invalid finger event seq num hack!!
		if (msg_resp->msg_id == BMKT_EVT_FINGER_REPORT)
		{
			// finger event message
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
#if 0 // invalid finger event seq num hack!!
		else
#else // invalid finger event seq num hack!!
	if (msg_resp->seq_num == 0)
	{
#endif // invalid finger event seq num hack!!
		if (msg_resp->msg_id == BMKT_RSP_GENERAL_ERROR)
		{
			// report general error
			bmkt_info_log("General Error!\n");
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
		bmkt_warn_log("Failed to process response: %d\n", ret);
		return ret;
	}

	session_ctx = get_session_ctx(sensor, msg_resp->seq_num);
	if (session_ctx == NULL)
	{
		bmkt_warn_log("Response received with invalid sequence number: %d, return BMKT_UNRECOGNIZED_MESSAGE(112)\n", msg_resp->seq_num);
		return BMKT_UNRECOGNIZED_MESSAGE;
	}

	if (session_ctx->resp_cb != NULL)
	{
		ret = session_ctx->resp_cb(&resp, session_ctx->cb_ctx);
		if (ret != BMKT_SUCCESS)
		{
			bmkt_warn_log("response callback failed: %d\n", ret);
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
		// The previous commands have been canceled. Release all session ctx
		for (i = 0; i < BMKT_MAX_PENDING_SESSIONS; i++)
		{
			release_session_ctx(sensor, &sensor->pending_sessions[i]);
		}
	}

	return BMKT_SUCCESS;
}

int bmkt_sensor_handle_interrupt(bmkt_sensor_t *sensor)
{
	int ret = BMKT_SUCCESS;
	int mask = 0;
	int len = 0;
	uint8_t *resp_buf;
	int resp_len;
	bmkt_msg_resp_t msg_resp;

#ifdef THREAD_SUPPORT
	ret = bmkt_mutex_lock(&sensor->interrupt_mutex);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_err_log("Failed to lock mutex: %d\n", ret);
		return ret;
	}
#endif /* THREAD_SUPPORT */

	for (;;)
	{
		ret = bmkt_transport_read_interrupt_status(&sensor->xport, &mask);
		if (ret != BMKT_SUCCESS)
		{
			bmkt_dbg_log("bmkt_sensor_handle_interrupt: bmkt_transport_read_interrupt_status failed with ret=0x%x\n", ret);
			goto cleanup;
		}

		if (mask == BMKT_XPORT_INT_NONE)
		{
			ret = BMKT_SUCCESS;
			bmkt_dbg_log("bmkt_sensor_handle_interrupt: bmkt_transport_read_interrupt_status get mask=0, ret BMKT_SUCCESS\n");
			goto cleanup;
		}

		if (mask & BMKT_XPORT_INT_RESPONSE)
		{
			ret = bmkt_transport_get_response_buffer(&sensor->xport, &resp_buf, &resp_len);
			if (ret != BMKT_SUCCESS)
			{
				bmkt_dbg_log("bmkt_sensor_handle_interrupt: bmkt_transport_get_response_buffer failed with ret=0x%x\n", ret);
				goto cleanup;
			}
			ret = bmkt_transport_receive_response(&sensor->xport, &len);
			if (ret == BMKT_SUCCESS)
			{
				ret = bmkt_sensor_handle_response(sensor, resp_buf, resp_len, &msg_resp);
			}
			bmkt_transport_release_response_buffer(&sensor->xport);

			if (ret != BMKT_SUCCESS)
			{
				goto cleanup;
			}
		}

		if (mask & BMKT_XPORT_INT_FINGER)
		{
			// may not use this
		}

		if (mask & BMKT_XPORT_INT_ASYNC)
		{
			ret = bmkt_sensor_send_async_read_command(sensor);
			if (ret != BMKT_SUCCESS)
			{
				if (ret == BMKT_SENSOR_NOT_READY || ret == BMKT_SENSOR_RESPONSE_PENDING)
				{
					continue;
				}
				else
				{
					goto cleanup;
				}
			}
		}

		break;
	}

cleanup:
#ifdef THREAD_SUPPORT
	bmkt_mutex_unlock(&sensor->interrupt_mutex);
#endif /* THREAD_SUPPORT */
	return BMKT_SUCCESS;
}

int bmkt_sensor_process_pending_interrupts(bmkt_sensor_t *sensor)
{
	int ret;

	if (sensor->sensor_state == BMKT_SENSOR_STATE_UNINIT || 
				(!(sensor->expect_response) && !(sensor->flags & BMKT_SENSOR_FLAGS_POLLING)))
	{
		bmkt_event_wait(&sensor->interrupt_event, 0);
	}

	ret = bmkt_sensor_handle_interrupt(sensor);
	if (ret != BMKT_SUCCESS)
	{
		bmkt_warn_log("bmkt_sensor_process_pending_interrupts: Failed to handle interrupt: %d\n", ret);
		return ret;
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
