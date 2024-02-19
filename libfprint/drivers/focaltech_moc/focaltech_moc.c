/*
 * Copyright (C) 2022 FocalTech Electronics Inc
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

#include "focaltech_moc.h"

#include <ctype.h>

#define FP_COMPONENT "focaltech_moc"

#include "drivers_api.h"

G_DEFINE_TYPE (FpiDeviceFocaltechMoc, fpi_device_focaltech_moc, FP_TYPE_DEVICE)

static const FpIdEntry id_table[] = {
  { .vid = 0x2808,  .pid = 0x9e48,  },
  { .vid = 0x2808,  .pid = 0xd979,  },
  { .vid = 0x2808,  .pid = 0xa959,  },
  { .vid = 0,  .pid = 0,  .driver_data = 0 },   /* terminating entry */
};

typedef void (*SynCmdMsgCallback) (FpiDeviceFocaltechMoc *self,
                                   uint8_t               *buffer_in,
                                   gsize                  length_in,
                                   GError                *error);

typedef struct
{
  SynCmdMsgCallback callback;
} CommandData;

typedef struct
{
  uint8_t h;
  uint8_t l;
} FpCmdLen;

typedef struct
{
  uint8_t  magic;
  FpCmdLen len;
} FpCmdHeader;

typedef struct
{
  FpCmdHeader header;
  uint8_t     code;
  uint8_t     payload[0];
} FpCmd;

typedef struct
{
#if (G_BYTE_ORDER == G_LITTLE_ENDIAN)
  uint8_t b0;
  uint8_t b1;
#else
  uint8_t b1;
  uint8_t b0;
#endif
} u16_bytes_t;

typedef union
{
  u16_bytes_t s;
  uint16_t    v;
} u_u16_bytes_t;

static inline uint16_t
get_u16_from_u8_lh (uint8_t l, uint8_t h)
{
  u_u16_bytes_t u_u16_bytes;

  u_u16_bytes.v = 0;
  u_u16_bytes.s.b0 = l;
  u_u16_bytes.s.b1 = h;

  return u_u16_bytes.v;
}

static inline uint8_t
get_u8_l_from_u16 (uint16_t v)
{
  u_u16_bytes_t u_u16_bytes;

  u_u16_bytes.v = v;

  return u_u16_bytes.s.b0;
}

static inline uint8_t
get_u8_h_from_u16 (uint16_t v)
{
  u_u16_bytes_t u_u16_bytes;

  u_u16_bytes.v = v;

  return u_u16_bytes.s.b1;
}

static uint8_t
fp_cmd_bcc (uint8_t *data, uint16_t len)
{
  int i;
  uint8_t bcc = 0;

  for (i = 0; i < len; i++)
    bcc ^= data[i];

  return bcc;
}

static uint8_t *
focaltech_moc_compose_cmd (uint8_t cmd, const uint8_t *data, uint16_t len)
{
  g_autofree uint8_t *cmd_buf = NULL;
  FpCmd *fp_cmd = NULL;
  uint8_t *bcc = NULL;
  uint16_t header_len = len + sizeof (*bcc);

  cmd_buf = g_new0 (uint8_t, sizeof (FpCmd) + header_len);

  fp_cmd = (FpCmd *) cmd_buf;

  fp_cmd->header.magic = 0x02;
  fp_cmd->header.len.l = get_u8_l_from_u16 (header_len);
  fp_cmd->header.len.h = get_u8_h_from_u16 (header_len);
  fp_cmd->code = cmd;

  if (data != NULL)
    memcpy (fp_cmd->payload, data, len);

  bcc = fp_cmd->payload + len;
  *bcc = fp_cmd_bcc ((uint8_t *) &fp_cmd->header.len, bcc - (uint8_t *) &fp_cmd->header.len);

  return g_steal_pointer (&cmd_buf);
}

static int
focaltech_moc_check_cmd (uint8_t *response_buf, uint16_t len)
{
  int ret = -1;
  FpCmd *fp_cmd = NULL;
  uint8_t *bcc = NULL;
  uint16_t header_len;
  uint16_t data_len;

  fp_cmd = (FpCmd *) response_buf;

  if (len < sizeof (FpCmd) + sizeof (*bcc))
    return ret;

  if (fp_cmd->header.magic != 0x02)
    return ret;

  header_len = get_u16_from_u8_lh (fp_cmd->header.len.l, fp_cmd->header.len.h);

  if (header_len < sizeof (*bcc))
    return ret;

  if ((sizeof (FpCmd) + header_len) > len)
    return ret;

  data_len = header_len - sizeof (*bcc);

  bcc = fp_cmd->payload + data_len;

  if (fp_cmd_bcc ((uint8_t *) &fp_cmd->header.len,
                  bcc - (uint8_t *) &fp_cmd->header.len) != *bcc)
    return ret;

  ret = 0;
  return ret;
}

static void
fp_cmd_receive_cb (FpiUsbTransfer *transfer,
                   FpDevice       *device,
                   gpointer        userdata,
                   GError         *error)
{
  FpiDeviceFocaltechMoc *self = FPI_DEVICE_FOCALTECH_MOC (device);
  CommandData *data = userdata;
  int ssm_state = 0;

  if (error)
    {
      fpi_ssm_mark_failed (transfer->ssm, g_steal_pointer (&error));
      return;
    }

  if (data == NULL)
    {
      fpi_ssm_mark_failed (transfer->ssm,
                           fpi_device_error_new (FP_DEVICE_ERROR_GENERAL));
      return;
    }

  ssm_state = fpi_ssm_get_cur_state (transfer->ssm);

  /* skip zero length package */
  if (transfer->actual_length == 0)
    {
      fpi_ssm_jump_to_state (transfer->ssm, ssm_state);
      return;
    }

  if (focaltech_moc_check_cmd (transfer->buffer, transfer->actual_length) != 0)
    {
      fpi_ssm_mark_failed (transfer->ssm,
                           fpi_device_error_new (FP_DEVICE_ERROR_GENERAL));
      return;
    }

  if (data->callback)
    data->callback (self, transfer->buffer, transfer->actual_length, NULL);

  fpi_ssm_mark_completed (transfer->ssm);
}

typedef enum {
  FP_CMD_SEND = 0,
  FP_CMD_GET,
  FP_CMD_NUM_STATES,
} FpCmdState;

static void
fp_cmd_run_state (FpiSsm   *ssm,
                  FpDevice *device)
{
  FpiUsbTransfer *transfer;
  FpiDeviceFocaltechMoc *self = FPI_DEVICE_FOCALTECH_MOC (device);

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case FP_CMD_SEND:
      if (self->cmd_transfer)
        {
          self->cmd_transfer->ssm = ssm;
          fpi_usb_transfer_submit (g_steal_pointer (&self->cmd_transfer),
                                   FOCALTECH_MOC_CMD_TIMEOUT,
                                   NULL,
                                   fpi_ssm_usb_transfer_cb,
                                   NULL);
        }
      else
        {
          fpi_ssm_next_state (ssm);
        }

      break;

    case FP_CMD_GET:
      if (self->cmd_len_in == 0)
        {
          CommandData *data = fpi_ssm_get_data (ssm);

          if (data->callback)
            data->callback (self, NULL, 0, 0);

          fpi_ssm_mark_completed (ssm);
          return;
        }

      transfer = fpi_usb_transfer_new (device);
      transfer->ssm = ssm;
      fpi_usb_transfer_fill_bulk (transfer, self->bulk_in_ep, self->cmd_len_in);
      fpi_usb_transfer_submit (transfer,
                               self->cmd_cancelable ? 0 : FOCALTECH_MOC_CMD_TIMEOUT,
                               self->cmd_cancelable ? fpi_device_get_cancellable (device) : NULL,
                               fp_cmd_receive_cb,
                               fpi_ssm_get_data (ssm));
      break;

    }

}

static void
fp_cmd_ssm_done (FpiSsm *ssm, FpDevice *device, GError *error)
{
  g_autoptr(GError) local_error = error;
  FpiDeviceFocaltechMoc *self = FPI_DEVICE_FOCALTECH_MOC (device);
  CommandData *data = fpi_ssm_get_data (ssm);

  self->cmd_ssm = NULL;

  if (local_error && data->callback)
    data->callback (self, NULL, 0, g_steal_pointer (&local_error));
}

static void
fp_cmd_ssm_done_data_free (CommandData *data)
{
  g_free (data);
}

static void
focaltech_moc_get_cmd (FpDevice *device, guint8 *buffer_out,
                       gsize length_out, gsize length_in,
                       gboolean can_be_cancelled,
                       SynCmdMsgCallback callback)
{
  FpiDeviceFocaltechMoc *self = FPI_DEVICE_FOCALTECH_MOC (device);

  g_autoptr(FpiUsbTransfer) transfer = NULL;
  CommandData *data = g_new0 (CommandData, 1);

  transfer = fpi_usb_transfer_new (device);
  transfer->short_is_error = TRUE;
  fpi_usb_transfer_fill_bulk_full (transfer, self->bulk_out_ep, buffer_out,
                                   length_out, g_free);
  data->callback = callback;

  self->cmd_transfer = g_steal_pointer (&transfer);
  self->cmd_len_in = length_in + 1;
  self->cmd_cancelable = can_be_cancelled;

  self->cmd_ssm = fpi_ssm_new (FP_DEVICE (self),
                               fp_cmd_run_state,
                               FP_CMD_NUM_STATES);

  fpi_ssm_set_data (self->cmd_ssm, data, (GDestroyNotify) fp_cmd_ssm_done_data_free);

  fpi_ssm_start (self->cmd_ssm, fp_cmd_ssm_done);
}

struct UserId
{
  uint8_t uid[32];
};

static void
fprint_set_uid (FpPrint *print, uint8_t *uid, size_t size)
{
  GVariant *var_uid;
  GVariant *var_data;

  var_uid = g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, uid, size, 1);
  var_data = g_variant_new ("(@ay)", var_uid);
  fpi_print_set_type (print, FPI_PRINT_RAW);
  fpi_print_set_device_stored (print, TRUE);
  g_object_set (print, "fpi-data", var_data, NULL);
}

enum enroll_states {
  ENROLL_RSP_RETRY,
  ENROLL_RSP_ENROLL_REPORT,
  ENROLL_RSP_ENROLL_OK,
  ENROLL_RSP_ENROLL_CANCEL_REPORT,
};

static void
enroll_status_report (FpiDeviceFocaltechMoc *self, int enroll_status_id,
                      int data, GError *error)
{
  FpDevice *device = FP_DEVICE (self);

  switch (enroll_status_id)
    {
    case ENROLL_RSP_RETRY:
      {
        fpi_device_enroll_progress (device, self->num_frames, NULL,
                                    fpi_device_retry_new (FP_DEVICE_RETRY_CENTER_FINGER));
        break;
      }

    case ENROLL_RSP_ENROLL_REPORT:
      {
        fpi_device_enroll_progress (device, self->num_frames, NULL, NULL);
        break;
      }

    case ENROLL_RSP_ENROLL_OK:
      {
        FpPrint *print = NULL;
        fp_info ("Enrollment was successful!");
        fpi_device_get_enroll_data (device, &print);
        fpi_device_enroll_complete (device, g_object_ref (print), NULL);
        break;
      }

    case ENROLL_RSP_ENROLL_CANCEL_REPORT:
      {
        fpi_device_enroll_complete (device, NULL,
                                    fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                              "Enrollment failed (%d) (ENROLL_RSP_ENROLL_CANCEL_REPORT)",
                                                              data));
      }
    }
}

static void
task_ssm_done (FpiSsm *ssm, FpDevice *device, GError *error)
{
  FpiDeviceFocaltechMoc *self = FPI_DEVICE_FOCALTECH_MOC (device);

  self->num_frames = 0;
  self->task_ssm = NULL;

  if (error)
    fpi_device_action_error (device, g_steal_pointer (&error));
}

static const char *
get_g_usb_device_direction_des (GUsbDeviceDirection dir)
{
  switch (dir)
    {
    case G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST:
      return "G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST";

    case G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE:
      return "G_USB_DEVICE_DIRECTION_HOST_TO_DEVICE";

    default:
      return "unknown";
    }
}

static int
usb_claim_interface_probe (FpDevice *device, int claim, GError **error)
{
  g_autoptr(GPtrArray) interfaces = NULL;
  FpiDeviceFocaltechMoc *self = FPI_DEVICE_FOCALTECH_MOC (device);
  int ret = -1;
  int i;

  interfaces = g_usb_device_get_interfaces (fpi_device_get_usb_device (device), error);

  for (i = 0; i < interfaces->len; i++)
    {
      GUsbInterface *cur_iface = g_ptr_array_index (interfaces, i);
      g_autoptr(GPtrArray) endpoints = g_usb_interface_get_endpoints (cur_iface);

      fp_dbg ("class:%x, subclass:%x, protocol:%x",
              g_usb_interface_get_class (cur_iface),
              g_usb_interface_get_subclass (cur_iface),
              g_usb_interface_get_protocol (cur_iface));

      if (claim == 1)
        {
          int j;

          for (j = 0; j < endpoints->len; j++)
            {
              GUsbEndpoint *endpoint = g_ptr_array_index (endpoints, j);
              GBytes *bytes = g_usb_endpoint_get_extra (endpoint);

              fp_dbg ("bytes size:%ld", g_bytes_get_size (bytes));

              fp_dbg ("kind:%x, max packet size:%d, poll interval:%d, refresh:%x, "
                      "sync address:%x, address:%x, number:%d, direction:%s",
                      g_usb_endpoint_get_kind (endpoint),
                      g_usb_endpoint_get_maximum_packet_size (endpoint),
                      g_usb_endpoint_get_polling_interval (endpoint),
                      g_usb_endpoint_get_refresh (endpoint),
                      g_usb_endpoint_get_synch_address (endpoint),
                      g_usb_endpoint_get_address (endpoint),
                      g_usb_endpoint_get_number (endpoint),
                      get_g_usb_device_direction_des (g_usb_endpoint_get_direction (endpoint)));

              if (g_usb_endpoint_get_direction (endpoint) == G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST)
                self->bulk_in_ep = g_usb_endpoint_get_address (endpoint);
              else
                self->bulk_out_ep = g_usb_endpoint_get_address (endpoint);
            }

          if (!g_usb_device_claim_interface (fpi_device_get_usb_device (device),
                                             g_usb_interface_get_number (cur_iface),
                                             0, error))
            return ret;
        }
      else if (!g_usb_device_release_interface (fpi_device_get_usb_device (device),
                                                g_usb_interface_get_number (cur_iface),
                                                0, error))
        {
          return ret;
        }


    }

  ret = 0;

  return ret;
}

static void
task_ssm_init_done (FpiSsm *ssm, FpDevice *device, GError *error)
{
  FpiDeviceFocaltechMoc *self = FPI_DEVICE_FOCALTECH_MOC (device);

  if (error)
    usb_claim_interface_probe (device, 0, &error);

  fpi_device_open_complete (FP_DEVICE (self), g_steal_pointer (&error));
}

struct EnrollTimes
{
  uint8_t enroll_times;
};

static void
focaltech_moc_get_enroll_times (FpiDeviceFocaltechMoc *self,
                                uint8_t               *buffer_in,
                                gsize                  length_in,
                                GError                *error)
{
  FpCmd *fp_cmd = NULL;
  struct EnrollTimes *enroll_times = NULL;

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }

  fp_cmd = (FpCmd *) buffer_in;
  enroll_times = (struct EnrollTimes *) (fp_cmd + 1);

  if (fp_cmd->code != 0x04)
    {
      fpi_ssm_mark_failed (self->task_ssm,
                           fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                     "Can't get response!!"));
    }
  else
    {
      fp_dbg ("focaltechmoc enroll_times: %d", enroll_times->enroll_times + 1);
      fpi_device_set_nr_enroll_stages (FP_DEVICE (self), enroll_times->enroll_times + 1);
      fpi_ssm_next_state (self->task_ssm);
    }
}

static void
focaltech_moc_release_finger (FpiDeviceFocaltechMoc *self,
                              uint8_t               *buffer_in,
                              gsize                  length_in,
                              GError                *error)
{
  FpCmd *fp_cmd = NULL;

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }

  fp_cmd = (FpCmd *) buffer_in;

  if (fp_cmd->code != 0x04)
    {
      fpi_ssm_mark_failed (self->task_ssm,
                           fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                     "Can't get response!!"));
    }
  else
    {
      fpi_ssm_next_state (self->task_ssm);
    }
}

enum dev_init_states {
  DEV_INIT_GET_ENROLL_TIMES,
  DEV_INIT_RELEASE_FINGER,
  DEV_INIT_STATES,
};

static void
dev_init_handler (FpiSsm *ssm, FpDevice *device)
{
  guint8 *cmd_buf = NULL;
  uint16_t cmd_len = 0;
  uint16_t resp_len = 0;

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case DEV_INIT_GET_ENROLL_TIMES:
      cmd_len = 0;
      resp_len = sizeof (struct EnrollTimes);
      cmd_buf = focaltech_moc_compose_cmd (0xa5, NULL, cmd_len);
      focaltech_moc_get_cmd (device, cmd_buf,
                             sizeof (FpCmd) + cmd_len + sizeof (uint8_t),
                             sizeof (FpCmd) + resp_len + sizeof (uint8_t),
                             1,
                             focaltech_moc_get_enroll_times);
      break;

    case DEV_INIT_RELEASE_FINGER:
      {
        uint8_t d1 = 0x78;
        cmd_len = sizeof (d1);
        resp_len = 0;
        cmd_buf = focaltech_moc_compose_cmd (0x82, &d1, cmd_len);
        focaltech_moc_get_cmd (device, cmd_buf,
                               sizeof (FpCmd) + cmd_len + sizeof (uint8_t),
                               sizeof (FpCmd) + resp_len + sizeof (uint8_t),
                               1,
                               focaltech_moc_release_finger);
        break;
      }
    }
}

static void
focaltech_moc_open (FpDevice *device)
{
  FpiDeviceFocaltechMoc *self = FPI_DEVICE_FOCALTECH_MOC (device);
  GError *error = NULL;

  if (!g_usb_device_reset (fpi_device_get_usb_device (device), &error))
    {
      fpi_device_open_complete (FP_DEVICE (self), g_steal_pointer (&error));
      return;
    }

  if (usb_claim_interface_probe (device, 1, &error) != 0)
    {
      fpi_device_open_complete (FP_DEVICE (self), g_steal_pointer (&error));
      return;
    }

  self->task_ssm = fpi_ssm_new (FP_DEVICE (self), dev_init_handler, DEV_INIT_STATES);
  fpi_ssm_start (self->task_ssm, task_ssm_init_done);
}

static void
task_ssm_exit_done (FpiSsm *ssm, FpDevice *device, GError *error)
{
  FpiDeviceFocaltechMoc *self = FPI_DEVICE_FOCALTECH_MOC (device);

  if (!error)
    {
      GError *local_error = NULL;

      if (usb_claim_interface_probe (device, 0, &local_error) < 0)
        g_propagate_error (&error, g_steal_pointer (&local_error));
    }

  fpi_device_close_complete (FP_DEVICE (self), error);
  self->task_ssm = NULL;
}

enum dev_exit_states {
  DEV_EXIT_START,
  DEV_EXIT_STATES,
};

static void
dev_exit_handler (FpiSsm *ssm, FpDevice *device)
{
  FpiDeviceFocaltechMoc *self = FPI_DEVICE_FOCALTECH_MOC (device);

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case DEV_EXIT_START:
      fpi_ssm_next_state (self->task_ssm);
      break;

    default:
      g_assert_not_reached ();
    }
}

static void
focaltech_moc_close (FpDevice *device)
{
  FpiDeviceFocaltechMoc *self = FPI_DEVICE_FOCALTECH_MOC (device);

  fp_info ("Focaltechmoc dev_exit");
  self->task_ssm = fpi_ssm_new (FP_DEVICE (self), dev_exit_handler, DEV_EXIT_STATES);
  fpi_ssm_start (self->task_ssm, task_ssm_exit_done);
}

enum identify_states {
  MOC_IDENTIFY_RELEASE_FINGER,
  MOC_IDENTIFY_WAIT_FINGER,
  MOC_IDENTIFY_WAIT_FINGER_DELAY,
  MOC_IDENTIFY_CAPTURE,
  MOC_IDENTIFY_MATCH,
  MOC_IDENTIFY_NUM_STATES,
};

static void
focaltech_moc_identify_wait_finger_cb (FpiDeviceFocaltechMoc *self,
                                       uint8_t               *buffer_in,
                                       gsize                  length_in,
                                       GError                *error)
{
  FpCmd *fp_cmd = NULL;
  uint8_t *finger_status = NULL;

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }

  fp_cmd = (FpCmd *) buffer_in;
  finger_status = (uint8_t *) (fp_cmd + 1);

  if (fp_cmd->code != 0x04)
    {
      fpi_ssm_mark_failed (self->task_ssm,
                           fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                     "Can't get response!!"));
    }
  else
    {

      if (*finger_status == 0x01)
        fpi_ssm_jump_to_state (self->task_ssm, MOC_IDENTIFY_CAPTURE);
      else
        fpi_ssm_jump_to_state (self->task_ssm, MOC_IDENTIFY_WAIT_FINGER_DELAY);
    }
}

static void
focaltech_moc_identify_wait_finger_delay (FpDevice *device,
                                          void     *user_data)
{
  FpiDeviceFocaltechMoc *self = FPI_DEVICE_FOCALTECH_MOC (device);

  fpi_ssm_jump_to_state (self->task_ssm, MOC_IDENTIFY_WAIT_FINGER);
}

enum FprintError {
  ERROR_NONE,
  ERROR_QUALITY,
  ERROR_SHORT,
  ERROR_LEFT,
  ERROR_RIGHT,
  ERROR_NONFINGER,
  ERROR_NOMOVE,
  ERROR_OTHER,
};

struct CaptureResult
{
  uint8_t error;
  uint8_t remain;
};

static void
focaltech_moc_identify_capture_cb (FpiDeviceFocaltechMoc *self,
                                   uint8_t               *buffer_in,
                                   gsize                  length_in,
                                   GError                *error)
{
  FpCmd *fp_cmd = NULL;
  struct CaptureResult *capture_result = NULL;

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }

  fp_cmd = (FpCmd *) buffer_in;
  capture_result = (struct CaptureResult *) (fp_cmd + 1);

  if (fp_cmd->code != 0x04)
    {
      fpi_ssm_mark_failed (self->task_ssm,
                           fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                     "Can't get response!!"));
    }
  else
    {
      if (capture_result->error == ERROR_NONE)
        {
          fpi_ssm_next_state (self->task_ssm);
        }
      else
        {
          if (fpi_device_get_current_action (FP_DEVICE (self)) == FPI_DEVICE_ACTION_VERIFY)
            {
              fpi_device_verify_report (FP_DEVICE (self), FPI_MATCH_ERROR, NULL, error);
              fpi_device_verify_complete (FP_DEVICE (self), NULL);
            }
          else
            {
              fpi_device_identify_report (FP_DEVICE (self), NULL, NULL, error);
              fpi_device_identify_complete (FP_DEVICE (self), NULL);
            }

          fpi_ssm_mark_failed (self->task_ssm, fpi_device_retry_new (FP_DEVICE_RETRY_GENERAL));
        }
    }
}

static void
identify_status_report (FpiDeviceFocaltechMoc *self, FpPrint *print, GError *error)
{
  FpDevice *device = FP_DEVICE (self);

  if (print == NULL)
    {
      if (fpi_device_get_current_action (device) == FPI_DEVICE_ACTION_IDENTIFY)
        {
          fpi_device_identify_report (device, NULL, NULL, NULL);
          fpi_device_identify_complete (device, NULL);
        }
      else
        {
          fpi_device_verify_report (device, FPI_MATCH_FAIL, NULL, NULL);
          fpi_device_verify_complete (device, NULL);
        }
    }
  else
    {
      if (fpi_device_get_current_action (device) == FPI_DEVICE_ACTION_IDENTIFY)
        {
          GPtrArray *prints;
          gboolean found = FALSE;
          guint index;

          fpi_device_get_identify_data (device, &prints);
          found = g_ptr_array_find_with_equal_func (prints,
                                                    print,
                                                    (GEqualFunc) fp_print_equal,
                                                    &index);

          if (found)
            fpi_device_identify_report (device, g_ptr_array_index (prints, index), print, NULL);
          else
            fpi_device_identify_report (device, NULL, print, NULL);

          fpi_device_identify_complete (device, NULL);
        }
      else
        {
          FpPrint *verify_print = NULL;
          fpi_device_get_verify_data (device, &verify_print);

          if (fp_print_equal (verify_print, print))
            fpi_device_verify_report (device, FPI_MATCH_SUCCESS, print, NULL);
          else
            fpi_device_verify_report (device, FPI_MATCH_FAIL, print, NULL);

          fpi_device_verify_complete (device, NULL);
        }
    }
}

static void
focaltech_moc_identify_match_cb (FpiDeviceFocaltechMoc *self,
                                 uint8_t               *buffer_in,
                                 gsize                  length_in,
                                 GError                *error)
{
  FpCmd *fp_cmd = NULL;
  struct UserId *user_id = NULL;
  FpPrint *print = NULL;

  fp_cmd = (FpCmd *) buffer_in;
  user_id = (struct UserId *) (fp_cmd + 1);

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }

  if (fp_cmd->code == 0x04)
    {
      print = fp_print_new (FP_DEVICE (self));
      fprint_set_uid (print, user_id->uid, sizeof (user_id->uid));
    }

  identify_status_report (self, print, error);

  fpi_ssm_next_state (self->task_ssm);
}

static void
focaltech_identify_run_state (FpiSsm *ssm, FpDevice *device)
{
  guint8 *cmd_buf = NULL;
  uint16_t cmd_len = 0;
  uint16_t resp_len = 0;

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case MOC_IDENTIFY_RELEASE_FINGER:
      {
        uint8_t d1 = 0x78;
        cmd_len = sizeof (d1);
        resp_len = 0;
        cmd_buf = focaltech_moc_compose_cmd (0x82, &d1, cmd_len);
        focaltech_moc_get_cmd (device, cmd_buf,
                               sizeof (FpCmd) + cmd_len + sizeof (uint8_t),
                               sizeof (FpCmd) + resp_len + sizeof (uint8_t),
                               1,
                               focaltech_moc_release_finger);
        break;
      }

    case MOC_IDENTIFY_WAIT_FINGER:
      {
        uint8_t data = 0x02;
        cmd_len = sizeof (uint8_t);
        resp_len = sizeof (uint8_t);
        cmd_buf = focaltech_moc_compose_cmd (0x80, &data, cmd_len);
        focaltech_moc_get_cmd (device, cmd_buf,
                               sizeof (FpCmd) + cmd_len + sizeof (uint8_t),
                               sizeof (FpCmd) + resp_len + sizeof (uint8_t),
                               1,
                               focaltech_moc_identify_wait_finger_cb);
        break;
      }

    case MOC_IDENTIFY_WAIT_FINGER_DELAY:
      fpi_device_add_timeout (device, 50,
                              focaltech_moc_identify_wait_finger_delay,
                              NULL, NULL);
      break;

    case MOC_IDENTIFY_CAPTURE:
      cmd_len = 0;
      resp_len = sizeof (struct CaptureResult);
      cmd_buf = focaltech_moc_compose_cmd (0xa6, NULL, cmd_len);
      focaltech_moc_get_cmd (device, cmd_buf,
                             sizeof (FpCmd) + cmd_len + sizeof (uint8_t),
                             sizeof (FpCmd) + resp_len + sizeof (uint8_t),
                             1,
                             focaltech_moc_identify_capture_cb);
      break;

    case MOC_IDENTIFY_MATCH:
      cmd_len = 0;
      resp_len = sizeof (struct UserId);
      cmd_buf = focaltech_moc_compose_cmd (0xaa, NULL, cmd_len);
      focaltech_moc_get_cmd (device, cmd_buf,
                             sizeof (FpCmd) + cmd_len + sizeof (uint8_t),
                             sizeof (FpCmd) + resp_len + sizeof (uint8_t),
                             1,
                             focaltech_moc_identify_match_cb);
      break;
    }
}

static void
focaltech_moc_identify (FpDevice *device)
{
  FpiDeviceFocaltechMoc *self = FPI_DEVICE_FOCALTECH_MOC (device);

  self->task_ssm = fpi_ssm_new (device,
                                focaltech_identify_run_state,
                                MOC_IDENTIFY_NUM_STATES);
  fpi_ssm_start (self->task_ssm, task_ssm_done);
}

enum moc_enroll_states {
  MOC_ENROLL_GET_ENROLLED_INFO,
  MOC_ENROLL_GET_ENROLLED_LIST,
  MOC_ENROLL_RELEASE_FINGER,
  MOC_ENROLL_START_ENROLL,
  MOC_ENROLL_WAIT_FINGER,
  MOC_ENROLL_WAIT_FINGER_DELAY,
  MOC_ENROLL_ENROLL_CAPTURE,
  MOC_ENROLL_SET_ENROLLED_INFO,
  MOC_ENROLL_COMMIT_RESULT,
  MOC_ENROLL_NUM_STATES,
};

struct EnrolledInfoItem
{
  uint8_t uid[FOCALTECH_MOC_UID_PREFIX_LENGTH];
  uint8_t user_id[FOCALTECH_MOC_USER_ID_LENGTH];
};

struct UserDes
{
  uint8_t finger;
  char    username[FOCALTECH_MOC_USER_ID_LENGTH];
};

struct EnrolledInfo
{
  uint8_t                 actived[FOCALTECH_MOC_MAX_FINGERS];
  struct EnrolledInfoItem items[FOCALTECH_MOC_MAX_FINGERS];
  struct UserId           user_id[FOCALTECH_MOC_MAX_FINGERS];
  struct UserDes          user_des[FOCALTECH_MOC_MAX_FINGERS];
};

typedef struct
{
  GPtrArray           *list_result;
  struct EnrolledInfo *enrolled_info;
} FpActionData;

struct EnrolledInfoSetData
{
  uint8_t                 data;
  struct EnrolledInfoItem items[FOCALTECH_MOC_MAX_FINGERS];
};

static void
fp_action_ssm_done_data_free (FpActionData *data)
{
  g_clear_pointer (&data->list_result, g_ptr_array_unref);
  g_clear_pointer (&data->enrolled_info, g_free);
  g_free (data);
}

static void
focaltech_moc_get_enrolled_info_cb (FpiDeviceFocaltechMoc *self,
                                    uint8_t               *buffer_in,
                                    gsize                  length_in,
                                    GError                *error)
{
  FpCmd *fp_cmd = NULL;
  struct EnrolledInfoItem *items = NULL;
  FpActionData *data = fpi_ssm_get_data (self->task_ssm);

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }

  fp_cmd = (FpCmd *) buffer_in;
  items = (struct EnrolledInfoItem *) (fp_cmd + 1);

  if (fp_cmd->code != 0x04)
    {
      fpi_ssm_mark_failed (self->task_ssm,
                           fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                     "Can't get response!!"));
    }
  else
    {
      memcpy (&data->enrolled_info->items[0], items,
              FOCALTECH_MOC_MAX_FINGERS * sizeof (struct EnrolledInfoItem));
      fpi_ssm_next_state (self->task_ssm);
    }
}

struct UidList
{
  uint8_t       actived[FOCALTECH_MOC_MAX_FINGERS];
  struct UserId uid[FOCALTECH_MOC_MAX_FINGERS];
};

static int
focaltech_moc_get_enrolled_info_item (FpiDeviceFocaltechMoc *self,
                                      uint8_t *uid,
                                      struct EnrolledInfoItem **pitem, int *index)
{
  FpActionData *data = fpi_ssm_get_data (self->task_ssm);
  int ret = -1;
  int i;

  for (i = 0; i < FOCALTECH_MOC_MAX_FINGERS; i++)
    {
      struct EnrolledInfoItem *item = &data->enrolled_info->items[i];

      if (memcmp (item->uid, uid, FOCALTECH_MOC_UID_PREFIX_LENGTH) == 0)
        {
          data->enrolled_info->actived[i] = 1;
          *pitem = item;
          *index = i;
          ret = 0;
          break;
        }
    }

  return ret;
}

static void
focaltech_moc_get_enrolled_list_cb (FpiDeviceFocaltechMoc *self,
                                    uint8_t               *buffer_in,
                                    gsize                  length_in,
                                    GError                *error)
{
  FpCmd *fp_cmd = NULL;
  struct UidList *uid_list = NULL;

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }

  fp_cmd = (FpCmd *) buffer_in;
  uid_list = (struct UidList *) (fp_cmd + 1);

  if (fp_cmd->code != 0x04)
    {
      fpi_ssm_mark_failed (self->task_ssm,
                           fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                     "Can't get response!!"));
    }
  else
    {
      FpActionData *data = fpi_ssm_get_data (self->task_ssm);
      int i;

      for (i = 0; i < FOCALTECH_MOC_MAX_FINGERS; i++)
        {
          if (uid_list->actived[i] != 0)
            {
              struct UserId *user_id = &uid_list->uid[i];
              FpPrint *print = fp_print_new (FP_DEVICE (self));
              struct EnrolledInfoItem *item = NULL;
              int index;

              fp_info ("focaltechmoc add slot: %d", i);

              fprint_set_uid (print, user_id->uid, sizeof (user_id->uid));

              if (focaltech_moc_get_enrolled_info_item (self, user_id->uid, &item, &index) == 0)
                {
                  g_autofree gchar *userid_safe = NULL;
                  const gchar *username;
                  userid_safe = g_strndup ((const char *) &item->user_id, FOCALTECH_MOC_USER_ID_LENGTH);
                  fp_dbg ("%s", userid_safe);
                  fpi_print_fill_from_user_id (print, userid_safe);
                  memcpy (data->enrolled_info->user_id[index].uid, user_id->uid, 32);
                  data->enrolled_info->user_des[index].finger = fp_print_get_finger (print);
                  username = fp_print_get_username (print);

                  if (username != NULL)
                    strncpy (data->enrolled_info->user_des[index].username, username, 64);
                }

              g_ptr_array_add (data->list_result, g_object_ref_sink (print));
            }
        }

      for (i = 0; i < FOCALTECH_MOC_MAX_FINGERS; i++)
        {
          struct EnrolledInfoItem *item = &data->enrolled_info->items[i];

          if (data->enrolled_info->actived[i] == 0)
            memset (item, 0, sizeof (struct EnrolledInfoItem));
        }

      fpi_ssm_next_state (self->task_ssm);
    }
}

static void
focaltech_moc_enroll_wait_finger_cb (FpiDeviceFocaltechMoc *self,
                                     uint8_t               *buffer_in,
                                     gsize                  length_in,
                                     GError                *error)
{
  FpCmd *fp_cmd = NULL;
  uint8_t *finger_status = NULL;

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }

  fp_cmd = (FpCmd *) buffer_in;
  finger_status = (uint8_t *) (fp_cmd + 1);

  if (fp_cmd->code != 0x04)
    {
      fpi_ssm_jump_to_state (self->task_ssm, MOC_ENROLL_WAIT_FINGER_DELAY);
    }
  else
    {

      if (*finger_status == 0x01)
        fpi_ssm_jump_to_state (self->task_ssm, MOC_ENROLL_ENROLL_CAPTURE);
      else
        fpi_ssm_jump_to_state (self->task_ssm, MOC_ENROLL_WAIT_FINGER_DELAY);
    }
}

static void
focaltech_moc_enroll_wait_finger_delay (FpDevice *device,
                                        void     *user_data
                                       )
{
  FpiDeviceFocaltechMoc *self = FPI_DEVICE_FOCALTECH_MOC (device);

  fpi_ssm_jump_to_state (self->task_ssm, MOC_ENROLL_WAIT_FINGER);
}

static void
focaltech_moc_start_enroll_cb (FpiDeviceFocaltechMoc *self,
                               uint8_t               *buffer_in,
                               gsize                  length_in,
                               GError                *error)
{
  FpCmd *fp_cmd = NULL;
  struct UserId *user_id = NULL;
  FpPrint *print = NULL;
  struct EnrolledInfoItem *item = NULL;
  int index;

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }

  fp_cmd = (FpCmd *) buffer_in;
  user_id = (struct UserId *) (fp_cmd + 1);

  if (fp_cmd->code != 0x04)
    {
      if (fp_cmd->code == 0x05)
        {
          fpi_ssm_mark_failed (self->task_ssm,
                               fpi_device_error_new_msg (FP_DEVICE_ERROR_DATA_FULL,
                                                         "device data full!!"));
        }
      else
        {
          fpi_ssm_mark_failed (self->task_ssm,
                               fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                         "Can't get response!!"));
        }
      return;
    }

  if (focaltech_moc_get_enrolled_info_item (self, user_id->uid, &item, &index) == 0)
    {
      fpi_ssm_mark_failed (self->task_ssm,
                           fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                     "uid error!!"));
    }
  else
    {
      FpActionData *data = fpi_ssm_get_data (self->task_ssm);
      g_autofree gchar *userid_safe = NULL;
      gsize userid_len;
      uint8_t found = 0;
      int i;
      struct EnrolledInfoItem *free_item = NULL;

      for (i = 0; i < FOCALTECH_MOC_MAX_FINGERS; i++)
        {
          item = &data->enrolled_info->items[i];

          if (data->enrolled_info->actived[i] == 0)
            {
              found = 1;
              free_item = item;
              break;
            }
        }

      if (found == 0)
        {
          fpi_ssm_mark_failed (self->task_ssm,
                               fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                         "no uid slot!!"));
        }
      else
        {
          fpi_device_get_enroll_data (FP_DEVICE (self), &print);
          fprint_set_uid (print, user_id->uid, sizeof (user_id->uid));
          userid_safe = fpi_print_generate_user_id (print);
          userid_len = strlen (userid_safe);
          userid_len = MIN (FOCALTECH_MOC_USER_ID_LENGTH, userid_len);
          fp_info ("focaltechmoc user id: %s", userid_safe);
          memcpy (free_item->uid, user_id->uid, FOCALTECH_MOC_UID_PREFIX_LENGTH);
          memcpy (free_item->user_id, userid_safe, userid_len);
          fpi_ssm_next_state (self->task_ssm);
        }
    }
}

static void
focaltech_moc_enroll_capture_cb (FpiDeviceFocaltechMoc *self,
                                 uint8_t               *buffer_in,
                                 gsize                  length_in,
                                 GError                *error)
{
  FpCmd *fp_cmd = NULL;
  struct CaptureResult *capture_result = NULL;

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }

  fp_cmd = (FpCmd *) buffer_in;
  capture_result = (struct CaptureResult *) (fp_cmd + 1);

  if (fp_cmd->code != 0x04)
    {
      fpi_ssm_mark_failed (self->task_ssm,
                           fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                     "Can't get response!!"));
    }
  else
    {
      if (capture_result->error == ERROR_NONE)
        {
          self->num_frames += 1;
          enroll_status_report (self, ENROLL_RSP_ENROLL_REPORT, self->num_frames, NULL);
          fp_info ("focaltechmoc remain: %d", capture_result->remain);
        }
      else
        {
          enroll_status_report (self, ENROLL_RSP_RETRY, self->num_frames, NULL);
        }

      if (self->num_frames == fp_device_get_nr_enroll_stages (FP_DEVICE (self)))
        fpi_ssm_next_state (self->task_ssm);
      else
        fpi_ssm_jump_to_state (self->task_ssm, MOC_ENROLL_WAIT_FINGER);
    }
}

static void
focaltech_moc_set_enrolled_info_cb (FpiDeviceFocaltechMoc *self,
                                    uint8_t               *buffer_in,
                                    gsize                  length_in,
                                    GError                *error)
{
  FpCmd *fp_cmd = NULL;

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }

  fp_cmd = (FpCmd *) buffer_in;

  if (fp_cmd->code != 0x04)
    {
      fpi_ssm_mark_failed (self->task_ssm,
                           fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                     "Can't get response!!"));
      return;
    }

  fpi_ssm_next_state (self->task_ssm);
}

static void
focaltech_moc_commit_cb (FpiDeviceFocaltechMoc *self,
                         uint8_t               *buffer_in,
                         gsize                  length_in,
                         GError                *error)
{
  FpCmd *fp_cmd = NULL;

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }

  fp_cmd = (FpCmd *) buffer_in;

  if (fp_cmd->code != 0x04)
    {
      fpi_ssm_mark_failed (self->task_ssm,
                           fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                     "Can't get response!!"));
    }
  else
    {
      fp_info ("focaltech_moc_commit_cb success");
      enroll_status_report (self, ENROLL_RSP_ENROLL_OK, self->num_frames, NULL);
      fpi_ssm_next_state (self->task_ssm);
    }
}

static void
focaltech_enroll_run_state (FpiSsm *ssm, FpDevice *device)
{
  FpiDeviceFocaltechMoc *self = FPI_DEVICE_FOCALTECH_MOC (device);
  guint8 *cmd_buf = NULL;
  uint16_t cmd_len = 0;
  uint16_t resp_len = 0;

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case MOC_ENROLL_GET_ENROLLED_INFO:
      {
        uint8_t data = 0x00;
        cmd_len = sizeof (uint8_t);
        resp_len = sizeof (struct EnrolledInfoItem) * FOCALTECH_MOC_MAX_FINGERS;
        cmd_buf = focaltech_moc_compose_cmd (0xaf, &data, cmd_len);
        focaltech_moc_get_cmd (device, cmd_buf,
                               sizeof (FpCmd) + cmd_len + sizeof (uint8_t),
                               sizeof (FpCmd) + resp_len + sizeof (uint8_t),
                               1,
                               focaltech_moc_get_enrolled_info_cb);
        break;
      }

    case MOC_ENROLL_GET_ENROLLED_LIST:
      {
        cmd_len = 0;
        resp_len = sizeof (struct UidList);
        cmd_buf = focaltech_moc_compose_cmd (0xab, NULL, cmd_len);
        focaltech_moc_get_cmd (device, cmd_buf,
                               sizeof (FpCmd) + cmd_len + sizeof (uint8_t),
                               sizeof (FpCmd) + resp_len + sizeof (uint8_t),
                               1,
                               focaltech_moc_get_enrolled_list_cb);
        break;
      }

    case MOC_ENROLL_RELEASE_FINGER:
      {
        uint8_t d1 = 0x78;
        cmd_len = sizeof (d1);
        resp_len = 0;
        cmd_buf = focaltech_moc_compose_cmd (0x82, &d1, cmd_len);
        focaltech_moc_get_cmd (device, cmd_buf,
                               sizeof (FpCmd) + cmd_len + sizeof (uint8_t),
                               sizeof (FpCmd) + resp_len + sizeof (uint8_t),
                               1,
                               focaltech_moc_release_finger);
        break;
      }

    case MOC_ENROLL_START_ENROLL:
      cmd_len = 0;
      resp_len = sizeof (struct UserId);
      cmd_buf = focaltech_moc_compose_cmd (0xa9, NULL, cmd_len);
      focaltech_moc_get_cmd (device, cmd_buf,
                             sizeof (FpCmd) + cmd_len + sizeof (uint8_t),
                             sizeof (FpCmd) + resp_len + sizeof (uint8_t),
                             1,
                             focaltech_moc_start_enroll_cb);
      break;


    case MOC_ENROLL_WAIT_FINGER:
      {
        uint8_t data = 0x02;
        cmd_len = sizeof (uint8_t);
        resp_len = sizeof (uint8_t);
        cmd_buf = focaltech_moc_compose_cmd (0x80, &data, cmd_len);
        focaltech_moc_get_cmd (device, cmd_buf,
                               sizeof (FpCmd) + cmd_len + sizeof (uint8_t),
                               sizeof (FpCmd) + resp_len + sizeof (uint8_t),
                               1,
                               focaltech_moc_enroll_wait_finger_cb);
        break;
      }

    case MOC_ENROLL_WAIT_FINGER_DELAY:
      fpi_device_add_timeout (device, 50,
                              focaltech_moc_enroll_wait_finger_delay,
                              NULL, NULL);
      break;

    case MOC_ENROLL_ENROLL_CAPTURE:
      cmd_len = 0;
      resp_len = sizeof (struct CaptureResult);
      cmd_buf = focaltech_moc_compose_cmd (0xa6, NULL, cmd_len);
      focaltech_moc_get_cmd (device, cmd_buf,
                             sizeof (FpCmd) + cmd_len + sizeof (uint8_t),
                             sizeof (FpCmd) + resp_len + sizeof (uint8_t),
                             1,
                             focaltech_moc_enroll_capture_cb);
      break;

    case MOC_ENROLL_SET_ENROLLED_INFO:
      {
        g_autofree struct EnrolledInfoSetData *set_data = NULL;
        FpActionData *data = fpi_ssm_get_data (self->task_ssm);

        cmd_len = sizeof (struct EnrolledInfoSetData);
        resp_len = 0;
        set_data = (struct EnrolledInfoSetData *) g_malloc0 (cmd_len);
        set_data->data = 0x01;
        memcpy (&set_data->items[0], &data->enrolled_info->items[0],
                FOCALTECH_MOC_MAX_FINGERS * sizeof (struct EnrolledInfoItem));
        cmd_buf = focaltech_moc_compose_cmd (0xaf, (const uint8_t *) set_data, cmd_len);
        focaltech_moc_get_cmd (device, cmd_buf,
                               sizeof (FpCmd) + cmd_len + sizeof (uint8_t),
                               sizeof (FpCmd) + resp_len + sizeof (uint8_t),
                               1,
                               focaltech_moc_set_enrolled_info_cb);
        break;
      }

    case MOC_ENROLL_COMMIT_RESULT:
      {
        FpPrint *print = NULL;
        g_autoptr(GVariant) data = NULL;
        g_autoptr(GVariant) user_id_var = NULL;
        const guint8 *user_id;
        gsize user_id_len = 0;

        fpi_device_get_enroll_data (FP_DEVICE (self), &print);
        g_object_get (print, "fpi-data", &data, NULL);

        if (!g_variant_check_format_string (data, "(@ay)", FALSE))
          {
            fpi_ssm_mark_failed (ssm, fpi_device_error_new (FP_DEVICE_ERROR_DATA_INVALID));
            return;
          }

        g_variant_get (data,
                       "(@ay)",
                       &user_id_var);
        user_id = g_variant_get_fixed_array (user_id_var, &user_id_len, 1);

        cmd_len = user_id_len;
        resp_len = 0;
        cmd_buf = focaltech_moc_compose_cmd (0xa3, user_id, cmd_len);
        focaltech_moc_get_cmd (device, cmd_buf,
                               sizeof (FpCmd) + cmd_len + sizeof (uint8_t),
                               sizeof (FpCmd) + resp_len + sizeof (uint8_t),
                               1,
                               focaltech_moc_commit_cb);
        break;
      }
    }
}

static void
focaltech_moc_enroll (FpDevice *device)
{
  FpiDeviceFocaltechMoc *self = FPI_DEVICE_FOCALTECH_MOC (device);
  FpActionData *data = g_new0 (FpActionData, 1);

  data->enrolled_info = g_new0 (struct EnrolledInfo, 1);
  data->list_result = g_ptr_array_new_with_free_func (g_object_unref);

  self->task_ssm = fpi_ssm_new (FP_DEVICE (self),
                                focaltech_enroll_run_state,
                                MOC_ENROLL_NUM_STATES);
  fpi_ssm_set_data (self->task_ssm, data, (GDestroyNotify) fp_action_ssm_done_data_free);
  fpi_ssm_start (self->task_ssm, task_ssm_done);
}

static void
focaltech_moc_delete_cb (FpiDeviceFocaltechMoc *self,
                         uint8_t               *buffer_in,
                         gsize                  length_in,
                         GError                *error)
{
  FpCmd *fp_cmd = NULL;

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }

  fp_cmd = (FpCmd *) buffer_in;

  if (fp_cmd->code != 0x04)
    {
      fpi_ssm_mark_failed (self->task_ssm,
                           fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                     "Can't get response!!"));
    }
  else
    {
      FpActionData *data = fpi_ssm_get_data (self->task_ssm);
      int ssm_state;

      if (self->delete_slot != -1)
        {
          fp_dbg ("delete slot %d", self->delete_slot);
          data->enrolled_info->actived[self->delete_slot] = 0;
          memset (&data->enrolled_info->items[self->delete_slot], 0, sizeof (struct EnrolledInfoItem));
          memset (&data->enrolled_info->user_id[self->delete_slot], 0, sizeof (struct UserId));
          memset (&data->enrolled_info->user_des[self->delete_slot], 0, sizeof (struct UserDes));
        }

      ssm_state = fpi_ssm_get_cur_state (self->task_ssm);
      fpi_ssm_jump_to_state (self->task_ssm, ssm_state);
    }
}

enum delete_states {
  MOC_DELETE_GET_ENROLLED_INFO,
  MOC_DELETE_GET_ENROLLED_LIST,
  MOC_DELETE_SET_ENROLLED_INFO,
  MOC_DELETE_BY_UID,
  MOC_DELETE_BY_USER_INFO,
  MOC_DELETE_NUM_STATES,
};

static void
focaltech_delete_run_state (FpiSsm *ssm, FpDevice *device)
{
  guint8 *cmd_buf = NULL;
  uint16_t cmd_len = 0;
  uint16_t resp_len = 0;

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case MOC_DELETE_GET_ENROLLED_INFO:
      {
        uint8_t data = 0x00;
        cmd_len = sizeof (uint8_t);
        resp_len = sizeof (struct EnrolledInfoItem) * FOCALTECH_MOC_MAX_FINGERS;
        cmd_buf = focaltech_moc_compose_cmd (0xaf, &data, cmd_len);
        focaltech_moc_get_cmd (device, cmd_buf,
                               sizeof (FpCmd) + cmd_len + sizeof (uint8_t),
                               sizeof (FpCmd) + resp_len + sizeof (uint8_t),
                               1,
                               focaltech_moc_get_enrolled_info_cb);
        break;
      }

    case MOC_DELETE_GET_ENROLLED_LIST:
      {
        cmd_len = 0;
        resp_len = sizeof (struct UidList) + sizeof (struct UserId) * FOCALTECH_MOC_MAX_FINGERS;
        cmd_buf = focaltech_moc_compose_cmd (0xab, NULL, cmd_len);
        focaltech_moc_get_cmd (device, cmd_buf,
                               sizeof (FpCmd) + cmd_len + sizeof (uint8_t),
                               sizeof (FpCmd) + resp_len + sizeof (uint8_t),
                               1,
                               focaltech_moc_get_enrolled_list_cb);
        break;
      }

    case MOC_DELETE_SET_ENROLLED_INFO:
      {
        FpiDeviceFocaltechMoc *self = FPI_DEVICE_FOCALTECH_MOC (device);
        g_autofree struct EnrolledInfoSetData *set_data = NULL;
        FpActionData *data = fpi_ssm_get_data (self->task_ssm);

        cmd_len = sizeof (struct EnrolledInfoSetData);
        resp_len = 0;
        set_data = (struct EnrolledInfoSetData *) g_malloc0 (cmd_len);
        set_data->data = 0x01;
        memcpy (&set_data->items[0], &data->enrolled_info->items[0], FOCALTECH_MOC_MAX_FINGERS * sizeof (struct EnrolledInfoItem));
        cmd_buf = focaltech_moc_compose_cmd (0xaf, (const uint8_t *) set_data, cmd_len);
        focaltech_moc_get_cmd (device, cmd_buf,
                               sizeof (FpCmd) + cmd_len + sizeof (uint8_t),
                               sizeof (FpCmd) + resp_len + sizeof (uint8_t),
                               1,
                               focaltech_moc_set_enrolled_info_cb);
        break;
      }

    case MOC_DELETE_BY_UID:
      {
        FpiDeviceFocaltechMoc *self = FPI_DEVICE_FOCALTECH_MOC (device);
        FpPrint *print = NULL;
        g_autoptr(GVariant) data = NULL;
        g_autoptr(GVariant) user_id_var = NULL;
        const guint8 *user_id;
        gsize user_id_len = 0;
        struct EnrolledInfoItem *item = NULL;
        int index;

        self->delete_slot = -1;
        fpi_device_get_delete_data (device, &print);
        g_object_get (print, "fpi-data", &data, NULL);

        if (!g_variant_check_format_string (data, "(@ay)", FALSE))
          {
            fpi_device_delete_complete (device,
                                        fpi_device_error_new (FP_DEVICE_ERROR_DATA_INVALID));
            return;
          }

        g_variant_get (data, "(@ay)", &user_id_var);
        user_id = g_variant_get_fixed_array (user_id_var, &user_id_len, 1);

        if (focaltech_moc_get_enrolled_info_item (self, (uint8_t *) user_id, &item, &index) == 0)
          self->delete_slot = index;

        if (self->delete_slot != -1)
          {
            cmd_len = sizeof (struct UserId);
            resp_len = 0;
            cmd_buf = focaltech_moc_compose_cmd (0xa8, user_id, cmd_len);
            focaltech_moc_get_cmd (device, cmd_buf,
                                   sizeof (FpCmd) + cmd_len + sizeof (uint8_t),
                                   sizeof (FpCmd) + resp_len + sizeof (uint8_t),
                                   1,
                                   focaltech_moc_delete_cb);
          }
        else
          {
            fpi_ssm_next_state (self->task_ssm);
          }

        break;
      }

    case MOC_DELETE_BY_USER_INFO:
      {
        FpiDeviceFocaltechMoc *self = FPI_DEVICE_FOCALTECH_MOC (device);
        FpActionData *data = fpi_ssm_get_data (self->task_ssm);
        FpPrint *print = NULL;
        const guint8 *user_id;
        const gchar *username;
        uint8_t finger;
        int i;

        self->delete_slot = -1;
        fpi_device_get_delete_data (device, &print);
        username = fp_print_get_username (print);
        finger = fp_print_get_finger (print);

        for (i = 0; i < FOCALTECH_MOC_MAX_FINGERS; i++)
          {
            struct UserDes *user_des = &data->enrolled_info->user_des[i];

            if (username == NULL)
              continue;

            if (strncmp (user_des->username, username, FOCALTECH_MOC_USER_ID_LENGTH) != 0)
              continue;

            if (finger != user_des->finger)
              continue;

            self->delete_slot = i;
          }

        if (self->delete_slot != -1)
          {
            user_id = (const guint8 *) &data->enrolled_info->user_id[self->delete_slot].uid[0];
            cmd_len = sizeof (struct UserId);
            resp_len = 0;
            cmd_buf = focaltech_moc_compose_cmd (0xa8, user_id, cmd_len);
            focaltech_moc_get_cmd (device, cmd_buf,
                                   sizeof (FpCmd) + cmd_len + sizeof (uint8_t),
                                   sizeof (FpCmd) + resp_len + sizeof (uint8_t),
                                   1,
                                   focaltech_moc_delete_cb);
          }
        else
          {
            fpi_device_delete_complete (FP_DEVICE (self), NULL);
            fpi_ssm_next_state (self->task_ssm);
          }

        break;
      }
    }
}

static void
focaltech_moc_delete_print (FpDevice *device)
{
  FpiDeviceFocaltechMoc *self = FPI_DEVICE_FOCALTECH_MOC (device);
  FpActionData *data = g_new0 (FpActionData, 1);

  data->enrolled_info = g_new0 (struct EnrolledInfo, 1);
  data->list_result = g_ptr_array_new_with_free_func (g_object_unref);

  self->task_ssm = fpi_ssm_new (device,
                                focaltech_delete_run_state,
                                MOC_DELETE_NUM_STATES);
  fpi_ssm_set_data (self->task_ssm, data, (GDestroyNotify) fp_action_ssm_done_data_free);
  fpi_ssm_start (self->task_ssm, task_ssm_done);
}

enum moc_list_states {
  MOC_LIST_GET_ENROLLED_INFO,
  MOC_LIST_GET_ENROLLED_LIST,
  MOC_LIST_REPORT,
  MOC_LIST_NUM_STATES,
};

static void
focaltech_list_run_state (FpiSsm *ssm, FpDevice *device)
{
  FpiDeviceFocaltechMoc *self = FPI_DEVICE_FOCALTECH_MOC (device);
  guint8 *cmd_buf = NULL;
  uint16_t cmd_len = 0;
  uint16_t resp_len = 0;

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case MOC_LIST_GET_ENROLLED_INFO:
      {
        uint8_t data = 0x00;
        cmd_len = sizeof (uint8_t);
        resp_len = sizeof (struct EnrolledInfoItem) * FOCALTECH_MOC_MAX_FINGERS;
        cmd_buf = focaltech_moc_compose_cmd (0xaf, &data, cmd_len);
        focaltech_moc_get_cmd (device, cmd_buf,
                               sizeof (FpCmd) + cmd_len + sizeof (uint8_t),
                               sizeof (FpCmd) + resp_len + sizeof (uint8_t),
                               1,
                               focaltech_moc_get_enrolled_info_cb);
        break;
      }

    case MOC_LIST_GET_ENROLLED_LIST:
      {
        cmd_len = 0;
        resp_len = sizeof (struct UidList) + sizeof (struct UserId) * FOCALTECH_MOC_MAX_FINGERS;
        cmd_buf = focaltech_moc_compose_cmd (0xab, NULL, cmd_len);
        focaltech_moc_get_cmd (device, cmd_buf,
                               sizeof (FpCmd) + cmd_len + sizeof (uint8_t),
                               sizeof (FpCmd) + resp_len + sizeof (uint8_t),
                               1,
                               focaltech_moc_get_enrolled_list_cb);
        break;
      }

    case MOC_LIST_REPORT:
      {
        FpActionData *data = fpi_ssm_get_data (self->task_ssm);
        fpi_device_list_complete (FP_DEVICE (self), g_steal_pointer (&data->list_result), NULL);
        fpi_ssm_next_state (self->task_ssm);
        break;
      }
    }
}

static void
focaltech_moc_list (FpDevice *device)
{
  FpiDeviceFocaltechMoc *self = FPI_DEVICE_FOCALTECH_MOC (device);
  FpActionData *data = g_new0 (FpActionData, 1);

  data->enrolled_info = g_new0 (struct EnrolledInfo, 1);
  data->list_result = g_ptr_array_new_with_free_func (g_object_unref);
  self->task_ssm = fpi_ssm_new (device,
                                focaltech_list_run_state,
                                MOC_LIST_NUM_STATES);
  fpi_ssm_set_data (self->task_ssm, data, (GDestroyNotify) fp_action_ssm_done_data_free);
  fpi_ssm_start (self->task_ssm, task_ssm_done);
}

static void
fpi_device_focaltech_moc_init (FpiDeviceFocaltechMoc *self)
{
  G_DEBUG_HERE ();
}

static void
fpi_device_focaltech_moc_class_init (FpiDeviceFocaltechMocClass *klass)
{
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);

  dev_class->id = FP_COMPONENT;
  dev_class->full_name = FOCALTECH_MOC_DRIVER_FULLNAME;

  dev_class->type = FP_DEVICE_TYPE_USB;
  dev_class->scan_type = FP_SCAN_TYPE_PRESS;
  dev_class->id_table = id_table;
  dev_class->nr_enroll_stages = FOCALTECH_MOC_MAX_FINGERS;
  dev_class->temp_hot_seconds = -1;

  dev_class->open = focaltech_moc_open;
  dev_class->close = focaltech_moc_close;
  dev_class->verify = focaltech_moc_identify;
  dev_class->enroll = focaltech_moc_enroll;
  dev_class->identify = focaltech_moc_identify;
  dev_class->delete = focaltech_moc_delete_print;
  dev_class->list = focaltech_moc_list;

  fpi_device_class_auto_initialize_features (dev_class);
}
