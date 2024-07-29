/*
 * Driver for ELAN Match-On-Chip sensors
 * Copyright (C) 2021-2023 Davide Depau <davide@depau.eu>
 *
 * Based on original reverse-engineering work by Davide Depau. The protocol has
 * been reverse-engineered from captures of the official Windows driver, and by
 * testing commands on the sensor with a multiplatform Python prototype driver:
 * https://github.com/depau/Elan-Fingerprint-0c4c-PoC/
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

// Stdlib includes
#include <stdbool.h>

// Library includes
#include <libusb.h>

// Local includes
#include "fpi-device.h"
#include "fpi-ssm.h"

#define ELANMOC2_DRIVER_FULLNAME "ELAN Match-on-Chip 2"
#define ELANMOC2_VEND_ID 0x04f3

#define ELANMOC2_ENROLL_TIMES 8
#define ELANMOC2_CMD_MAX_LEN 2
#define ELANMOC2_MAX_PRINTS 10

// USB parameters
#define ELANMOC2_EP_CMD_OUT (0x1 | FPI_USB_ENDPOINT_OUT)
#define ELANMOC2_EP_CMD_IN (0x3 | FPI_USB_ENDPOINT_IN)
#define ELANMOC2_EP_MOC_CMD_IN (0x4 | FPI_USB_ENDPOINT_IN)
#define ELANMOC2_USB_SEND_TIMEOUT 10000
#define ELANMOC2_USB_RECV_TIMEOUT 10000

// Response codes
#define ELANMOC2_RESP_MOVE_DOWN 0x41
#define ELANMOC2_RESP_MOVE_RIGHT 0x42
#define ELANMOC2_RESP_MOVE_UP 0x43
#define ELANMOC2_RESP_MOVE_LEFT 0x44
#define ELANMOC2_RESP_MAX_ENROLLED_REACHED 0xdd
#define ELANMOC2_RESP_SENSOR_DIRTY 0xfb
#define ELANMOC2_RESP_NOT_ENROLLED 0xfd
#define ELANMOC2_RESP_NOT_ENOUGH_SURFACE 0xfe

// Currently only one device is supported, but I'd like to future-proof this driver for any new contributions.
#define ELANMOC2_ALL_DEV 0
#define ELANMOC2_DEV_0C4C (1 << 0)
#define ELANMOC2_DEV_0C5E (2 << 0)

// Subtract the 2-byte header
#define ELANMOC2_USER_ID_MAX_LEN (cmd_finger_info.in_len - 2)
#define ELANMOC2_USER_ID_MAX_LEN_0C5E (cmd_finger_info.in_len - 3)

G_DECLARE_FINAL_TYPE (FpiDeviceElanMoC2, fpi_device_elanmoc2, FPI, DEVICE_ELANMOC2, FpDevice)

struct elanmoc2_cmd
{
  unsigned char  cmd[ELANMOC2_CMD_MAX_LEN];
  gboolean       is_single_byte_command;
  int            out_len;
  int            in_len;
  int            ep_in;
  unsigned short devices;
  gboolean       cancellable;
};


// Cancellable commands

static const struct elanmoc2_cmd cmd_identify = {
  .cmd = {0xff, 0x03},
  .out_len = 3,
  .in_len = 2,
  .ep_in = ELANMOC2_EP_MOC_CMD_IN,
  .cancellable = true,
};

static const struct elanmoc2_cmd cmd_enroll = {
  .cmd = {0xff, 0x01},
  .out_len = 7,
  .in_len = 2,
  .ep_in = ELANMOC2_EP_MOC_CMD_IN,
  .cancellable = true,
};


// Not cancellable / quick commands

static const struct elanmoc2_cmd cmd_get_fw_ver = {
  .cmd = {0x19},
  .is_single_byte_command = true,
  .out_len = 2,
  .in_len = 2,
  .ep_in = ELANMOC2_EP_CMD_IN,
};

static const struct elanmoc2_cmd cmd_finger_info = {
  .cmd = {0xff, 0x12},
  .out_len = 4,
  .in_len = 64,
  .ep_in = ELANMOC2_EP_CMD_IN,
};

static const struct elanmoc2_cmd cmd_get_enrolled_count = {
  .cmd = {0xff, 0x04},
  .out_len = 3,
  .in_len = 2,
  .ep_in = ELANMOC2_EP_CMD_IN,
};

static const struct elanmoc2_cmd cmd_abort = {
  .cmd = {0xff, 0x02},
  .out_len = 3,
  .in_len = 2,
  .ep_in = ELANMOC2_EP_CMD_IN,
};

static const struct elanmoc2_cmd cmd_commit = {
  .cmd = {0xff, 0x11},
  .out_len = 72,
  .in_len = 2,
  .ep_in = ELANMOC2_EP_CMD_IN,
};

static const struct elanmoc2_cmd cmd_check_enroll_collision = {
  .cmd = {0xff, 0x10},
  .out_len = 3,
  .in_len = 3,
  .ep_in = ELANMOC2_EP_CMD_IN,
};

static const struct elanmoc2_cmd cmd_delete = {
  .cmd = {0xff, 0x13},
  .out_len = 72,
  .in_len = 2,
  .ep_in = ELANMOC2_EP_CMD_IN,
};

static const struct elanmoc2_cmd cmd_wipe_sensor = {
  .cmd = {0xff, 0x99},
  .out_len = 3,
  .in_len = 0,
  .ep_in = ELANMOC2_EP_CMD_IN,
};


enum identify_states {
  IDENTIFY_GET_NUM_ENROLLED,
  IDENTIFY_CHECK_NUM_ENROLLED,
  IDENTIFY_IDENTIFY,
  IDENTIFY_GET_FINGER_INFO,
  IDENTIFY_CHECK_FINGER_INFO,
  IDENTIFY_NUM_STATES
};

enum list_states {
  LIST_GET_NUM_ENROLLED,
  LIST_CHECK_NUM_ENROLLED,
  LIST_GET_FINGER_INFO,
  LIST_CHECK_FINGER_INFO,
  LIST_NUM_STATES
};

enum enroll_states {
  ENROLL_GET_NUM_ENROLLED,
  ENROLL_CHECK_NUM_ENROLLED,
  ENROLL_EARLY_REENROLL_CHECK,
  ENROLL_GET_ENROLLED_FINGER_INFO,
  ENROLL_ATTEMPT_DELETE,
  ENROLL_CHECK_DELETED,
  ENROLL_WIPE_SENSOR,
  ENROLL_ENROLL,
  ENROLL_CHECK_ENROLLED,
  ENROLL_LATE_REENROLL_CHECK,
  ENROLL_COMMIT,
  ENROLL_CHECK_COMMITTED,
  ENROLL_NUM_STATES
};

enum delete_states {
  DELETE_GET_NUM_ENROLLED,
  DELETE_DELETE,
  DELETE_CHECK_DELETED,
  DELETE_NUM_STATES
};

enum clear_storage_states {
  CLEAR_STORAGE_WIPE_SENSOR,
  CLEAR_STORAGE_GET_NUM_ENROLLED,
  CLEAR_STORAGE_CHECK_NUM_ENROLLED,
  CLEAR_STORAGE_NUM_STATES
};

static const FpIdEntry elanmoc2_id_table[] = {
  {.vid = ELANMOC2_VEND_ID, .pid = 0x0c00, .driver_data = ELANMOC2_ALL_DEV},
  {.vid = ELANMOC2_VEND_ID, .pid = 0x0c4c, .driver_data = ELANMOC2_ALL_DEV},
  {.vid = ELANMOC2_VEND_ID, .pid = 0x0c5e, .driver_data = ELANMOC2_DEV_0C5E},
  {.vid = 0, .pid = 0, .driver_data = ELANMOC2_DEV_0C4C}
};
