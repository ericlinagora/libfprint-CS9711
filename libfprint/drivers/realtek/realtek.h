/*
 * Copyright (C) 2022-2023 Realtek Corp.
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

#pragma once

#include "fpi-device.h"
#include "fpi-ssm.h"

#include <stdio.h>
#include <stdlib.h>
#include <stdint.h>

#define EP_IN (2 | FPI_USB_ENDPOINT_IN)
#define EP_OUT (1 | FPI_USB_ENDPOINT_OUT)

#define EP_IN_MAX_BUF_SIZE 2048

#define FP_RTK_CMD_TOTAL_LEN 12
#define FP_RTK_CMD_LEN 2
#define FP_RTK_CMD_PARAM_LEN 4
#define FP_RTK_CMD_ADDR_LEN 4
#define FP_RTK_CMD_DATA_LEN 2

#define TEMPLATE_LEN 35
#define SUBFACTOR_OFFSET 2
#define UID_OFFSET 3
#define UID_PAYLOAD_LEN 32

/* Command transfer timeout :ms*/
#define CMD_TIMEOUT 1000
#define DATA_TIMEOUT 5000
#define STATUS_TIMEOUT 2000

#define MAX_ENROLL_SAMPLES 8
#define DEFAULT_UID_LEN 28
#define SUB_FINGER_01 0xFF

#define GET_CMD_TYPE(val)                   ((val & 0xC0) >> 6)
#define GET_TRANS_DATA_LEN(len_h, len_l)    ((len_h << 8) | len_l)
#define GET_LEN_L(total_data_len)           ((total_data_len) & 0xff)
#define GET_LEN_H(total_data_len)           ((total_data_len) >> 8)

G_DECLARE_FINAL_TYPE (FpiDeviceRealtek, fpi_device_realtek, FPI, DEVICE_REALTEK, FpDevice)

typedef void (*SynCmdMsgCallback) (FpiDeviceRealtek *self,
                                   uint8_t          *buffer_in,
                                   GError           *error);

typedef struct
{
  SynCmdMsgCallback callback;
} CommandData;

typedef enum {
  FP_RTK_CMD_ONLY = 0,
  FP_RTK_CMD_READ,
  FP_RTK_CMD_WRITE,
} FpRtkCmdType;

typedef enum {
  FP_RTK_MSG_PLAINTEXT = 0,
  FP_RTK_MSG_PLAINTEXT_NO_STATUS,
} FpRtkMsgType;

typedef enum {
  FP_RTK_PURPOSE_IDENTIFY = 0x01,     /* identify before enroll */
  FP_RTK_PURPOSE_VERIFY   = 0x02,
  FP_RTK_PURPOSE_ENROLL   = 0x04,
} FpRtkPurpose;

typedef enum {
  FP_RTK_SUCCESS = 0x0,
  FP_RTK_TOO_HIGH,
  FP_RTK_TOO_LOW,
  FP_RTK_TOO_LEFT,
  FP_RTK_TOO_RIGHT,
  FP_RTK_TOO_FAST,
  FP_RTK_TOO_SLOW,
  FP_RTK_POOR_QUALITY,
  FP_RTK_TOO_SKEWED,
  FP_RTK_TOO_SHORT,
  FP_RTK_MERGE_FAILURE,
  FP_RTK_MATCH_FAIL,
  FP_RTK_CMD_ERR,
} FpRtkInStatus;

typedef enum {
  FP_RTK_ENROLL_GET_TEMPLATE = 0,
  FP_RTK_ENROLL_BEGIN_POS,
  FP_RTK_ENROLL_CAPTURE,
  FP_RTK_ENROLL_FINISH_CAPTURE,
  FP_RTK_ENROLL_ACCEPT_SAMPLE,
  FP_RTK_ENROLL_CHECK_DUPLICATE,
  FP_RTK_ENROLL_COMMIT,
  FP_RTK_ENROLL_NUM_STATES,
} FpRtkEnrollState;

typedef enum {
  FP_RTK_VERIFY_CAPTURE = 0,
  FP_RTK_VERIFY_FINISH_CAPTURE,
  FP_RTK_VERIFY_ACCEPT_SAMPLE,
  FP_RTK_VERIFY_INDENTIFY_FEATURE,
  FP_RTK_VERIFY_UPDATE_TEMPLATE,
  FP_RTK_VERIFY_NUM_STATES,
} FpRtkVerifyState;

typedef enum {
  FP_RTK_DELETE_GET_POS = 0,
  FP_RTK_DELETE_PRINT,
  FP_RTK_DELETE_NUM_STATES,
} FpRtkDeleteState;

typedef enum {
  FP_RTK_INIT_SELECT_OS = 0,
  FP_RTK_INIT_GET_ENROLL_NUM,
  FP_RTK_INIT_NUM_STATES,
} FpRtkInitState;

typedef enum {
  FP_RTK_CMD_SEND = 0,
  FP_RTK_CMD_TRANS_DATA,
  FP_RTK_CMD_GET_STATUS,
  FP_RTK_CMD_NUM_STATES,
} FpRtkCmdState;

struct _FpiDeviceRealtek
{
  FpDevice        parent;
  FpiSsm         *task_ssm;
  FpiSsm         *cmd_ssm;
  FpiUsbTransfer *cmd_transfer;
  FpiUsbTransfer *data_transfer;
  gint            cmd_type;
  FpRtkMsgType    message_type;
  gboolean        cmd_cancellable;
  gint            enroll_stage;
  gint            max_enroll_stage;
  guchar         *read_data;
  gsize           trans_data_len;
  FpRtkPurpose    fp_purpose;
  gint            pos_index;
  gint            template_num;
};

struct realtek_fp_cmd
{
  uint8_t cmd[FP_RTK_CMD_LEN];
  uint8_t param[FP_RTK_CMD_PARAM_LEN];
  uint8_t addr[FP_RTK_CMD_ADDR_LEN];
  uint8_t data_len[FP_RTK_CMD_DATA_LEN];
};

static struct realtek_fp_cmd co_start_capture = {
  .cmd = {0x05, 0x05},
};

static struct realtek_fp_cmd co_finish_capture = {
  .cmd = {0x45, 0x06},
  .data_len = {0x05},
};

static struct realtek_fp_cmd co_accept_sample = {
  .cmd = {0x45, 0x08},
  .data_len = {0x09},
};

static struct realtek_fp_cmd tls_identify_feature = {
  .cmd = {0x45, 0x22},
  .data_len = {0x2A},
};

static struct realtek_fp_cmd co_get_enroll_num = {
  .cmd = {0x45, 0x0d},
  .data_len = {0x02},
};

static struct realtek_fp_cmd co_get_template = {
  .cmd = {0x45, 0x0E},
};

static struct realtek_fp_cmd tls_enroll_begin = {
  .cmd = {0x05, 0x20},
};

static struct realtek_fp_cmd co_check_duplicate = {
  .cmd = {0x45, 0x10},
  .data_len = {0x22},
};

static struct realtek_fp_cmd tls_enroll_commit = {
  .cmd = {0x85, 0x21},
  .data_len = {0x20},
};

static struct realtek_fp_cmd co_update_template = {
  .cmd = {0x05, 0x11},
};

static struct realtek_fp_cmd co_delete_record = {
  .cmd = {0x05, 0x0F},
};

static struct realtek_fp_cmd co_select_system = {
  .cmd = {0x05, 0x13},
};