/*
 * Driver for Egis Technology (LighTuning) Match-On-Chip sensors
 * Originally authored 2023 by Joshua Grisham <josh@joshuagrisham.com>
 *
 * Portions of code and logic inspired from the elanmoc libfprint driver
 * which is copyright (C) 2021 Elan Microelectronics Inc (see elanmoc.c)
 *
 * Based on original reverse-engineering work by Joshua Grisham. The protocol has
 * been reverse-engineered from captures of the official Windows driver, and by
 * testing commands on the sensor with a multiplatform Python prototype driver:
 * https://github.com/joshuagrisham/galaxy-book2-pro-linux/tree/main/fingerprint/
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

G_DECLARE_FINAL_TYPE (FpiDeviceEgisMoc, fpi_device_egismoc, FPI, DEVICE_EGISMOC, FpDevice)

#define EGISMOC_DRIVER_FULLNAME "Egis Technology (LighTuning) Match-on-Chip"

#define EGISMOC_DRIVER_CHECK_PREFIX_TYPE1 (1 << 0)
#define EGISMOC_DRIVER_CHECK_PREFIX_TYPE2 (1 << 1)

#define EGISMOC_EP_CMD_OUT (0x02 | FPI_USB_ENDPOINT_OUT)
#define EGISMOC_EP_CMD_IN (0x81 | FPI_USB_ENDPOINT_IN)
#define EGISMOC_EP_CMD_INTERRUPT_IN 0x83

#define EGISMOC_USB_CONTROL_TIMEOUT 5000
#define EGISMOC_USB_SEND_TIMEOUT 5000
#define EGISMOC_USB_RECV_TIMEOUT 5000
#define EGISMOC_USB_INTERRUPT_TIMEOUT 60000

#define EGISMOC_USB_IN_RECV_LENGTH 4096
#define EGISMOC_USB_INTERRUPT_IN_RECV_LENGTH 64

#define EGISMOC_ENROLL_TIMES 10
#define EGISMOC_MAX_ENROLL_NUM 10
#define EGISMOC_FINGERPRINT_DATA_SIZE 32
#define EGISMOC_LIST_RESPONSE_PREFIX_SIZE 14
#define EGISMOC_LIST_RESPONSE_SUFFIX_SIZE 2

/* standard prefixes for all read/writes */

static guchar egismoc_write_prefix[] = {'E', 'G', 'I', 'S', 0x00, 0x00, 0x00, 0x01};
static gsize egismoc_write_prefix_len = sizeof (egismoc_write_prefix) / sizeof (egismoc_write_prefix[0]);

static guchar egismoc_read_prefix[] = {'S', 'I', 'G', 'E', 0x00, 0x00, 0x00, 0x01};
static gsize egismoc_read_prefix_len = sizeof (egismoc_read_prefix) / sizeof (egismoc_read_prefix[0]);


/* hard-coded command payloads */

static guchar cmd_fw_version[] = {0x00, 0x00, 0x00, 0x07, 0x50, 0x7f, 0x00, 0x00, 0x00, 0x00, 0x0c};
static gsize cmd_fw_version_len = sizeof (cmd_fw_version) / sizeof (cmd_fw_version[0]);
static guchar rsp_fw_version_suffix[] = {0x90, 0x00};
static gsize rsp_fw_version_suffix_len = sizeof (rsp_fw_version_suffix) / sizeof (rsp_fw_version_suffix[0]);

static guchar cmd_list[] = {0x00, 0x00, 0x00, 0x07, 0x50, 0x19, 0x04, 0x00, 0x00, 0x01, 0x40};
static gsize cmd_list_len = sizeof (cmd_list) / sizeof (cmd_list[0]);

static guchar cmd_sensor_reset[] = {0x00, 0x00, 0x00, 0x04, 0x50, 0x1a, 0x00, 0x00};
static gsize cmd_sensor_reset_len = sizeof (cmd_sensor_reset) / sizeof (cmd_sensor_reset[0]);

static guchar cmd_sensor_check[] = {0x00, 0x00, 0x00, 0x04, 0x50, 0x17, 0x02, 0x00};
static gsize cmd_sensor_check_len = sizeof (cmd_sensor_check) / sizeof (cmd_sensor_check[0]);

static guchar cmd_sensor_identify[] = {0x00, 0x00, 0x00, 0x04, 0x50, 0x17, 0x01, 0x01};
static gsize cmd_sensor_identify_len = sizeof (cmd_sensor_identify) / sizeof (cmd_sensor_identify[0]);
static guchar rsp_identify_match_suffix[] = {0x90, 0x00};
static gsize rsp_identify_match_suffix_len = sizeof (rsp_identify_match_suffix) / sizeof (rsp_identify_match_suffix[0]);
static guchar rsp_identify_notmatch_suffix[] = {0x90, 0x04};
static gsize rsp_identify_notmatch_suffix_len = sizeof (rsp_identify_notmatch_suffix) / sizeof (rsp_identify_notmatch_suffix[0]);

static guchar cmd_sensor_enroll[] = {0x00, 0x00, 0x00, 0x04, 0x50, 0x17, 0x01, 0x00};
static gsize cmd_sensor_enroll_len = sizeof (cmd_sensor_enroll) / sizeof (cmd_sensor_enroll[0]);

static guchar cmd_enroll_starting[] = {0x00, 0x00, 0x00, 0x07, 0x50, 0x16, 0x01, 0x00, 0x00, 0x00, 0x20};
static gsize cmd_enroll_starting_len = sizeof (cmd_enroll_starting) / sizeof (cmd_enroll_starting[0]);

static guchar cmd_sensor_start_capture[] = {0x00, 0x00, 0x00, 0x04, 0x50, 0x16, 0x02, 0x01};
static gsize cmd_sensor_start_capture_len = sizeof (cmd_sensor_start_capture) / sizeof (cmd_sensor_start_capture[0]);

static guchar cmd_read_capture[] = {0x00, 0x00, 0x00, 0x07, 0x50, 0x16, 0x02, 0x02, 0x00, 0x00, 0x02};
static gsize cmd_read_capture_len = sizeof (cmd_read_capture) / sizeof (cmd_read_capture[0]);
static guchar rsp_read_success_prefix[] = {0x00, 0x00, 0x00, 0x04};
static gsize rsp_read_success_prefix_len = sizeof (rsp_read_success_prefix) / sizeof (rsp_read_success_prefix[0]);
static guchar rsp_read_success_suffix[] = {0x0a, 0x90, 0x00};
static gsize rsp_read_success_suffix_len = sizeof (rsp_read_success_suffix) / sizeof (rsp_read_success_suffix[0]);
static guchar rsp_read_offcenter_prefix[] = {0x00, 0x00, 0x00, 0x04};
static gsize rsp_read_offcenter_prefix_len = sizeof (rsp_read_offcenter_prefix) / sizeof (rsp_read_offcenter_prefix[0]);
static guchar rsp_read_offcenter_suffix[] = {0x0a, 0x64, 0x91};
static gsize rsp_read_offcenter_suffix_len = sizeof (rsp_read_offcenter_suffix) / sizeof (rsp_read_offcenter_suffix[0]);
static guchar rsp_read_dirty_prefix[] = {0x00, 0x00, 0x00, 0x02, 0x64};
static gsize rsp_read_dirty_prefix_len = sizeof (rsp_read_dirty_prefix) / sizeof (rsp_read_dirty_prefix[0]);

static guchar cmd_commit_starting[] = {0x00, 0x00, 0x00, 0x07, 0x50, 0x16, 0x05, 0x00, 0x00, 0x00, 0x20};
static gsize cmd_commit_starting_len = sizeof (cmd_commit_starting) / sizeof (cmd_commit_starting[0]);


/* commands which exist on the device but are currently not used */
/*
   static guchar cmd_sensor_cancel[] = {0x00, 0x00, 0x00, 0x04, 0x50, 0x16, 0x04, 0x00};
   static gsize cmd_sensor_cancel_len = sizeof(cmd_sensor_cancel) / sizeof(cmd_sensor_cancel[0]);

   static guchar cmd_sensor_verify[] = {0x00, 0x00, 0x00, 0x04, 0x50, 0x04, 0x01, 0x00};
   static gsize cmd_sensor_verify_len = sizeof(cmd_sensor_verify) / sizeof(cmd_sensor_verify[0]);

   static guchar cmd_read_verify[] = {0x00, 0x00, 0x00, 0x04, 0x50, 0x04, 0x02, 0x00};
   static gsize cmd_read_verify_len = sizeof(cmd_read_verify) / sizeof(cmd_read_verify[0]);
 */


/* prefixes/suffixes and other things for dynamically created command payloads */

#define EGISMOC_CHECK_BYTES_LENGTH 2
#define EGISMOC_IDENTIFY_RESPONSE_PRINT_ID_OFFSET 46
#define EGISMOC_CMD_CHECK_SEPARATOR_LENGTH 32

static guchar cmd_new_print_prefix[] = {0x00, 0x00, 0x00, 0x27, 0x50, 0x16, 0x03, 0x00, 0x00, 0x00, 0x20};
static gsize cmd_new_print_prefix_len = sizeof (cmd_new_print_prefix) / sizeof (cmd_new_print_prefix[0]);

static guchar cmd_delete_prefix[] = {0x50, 0x18, 0x04, 0x00, 0x00};
static gsize cmd_delete_prefix_len = sizeof (cmd_delete_prefix) / sizeof (cmd_delete_prefix[0]);
static guchar rsp_delete_success_prefix[] = {0x00, 0x00, 0x00, 0x02, 0x90, 0x00};
static gsize rsp_delete_success_prefix_len = sizeof (rsp_delete_success_prefix) / sizeof (rsp_delete_success_prefix[0]);

static guchar cmd_check_prefix_type1[] = {0x50, 0x17, 0x03, 0x00, 0x00};
static gsize cmd_check_prefix_type1_len = sizeof (cmd_check_prefix_type1) / sizeof (cmd_check_prefix_type1[0]);
static guchar cmd_check_prefix_type2[] = {0x50, 0x17, 0x03, 0x80, 0x00};
static gsize cmd_check_prefix_type2_len = sizeof (cmd_check_prefix_type2) / sizeof (cmd_check_prefix_type2[0]);
static guchar cmd_check_suffix[] = {0x00, 0x40};
static gsize cmd_check_suffix_len = sizeof (cmd_check_suffix) / sizeof (cmd_check_suffix[0]);
static guchar rsp_check_not_yet_enrolled_suffix[] = {0x90, 0x04};
static gsize rsp_check_not_yet_enrolled_suffix_len = sizeof (rsp_check_not_yet_enrolled_suffix) / sizeof (rsp_check_not_yet_enrolled_suffix[0]);


/* SSM task states and various status enums */

typedef enum {
  CMD_SEND,
  CMD_GET,
  CMD_STATES,
} CommandStates;

typedef enum {
  DEV_INIT_CONTROL1,
  DEV_INIT_CONTROL2,
  DEV_INIT_CONTROL3,
  DEV_INIT_CONTROL4,
  DEV_INIT_CONTROL5,
  DEV_GET_FW_VERSION,
  DEV_INIT_STATES,
} DeviceInitStates;

typedef enum {
  IDENTIFY_GET_ENROLLED_IDS,
  IDENTIFY_CHECK_ENROLLED_NUM,
  IDENTIFY_SENSOR_RESET,
  IDENTIFY_SENSOR_IDENTIFY,
  IDENTIFY_WAIT_FINGER,
  IDENTIFY_SENSOR_CHECK,
  IDENTIFY_CHECK,
  IDENTIFY_COMPLETE_SENSOR_RESET,
  IDENTIFY_COMPLETE,
  IDENTIFY_STATES,
} IdentifyStates;

typedef enum {
  ENROLL_GET_ENROLLED_IDS,
  ENROLL_CHECK_ENROLLED_NUM,
  ENROLL_SENSOR_RESET,
  ENROLL_SENSOR_ENROLL,
  ENROLL_WAIT_FINGER,
  ENROLL_SENSOR_CHECK,
  ENROLL_CHECK,
  ENROLL_START,
  ENROLL_CAPTURE_SENSOR_RESET,
  ENROLL_CAPTURE_SENSOR_START_CAPTURE,
  ENROLL_CAPTURE_WAIT_FINGER,
  ENROLL_CAPTURE_READ_RESPONSE,
  ENROLL_COMMIT_START,
  ENROLL_COMMIT,
  ENROLL_COMMIT_SENSOR_RESET,
  ENROLL_COMPLETE,
  ENROLL_STATES,
} EnrollStates;

typedef enum {
  ENROLL_STATUS_DEVICE_FULL,
  ENROLL_STATUS_DUPLICATE,
  ENROLL_STATUS_PARTIAL_OK,
  ENROLL_STATUS_RETRY,
  ENROLL_STATUS_COMPLETE,
} EnrollStatus;

typedef enum {
  LIST_GET_ENROLLED_IDS,
  LIST_RETURN_ENROLLED_PRINTS,
  LIST_STATES,
} ListStates;

typedef enum {
  DELETE_GET_ENROLLED_IDS,
  DELETE_DELETE,
  DELETE_STATES,
} DeleteStates;
