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

#define FP_COMPONENT "egismoc"

#include <stdio.h>
#include <glib.h>
#include <sys/param.h>

#include "drivers_api.h"

#include "egismoc.h"

struct _FpiDeviceEgisMoc
{
  FpDevice        parent;
  FpiSsm         *task_ssm;
  FpiSsm         *cmd_ssm;
  FpiUsbTransfer *cmd_transfer;
  GCancellable   *interrupt_cancellable;

  int             enrolled_num;
  GPtrArray      *enrolled_ids;
};

G_DEFINE_TYPE (FpiDeviceEgisMoc, fpi_device_egismoc, FP_TYPE_DEVICE);

static const FpIdEntry egismoc_id_table[] = {
  { .vid = 0x1c7a, .pid = 0x0582, .driver_data = EGISMOC_DRIVER_CHECK_PREFIX_TYPE1 },
  { .vid = 0x1c7a, .pid = 0x05a1, .driver_data = EGISMOC_DRIVER_CHECK_PREFIX_TYPE2 },
  { .vid = 0,      .pid = 0,      .driver_data = 0 }
};

typedef void (*SynCmdMsgCallback) (FpDevice *device,
                                   guchar   *buffer_in,
                                   gsize     length_in,
                                   GError   *error);

typedef struct egismoc_command_data
{
  SynCmdMsgCallback callback;
} CommandData;

typedef struct egismoc_enroll_print
{
  FpPrint *print;
  int      stage;
} EnrollPrint;

static void
egismoc_finger_on_sensor_cb (FpiUsbTransfer *transfer,
                             FpDevice       *device,
                             gpointer        userdata,
                             GError         *error)
{
  fp_dbg ("Finger on sensor callback");
  fpi_device_report_finger_status (device, FP_FINGER_STATUS_PRESENT);

  g_return_if_fail (transfer->ssm);
  if (error)
    fpi_ssm_mark_failed (transfer->ssm, error);
  else
    fpi_ssm_next_state (transfer->ssm);
}

static void
egismoc_wait_finger_on_sensor (FpiSsm   *ssm,
                               FpDevice *device)
{
  fp_dbg ("Wait for finger on sensor");
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);

  g_autoptr(FpiUsbTransfer) transfer = fpi_usb_transfer_new (device);

  fpi_usb_transfer_fill_interrupt (transfer, EGISMOC_EP_CMD_INTERRUPT_IN,
                                   EGISMOC_USB_INTERRUPT_IN_RECV_LENGTH);
  transfer->ssm = ssm;
  /* Interrupt on this device always returns 1 byte short; this is expected */
  transfer->short_is_error = FALSE;

  fpi_device_report_finger_status (device, FP_FINGER_STATUS_NEEDED);

  fpi_usb_transfer_submit (g_steal_pointer (&transfer),
                           EGISMOC_USB_INTERRUPT_TIMEOUT,
                           self->interrupt_cancellable,
                           egismoc_finger_on_sensor_cb,
                           NULL);
}

static gboolean
egismoc_validate_response_prefix (const guchar *buffer_in,
                                  const gsize   buffer_in_len,
                                  const guchar *valid_prefix,
                                  const gsize   valid_prefix_len)
{
  const gboolean result = memcmp (buffer_in +
                                  (egismoc_read_prefix_len +
                                   EGISMOC_CHECK_BYTES_LENGTH),
                                  valid_prefix,
                                  valid_prefix_len) == 0;

  fp_dbg ("Response prefix valid: %s", result ? "yes" : "NO");
  return result;
}

static gboolean
egismoc_validate_response_suffix (const guchar *buffer_in,
                                  const gsize   buffer_in_len,
                                  const guchar *valid_suffix,
                                  const gsize   valid_suffix_len)
{
  const gboolean result = memcmp (buffer_in + (buffer_in_len - valid_suffix_len),
                                  valid_suffix,
                                  valid_suffix_len) == 0;

  fp_dbg ("Response suffix valid: %s", result ? "yes" : "NO");
  return result;
}

static void
egismoc_task_ssm_done (FpiSsm   *ssm,
                       FpDevice *device,
                       GError   *error)
{
  fp_dbg ("Task SSM done");
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);

  /* task_ssm is going to be freed by completion of SSM */
  g_assert (!self->task_ssm || self->task_ssm == ssm);
  self->task_ssm = NULL;

  g_clear_pointer (&self->enrolled_ids, g_ptr_array_unref);
  self->enrolled_ids = NULL;
  self->enrolled_num = -1;

  if (error)
    fpi_device_action_error (device, error);
}

static void
egismoc_task_ssm_next_state_cb (FpDevice *device,
                                guchar   *buffer_in,
                                gsize     length_in,
                                GError   *error)
{
  fp_dbg ("Task SSM next state callback");
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);

  if (error)
    fpi_ssm_mark_failed (self->task_ssm, error);
  else
    fpi_ssm_next_state (self->task_ssm);
}

static void
egismoc_cmd_receive_cb (FpiUsbTransfer *transfer,
                        FpDevice       *device,
                        gpointer        userdata,
                        GError         *error)
{
  g_autofree guchar *buffer = NULL;
  CommandData *data = userdata;
  SynCmdMsgCallback callback;
  gssize actual_length;

  fp_dbg ("Command receive callback");

  if (error)
    {
      fpi_ssm_mark_failed (transfer->ssm, error);
      return;
    }
  if (data == NULL || transfer->actual_length < egismoc_read_prefix_len)
    {
      fpi_ssm_mark_failed (transfer->ssm,
                           fpi_device_error_new (FP_DEVICE_ERROR_GENERAL));
      return;
    }

  /* Let's complete the previous ssm and then handle the callback, so that
   * we are sure that we won't start a transfer or a new command while there is
   * another one still ongoing
   */
  callback = data->callback;
  buffer = g_steal_pointer (&transfer->buffer);
  actual_length = transfer->actual_length;

  fpi_ssm_mark_completed (transfer->ssm);

  if (callback)
    callback (device, buffer, actual_length, NULL);
}

static void
egismoc_cmd_run_state (FpiSsm   *ssm,
                       FpDevice *device)
{
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);

  g_autoptr(FpiUsbTransfer) transfer = NULL;

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case CMD_SEND:
      if (self->cmd_transfer)
        {
          self->cmd_transfer->ssm = ssm;
          fpi_usb_transfer_submit (g_steal_pointer (&self->cmd_transfer),
                                   EGISMOC_USB_SEND_TIMEOUT,
                                   fpi_device_get_cancellable (device),
                                   fpi_ssm_usb_transfer_cb,
                                   NULL);
          break;
        }

      fpi_ssm_next_state (ssm);
      break;

    case CMD_GET:
      transfer = fpi_usb_transfer_new (device);
      transfer->ssm = ssm;
      fpi_usb_transfer_fill_bulk (transfer, EGISMOC_EP_CMD_IN,
                                  EGISMOC_USB_IN_RECV_LENGTH);
      fpi_usb_transfer_submit (g_steal_pointer (&transfer),
                               EGISMOC_USB_RECV_TIMEOUT,
                               fpi_device_get_cancellable (device),
                               egismoc_cmd_receive_cb,
                               fpi_ssm_get_data (ssm));
      break;
    }
}

static void
egismoc_cmd_ssm_done (FpiSsm   *ssm,
                      FpDevice *device,
                      GError   *error)
{
  g_autoptr(GError) local_error = error;
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);
  CommandData *data = fpi_ssm_get_data (ssm);

  g_assert (self->cmd_ssm == ssm);
  g_assert (!self->cmd_transfer || self->cmd_transfer->ssm == ssm);

  self->cmd_ssm = NULL;
  self->cmd_transfer = NULL;

  if (error && data && data->callback)
    data->callback (device, NULL, 0, g_steal_pointer (&local_error));
}

typedef union
{
  guint16 check_value;
  guchar  check_bytes[EGISMOC_CHECK_BYTES_LENGTH];
} EgisMocCheckBytes;

G_STATIC_ASSERT (G_SIZEOF_MEMBER (EgisMocCheckBytes, check_value) ==
                 sizeof (guint8) * EGISMOC_CHECK_BYTES_LENGTH);

/*
 * Derive the 2 "check bytes" for write payloads
 * 32-bit big-endian sum of all 16-bit words (including check bytes) MOD 0xFFFF
 * should be 0, otherwise the device will reject the payload
 */
static EgisMocCheckBytes
egismoc_get_check_bytes (const guchar *value,
                         const gsize   value_length)
{
  fp_dbg ("Get check bytes");
  EgisMocCheckBytes check_bytes;
  const size_t steps = (value_length + 1) / 2;
  guint16 values[steps];
  size_t sum_values = 0;

  for (int i = 0, j = 0; i < value_length; i += 2, j++)
    {
      values[j] = (value[i] << 8 & 0xff00);

      if (i < value_length - 1)
        values[j] |= value[i + 1] & 0x00ff;
    }

  for (int i = 0; i < steps; i++)
    sum_values += values[i];

  check_bytes.check_value = GUINT16_TO_BE (0xffff - (sum_values % 0xffff));
  return check_bytes;
}

static void
egismoc_exec_cmd (FpDevice         *device,
                  guchar           *cmd,
                  const gsize       cmd_length,
                  GDestroyNotify    cmd_destroy,
                  SynCmdMsgCallback callback)
{
  fp_dbg ("Execute command and get response");
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);
  EgisMocCheckBytes check_bytes;
  g_autofree guchar *buffer_out = NULL;
  gsize buffer_out_length = 0;

  g_autoptr(FpiUsbTransfer) transfer = NULL;
  CommandData *data = g_new0 (CommandData, 1);

  g_assert (self->cmd_ssm == NULL);
  self->cmd_ssm = fpi_ssm_new (device,
                               egismoc_cmd_run_state,
                               CMD_STATES);

  transfer = fpi_usb_transfer_new (device);
  transfer->short_is_error = TRUE;

  /*
   * buffer_out should be a fully composed command (with prefix, check bytes, etc)
   * which looks like this:
   *   E G I S 00 00 00 01 {cb1} {cb2} {payload}
   * where cb1 and cb2 are some check bytes generated by the
   * egismoc_get_check_bytes() method and payload is what is passed via the cmd
   * parameter
   */
  buffer_out_length = egismoc_write_prefix_len
                      + EGISMOC_CHECK_BYTES_LENGTH
                      + cmd_length;
  buffer_out = g_new0 (guchar, buffer_out_length);

  /* Prefix */
  memcpy (buffer_out, egismoc_write_prefix, egismoc_write_prefix_len);

  /* Check Bytes - leave them as 00 for now then later generate and copy over
   * the real ones */

  /* Command Payload */
  memcpy (buffer_out + egismoc_write_prefix_len + EGISMOC_CHECK_BYTES_LENGTH,
          cmd, cmd_length);

  /* destroy cmd if requested */
  if (cmd_destroy)
    cmd_destroy (cmd);

  /* Now fetch and set the "real" check bytes based on the currently
   * assembled payload */
  check_bytes = egismoc_get_check_bytes (buffer_out, buffer_out_length);
  memcpy (buffer_out + egismoc_write_prefix_len, check_bytes.check_bytes,
          EGISMOC_CHECK_BYTES_LENGTH);

  fpi_usb_transfer_fill_bulk_full (transfer,
                                   EGISMOC_EP_CMD_OUT,
                                   g_steal_pointer (&buffer_out),
                                   buffer_out_length,
                                   g_free);
  transfer->ssm = self->cmd_ssm;

  g_assert (self->cmd_transfer == NULL);
  self->cmd_transfer = g_steal_pointer (&transfer);
  data->callback = callback;

  fpi_ssm_set_data (self->cmd_ssm, data, g_free);
  fpi_ssm_start (self->cmd_ssm, egismoc_cmd_ssm_done);
}

static void
egismoc_set_print_data (FpPrint     *print,
                        const gchar *device_print_id,
                        const gchar *user_id)
{
  GVariant *print_id_var = NULL;
  GVariant *fpi_data = NULL;
  g_autofree gchar *fill_user_id = NULL;

  if (user_id)
    fill_user_id = g_strdup (user_id);
  else
    fill_user_id = g_strndup (device_print_id, EGISMOC_FINGERPRINT_DATA_SIZE);

  fpi_print_fill_from_user_id (print, fill_user_id);

  fpi_print_set_type (print, FPI_PRINT_RAW);
  fpi_print_set_device_stored (print, TRUE);

  g_object_set (print, "description", fill_user_id, NULL);

  print_id_var = g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE,
                                            device_print_id,
                                            EGISMOC_FINGERPRINT_DATA_SIZE,
                                            sizeof (guchar));
  fpi_data = g_variant_new ("(@ay)", print_id_var);
  g_object_set (print, "fpi-data", fpi_data, NULL);
}

static GPtrArray *
egismoc_get_enrolled_prints (FpDevice *device)
{
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);

  g_autoptr(GPtrArray) result = g_ptr_array_new_with_free_func (g_object_unref);
  FpPrint *print = NULL;

  for (int i = 0; i < self->enrolled_num; i++)
    {
      print = fp_print_new (device);
      egismoc_set_print_data (print, g_ptr_array_index (self->enrolled_ids, i), NULL);
      g_ptr_array_add (result, g_object_ref_sink (print));
    }

  return g_steal_pointer (&result);
}

static void
egismoc_list_fill_enrolled_ids_cb (FpDevice *device,
                                   guchar   *buffer_in,
                                   gsize     length_in,
                                   GError   *error)
{
  fp_dbg ("List callback");
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }

  g_clear_pointer (&self->enrolled_ids, g_ptr_array_unref);
  self->enrolled_ids = g_ptr_array_new_with_free_func (g_free);
  self->enrolled_num = 0;

  /*
   * Each fingerprint ID will be returned in this response as a 32 byte array
   * The other stuff in the payload is 16 bytes long, so if there is at least 1
   * print then the length should be at least 16+32=48 bytes long
   */
  for (int pos = EGISMOC_LIST_RESPONSE_PREFIX_SIZE;
       pos < length_in - EGISMOC_LIST_RESPONSE_SUFFIX_SIZE;
       pos += EGISMOC_FINGERPRINT_DATA_SIZE, self->enrolled_num++)
    {
      g_autofree gchar *print_id = g_strndup ((gchar *) buffer_in + pos,
                                              EGISMOC_FINGERPRINT_DATA_SIZE);
      fp_dbg ("Device fingerprint %0d: %.*s", self->enrolled_num,
              EGISMOC_FINGERPRINT_DATA_SIZE, print_id);
      g_ptr_array_add (self->enrolled_ids, g_steal_pointer (&print_id));
    }

  fp_info ("Number of currently enrolled fingerprints on the device is %d",
           self->enrolled_num);

  if (self->task_ssm)
    fpi_ssm_next_state (self->task_ssm);
}

static void
egismoc_list_run_state (FpiSsm   *ssm,
                        FpDevice *device)
{
  g_autoptr(GPtrArray) enrolled_prints = NULL;

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case LIST_GET_ENROLLED_IDS:
      egismoc_exec_cmd (device, cmd_list, cmd_list_len, NULL,
                        egismoc_list_fill_enrolled_ids_cb);
      break;

    case LIST_RETURN_ENROLLED_PRINTS:
      enrolled_prints = egismoc_get_enrolled_prints (device);
      fpi_device_list_complete (device, g_steal_pointer (&enrolled_prints), NULL);
      fpi_ssm_next_state (ssm);
      break;
    }
}

static void
egismoc_list (FpDevice *device)
{
  fp_dbg ("List");
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);

  g_assert (self->task_ssm == NULL);
  self->task_ssm = fpi_ssm_new (device,
                                egismoc_list_run_state,
                                LIST_STATES);
  fpi_ssm_start (self->task_ssm, egismoc_task_ssm_done);
}

static guchar *
egismoc_get_delete_cmd (FpDevice *device,
                        FpPrint  *delete_print,
                        gsize    *length_out)
{
  fp_dbg ("Get delete command");
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);
  g_autoptr(GVariant) print_data = NULL;
  g_autoptr(GVariant) print_data_id_var = NULL;
  const guchar *print_data_id = NULL;
  gsize print_data_id_len = 0;
  g_autofree gchar *print_description = NULL;
  g_autofree guchar *enrolled_print_id = NULL;
  g_autofree guchar *result = NULL;
  gsize pos = 0;

  /*
   * The final command body should contain:
   * 1) hard-coded 00 00
   * 2) 2-byte size indiciator, 20*Number deleted identifiers plus 7 in form of:
   *    num_to_delete * 0x20 + 0x07
   *    Since max prints can be higher than 7 then this goes up to 2 bytes
   *    (e9 + 9 = 109)
   * 3) Hard-coded prefix (cmd_delete_prefix)
   * 4) 2-byte size indiciator, 20*Number of enrolled identifiers without plus 7
   *    (num_to_delete * 0x20)
   * 5) All of the currently registered prints to delete in their 32-byte device
   *    identifiers (enrolled_list)
   */

  const int num_to_delete = (!delete_print) ? self->enrolled_num : 1;
  const gsize body_length = sizeof (guchar) * EGISMOC_FINGERPRINT_DATA_SIZE *
                            num_to_delete;
  /* total_length is the 6 various bytes plus prefix and body payload */
  const gsize total_length = (sizeof (guchar) * 6) + cmd_delete_prefix_len +
                             body_length;

  /* pre-fill entire payload with 00s */
  result = g_new0 (guchar, total_length);

  /* start with 00 00 (just move starting offset up by 2) */
  pos = 2;

  /* Size Counter bytes */
  /* "easiest" way to handle 2-bytes size for counter is to hard-code logic for
   * when we go to the 2nd byte
   * note this will not work in case any model ever supports more than 14 prints
   * (assumed max is 10) */
  if (num_to_delete > 7)
    {
      memset (result + pos, 0x01, sizeof (guchar));
      pos += sizeof (guchar);
      memset (result + pos, ((num_to_delete - 8) * 0x20) + 0x07, sizeof (guchar));
      pos += sizeof (guchar);
    }
  else
    {
      /* first byte is 0x00, just skip it */
      pos += sizeof (guchar);
      memset (result + pos, (num_to_delete * 0x20) + 0x07, sizeof (guchar));
      pos += sizeof (guchar);
    }

  /* command prefix */
  memcpy (result + pos, cmd_delete_prefix, cmd_delete_prefix_len);
  pos += cmd_delete_prefix_len;

  /* 2-bytes size logic for counter again */
  if (num_to_delete > 7)
    {
      memset (result + pos, 0x01, sizeof (guchar));
      pos += sizeof (guchar);
      memset (result + pos, ((num_to_delete - 8) * 0x20), sizeof (guchar));
      pos += sizeof (guchar);
    }
  else
    {
      /* first byte is 0x00, just skip it */
      pos += sizeof (guchar);
      memset (result + pos, (num_to_delete * 0x20), sizeof (guchar));
      pos += sizeof (guchar);
    }

  /* append desired 32-byte fingerprint IDs */
  /* if passed a delete_print then fetch its data from the FpPrint */
  if (delete_print)
    {
      g_object_get (delete_print, "description", &print_description, NULL);
      g_object_get (delete_print, "fpi-data", &print_data, NULL);

      if (!g_variant_check_format_string (print_data, "(@ay)", FALSE))
        {
          fpi_ssm_mark_failed (self->task_ssm,
                               fpi_device_error_new (FP_DEVICE_ERROR_DATA_INVALID));
          return NULL;
        }

      g_variant_get (print_data, "(@ay)", &print_data_id_var);
      print_data_id = g_variant_get_fixed_array (print_data_id_var,
                                                 &print_data_id_len, sizeof (guchar));

      if (!g_str_has_prefix (print_description, "FP"))
        fp_dbg ("Fingerprint '%s' was not created by libfprint; deleting anyway.",
                print_description);

      fp_info ("Delete fingerprint %s (%s)", print_description, print_data_id);

      memcpy (result + pos, print_data_id, EGISMOC_FINGERPRINT_DATA_SIZE);
    }
  /* Otherwise assume this is a "clear" - just loop through and append all enrolled IDs */
  else
    {
      for (int i = 0; i < self->enrolled_ids->len; i++)
        memcpy (result + pos + (EGISMOC_FINGERPRINT_DATA_SIZE * i),
                g_ptr_array_index (self->enrolled_ids, i),
                EGISMOC_FINGERPRINT_DATA_SIZE);
    }

  if (length_out)
    *length_out = total_length;

  return g_steal_pointer (&result);
}

static void
egismoc_delete_cb (FpDevice *device,
                   guchar   *buffer_in,
                   gsize     length_in,
                   GError   *error)
{
  fp_dbg ("Delete callback");
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }

  /* Check that the read payload indicates "success" with the delete */
  if (egismoc_validate_response_prefix (buffer_in,
                                        length_in,
                                        rsp_delete_success_prefix,
                                        rsp_delete_success_prefix_len))
    {
      if (fpi_device_get_current_action (device) == FPI_DEVICE_ACTION_CLEAR_STORAGE)
        {
          fpi_device_clear_storage_complete (device, NULL);
          fpi_ssm_next_state (self->task_ssm);
        }
      else if (fpi_device_get_current_action (device) == FPI_DEVICE_ACTION_DELETE)
        {
          fpi_device_delete_complete (device, NULL);
          fpi_ssm_next_state (self->task_ssm);
        }
      else
        {
          fpi_ssm_mark_failed (self->task_ssm,
                               fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                         "Unsupported delete action."));
        }
    }
  else
    {
      fpi_ssm_mark_failed (self->task_ssm,
                           fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                     "Delete print was not successful"));
    }
}

static void
egismoc_delete_run_state (FpiSsm   *ssm,
                          FpDevice *device)
{
  g_autofree guchar *payload = NULL;
  gsize payload_length = 0;

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case DELETE_GET_ENROLLED_IDS:
      /* get enrolled_ids and enrolled_num from device for use building
       * delete payload below
       */
      egismoc_exec_cmd (device, cmd_list, cmd_list_len, NULL,
                        egismoc_list_fill_enrolled_ids_cb);
      break;

    case DELETE_DELETE:
      if (fpi_device_get_current_action (device) == FPI_DEVICE_ACTION_DELETE)
        payload = egismoc_get_delete_cmd (device, fpi_ssm_get_data (ssm),
                                          &payload_length);
      else
        payload = egismoc_get_delete_cmd (device, NULL, &payload_length);

      egismoc_exec_cmd (device, g_steal_pointer (&payload), payload_length,
                        g_free, egismoc_delete_cb);
      break;
    }
}

static void
egismoc_clear_storage (FpDevice *device)
{
  fp_dbg ("Clear storage");
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);

  g_assert (self->task_ssm == NULL);
  self->task_ssm = fpi_ssm_new (device,
                                egismoc_delete_run_state,
                                DELETE_STATES);
  fpi_ssm_start (self->task_ssm, egismoc_task_ssm_done);
}

static void
egismoc_delete (FpDevice *device)
{
  fp_dbg ("Delete");
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);
  FpPrint *delete_print = NULL;

  fpi_device_get_delete_data (device, &delete_print);

  g_assert (self->task_ssm == NULL);
  self->task_ssm = fpi_ssm_new (device,
                                egismoc_delete_run_state,
                                DELETE_STATES);
  /* the print is owned by libfprint during deletion task */
  fpi_ssm_set_data (self->task_ssm, delete_print, NULL);
  fpi_ssm_start (self->task_ssm, egismoc_task_ssm_done);
}

static void
egismoc_enroll_status_report (FpDevice    *device,
                              EnrollPrint *enroll_print,
                              EnrollStatus status,
                              GError      *error)
{
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);

  switch (status)
    {
    case ENROLL_STATUS_DEVICE_FULL:
    case ENROLL_STATUS_DUPLICATE:
      fpi_ssm_mark_failed (self->task_ssm, error);
      break;

    case ENROLL_STATUS_RETRY:
      fpi_device_enroll_progress (device, enroll_print->stage, NULL, error);
      break;

    case ENROLL_STATUS_PARTIAL_OK:
      enroll_print->stage++;
      fp_info ("Partial capture successful. Please touch the sensor again (%d/%d)",
               enroll_print->stage,
               EGISMOC_MAX_ENROLL_NUM);
      fpi_device_enroll_progress (device, enroll_print->stage, enroll_print->print, NULL);
      break;

    case ENROLL_STATUS_COMPLETE:
      fp_info ("Enrollment was successful!");
      fpi_device_enroll_complete (device, g_object_ref (enroll_print->print), NULL);
      break;

    default:
      if (error)
        fpi_ssm_mark_failed (self->task_ssm, error);
      else
        fpi_ssm_mark_failed (self->task_ssm,
                             fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                                       "Unknown error"));
    }
}

static void
egismoc_read_capture_cb (FpDevice *device,
                         guchar   *buffer_in,
                         gsize     length_in,
                         GError   *error)
{
  fp_dbg ("Read capture callback");
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);
  EnrollPrint *enroll_print = fpi_ssm_get_data (self->task_ssm);

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }

  /* Check that the read payload indicates "success" */
  if (egismoc_validate_response_prefix (buffer_in,
                                        length_in,
                                        rsp_read_success_prefix,
                                        rsp_read_success_prefix_len) &&
      egismoc_validate_response_suffix (buffer_in,
                                        length_in,
                                        rsp_read_success_suffix,
                                        rsp_read_success_suffix_len))
    {
      egismoc_enroll_status_report (device, enroll_print,
                                    ENROLL_STATUS_PARTIAL_OK, NULL);
    }
  else
    {
      /* If not success then the sensor can either report "off center" or "sensor is dirty" */

      /* "Off center" */
      if (egismoc_validate_response_prefix (buffer_in,
                                            length_in,
                                            rsp_read_offcenter_prefix,
                                            rsp_read_offcenter_prefix_len) &&
          egismoc_validate_response_suffix (buffer_in,
                                            length_in,
                                            rsp_read_offcenter_suffix,
                                            rsp_read_offcenter_suffix_len))
        error = fpi_device_retry_new (FP_DEVICE_RETRY_CENTER_FINGER);

      /* "Sensor is dirty" */
      else if (egismoc_validate_response_prefix (buffer_in,
                                                 length_in,
                                                 rsp_read_dirty_prefix,
                                                 rsp_read_dirty_prefix_len))
        error = fpi_device_retry_new_msg (FP_DEVICE_RETRY_REMOVE_FINGER,
                                          "Your device is having trouble recognizing you. "
                                          "Make sure your sensor is clean.");

      else
        error = fpi_device_retry_new_msg (FP_DEVICE_RETRY_REMOVE_FINGER,
                                          "Unknown failure trying to read your finger. "
                                          "Please try again.");

      egismoc_enroll_status_report (device, enroll_print, ENROLL_STATUS_RETRY, error);
    }

  if (enroll_print->stage == EGISMOC_ENROLL_TIMES)
    fpi_ssm_next_state (self->task_ssm);
  else
    fpi_ssm_jump_to_state (self->task_ssm, ENROLL_CAPTURE_SENSOR_RESET);
}

static void
egismoc_enroll_check_cb (FpDevice *device,
                         guchar   *buffer_in,
                         gsize     length_in,
                         GError   *error)
{
  fp_dbg ("Enroll check callback");
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }

  /* Check that the read payload reports "not yet enrolled" */
  if (egismoc_validate_response_suffix (buffer_in,
                                        length_in,
                                        rsp_check_not_yet_enrolled_suffix,
                                        rsp_check_not_yet_enrolled_suffix_len))
    fpi_ssm_next_state (self->task_ssm);
  else
    egismoc_enroll_status_report (device, NULL, ENROLL_STATUS_DUPLICATE,
                                  fpi_device_error_new (FP_DEVICE_ERROR_DATA_DUPLICATE));
}

/*
 * Builds the full "check" payload which includes identifiers for all
 * fingerprints which currently should exist on the storage. This payload is
 * used during both enrollment and verify actions.
 */
static guchar *
egismoc_get_check_cmd (FpDevice *device,
                       gsize    *length_out)
{
  fp_dbg ("Get check command");
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);
  g_autofree guchar *result = NULL;
  gsize pos = 0;

  /*
   * The final command body should contain:
   * 1) hard-coded 00 00
   * 2) 2-byte size indiciator, 20*Number enrolled identifiers plus 9 in form of:
   *    (enrolled_num + 1) * 0x20 + 0x09
   *    Since max prints can be higher than 7 then this goes up to 2 bytes
   *    (e9 + 9 = 109)
   * 3) Hard-coded prefix (cmd_check_prefix)
   * 4) 2-byte size indiciator, 20*Number of enrolled identifiers without plus 9
   *    ((enrolled_num + 1) * 0x20)
   * 5) Hard-coded 32 * 0x00 bytes
   * 6) All of the currently registered prints in their 32-byte device identifiers
   *    (enrolled_list)
   * 7) Hard-coded suffix (cmd_check_suffix)
   */

  const gsize body_length = sizeof (guchar) * self->enrolled_num *
                            EGISMOC_FINGERPRINT_DATA_SIZE;

  /* prefix length can depend on the type */
  const gsize check_prefix_length = (fpi_device_get_driver_data (device) &
                                     EGISMOC_DRIVER_CHECK_PREFIX_TYPE2) ?
                                    cmd_check_prefix_type2_len :
                                    cmd_check_prefix_type1_len;

  /* total_length is the 6 various bytes plus all other prefixes/suffixes and
   * the body payload */
  const gsize total_length = (sizeof (guchar) * 6)
                             + check_prefix_length
                             + EGISMOC_CMD_CHECK_SEPARATOR_LENGTH
                             + body_length
                             + cmd_check_suffix_len;

  /* pre-fill entire payload with 00s */
  result = g_new0 (guchar, total_length);

  /* start with 00 00 (just move starting offset up by 2) */
  pos = 2;

  /* Size Counter bytes */
  /* "easiest" way to handle 2-bytes size for counter is to hard-code logic for
   * when we go to the 2nd byte
   * note this will not work in case any model ever supports more than 14 prints
   * (assumed max is 10) */
  if (self->enrolled_num > 6)
    {
      memset (result + pos, 0x01, sizeof (guchar));
      pos += sizeof (guchar);
      memset (result + pos, ((self->enrolled_num - 7) * 0x20) + 0x09,
              sizeof (guchar));
      pos += sizeof (guchar);
    }
  else
    {
      /* first byte is 0x00, just skip it */
      pos += sizeof (guchar);
      memset (result + pos, ((self->enrolled_num + 1) * 0x20) + 0x09,
              sizeof (guchar));
      pos += sizeof (guchar);
    }

  /* command prefix */
  if (fpi_device_get_driver_data (device) & EGISMOC_DRIVER_CHECK_PREFIX_TYPE2)
    {
      memcpy (result + pos, cmd_check_prefix_type2, cmd_check_prefix_type2_len);
      pos += cmd_check_prefix_type2_len;
    }
  else
    {
      memcpy (result + pos, cmd_check_prefix_type1, cmd_check_prefix_type1_len);
      pos += cmd_check_prefix_type1_len;
    }

  /* 2-bytes size logic for counter again */
  if (self->enrolled_num > 6)
    {
      memset (result + pos, 0x01, sizeof (guchar));
      pos += sizeof (guchar);
      memset (result + pos, (self->enrolled_num - 7) * 0x20, sizeof (guchar));
      pos += sizeof (guchar);
    }
  else
    {
      /* first byte is 0x00, just skip it */
      pos += sizeof (guchar);
      memset (result + pos, (self->enrolled_num + 1) * 0x20, sizeof (guchar));
      pos += sizeof (guchar);
    }

  /* add 00s "separator" to offset position */
  pos += EGISMOC_CMD_CHECK_SEPARATOR_LENGTH;

  /* append all currently registered 32-byte fingerprint IDs */
  const gsize print_id_length = sizeof (guchar) * EGISMOC_FINGERPRINT_DATA_SIZE;

  for (int i = 0; i < self->enrolled_num; i++)
    {
      gchar *device_print_id = g_ptr_array_index (self->enrolled_ids, i);
      memcpy (result + pos + (print_id_length * i), device_print_id, print_id_length);
    }
  pos += body_length;

  /* command suffix */
  memcpy (result + pos, cmd_check_suffix, cmd_check_suffix_len);

  if (length_out)
    *length_out = total_length;

  return g_steal_pointer (&result);
}

static void
egismoc_enroll_run_state (FpiSsm   *ssm,
                          FpDevice *device)
{
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);
  EnrollPrint *enroll_print = fpi_ssm_get_data (ssm);
  g_autofree guchar *payload = NULL;
  gsize payload_length = 0;
  g_autofree gchar *device_print_id = NULL;
  g_autofree gchar *user_id = NULL;

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case ENROLL_GET_ENROLLED_IDS:
      /* get enrolled_ids and enrolled_num from device for use in check stages below */
      egismoc_exec_cmd (device, cmd_list, cmd_list_len,
                        NULL, egismoc_list_fill_enrolled_ids_cb);
      break;

    case ENROLL_CHECK_ENROLLED_NUM:
      if (self->enrolled_num >= EGISMOC_MAX_ENROLL_NUM)
        {
          egismoc_enroll_status_report (device, enroll_print, ENROLL_STATUS_DEVICE_FULL,
                                        fpi_device_error_new (FP_DEVICE_ERROR_DATA_FULL));
          return;
        }
      fpi_ssm_next_state (ssm);
      break;

    case ENROLL_SENSOR_RESET:
      egismoc_exec_cmd (device, cmd_sensor_reset, cmd_sensor_reset_len,
                        NULL, egismoc_task_ssm_next_state_cb);
      break;

    case ENROLL_SENSOR_ENROLL:
      egismoc_exec_cmd (device, cmd_sensor_enroll, cmd_sensor_enroll_len,
                        NULL, egismoc_task_ssm_next_state_cb);
      break;

    case ENROLL_WAIT_FINGER:
      egismoc_wait_finger_on_sensor (ssm, device);
      break;

    case ENROLL_SENSOR_CHECK:
      egismoc_exec_cmd (device, cmd_sensor_check, cmd_sensor_check_len,
                        NULL, egismoc_task_ssm_next_state_cb);
      break;

    case ENROLL_CHECK:
      payload = egismoc_get_check_cmd (device, &payload_length);
      egismoc_exec_cmd (device, g_steal_pointer (&payload), payload_length,
                        g_free, egismoc_enroll_check_cb);
      break;

    case ENROLL_START:
      egismoc_exec_cmd (device, cmd_enroll_starting, cmd_enroll_starting_len,
                        NULL, egismoc_task_ssm_next_state_cb);
      break;

    case ENROLL_CAPTURE_SENSOR_RESET:
      egismoc_exec_cmd (device, cmd_sensor_reset, cmd_sensor_reset_len,
                        NULL, egismoc_task_ssm_next_state_cb);
      break;

    case ENROLL_CAPTURE_SENSOR_START_CAPTURE:
      egismoc_exec_cmd (device, cmd_sensor_start_capture, cmd_sensor_start_capture_len,
                        NULL,
                        egismoc_task_ssm_next_state_cb);
      break;

    case ENROLL_CAPTURE_WAIT_FINGER:
      egismoc_wait_finger_on_sensor (ssm, device);
      break;

    case ENROLL_CAPTURE_READ_RESPONSE:
      egismoc_exec_cmd (device, cmd_read_capture, cmd_read_capture_len,
                        NULL, egismoc_read_capture_cb);
      break;

    case ENROLL_COMMIT_START:
      egismoc_exec_cmd (device, cmd_commit_starting, cmd_commit_starting_len,
                        NULL, egismoc_task_ssm_next_state_cb);
      break;

    case ENROLL_COMMIT:
      user_id = fpi_print_generate_user_id (enroll_print->print);
      fp_dbg ("New fingerprint ID: %s", user_id);

      device_print_id = g_strndup (user_id, EGISMOC_FINGERPRINT_DATA_SIZE);
      egismoc_set_print_data (enroll_print->print, device_print_id, user_id);

      /* create new dynamic payload of cmd_new_print_prefix + device_print_id */
      payload_length = cmd_new_print_prefix_len + EGISMOC_FINGERPRINT_DATA_SIZE;
      payload = g_new0 (guchar, payload_length);
      memcpy (payload, cmd_new_print_prefix, cmd_new_print_prefix_len);
      memcpy (payload + cmd_new_print_prefix_len, device_print_id,
              EGISMOC_FINGERPRINT_DATA_SIZE);

      egismoc_exec_cmd (device, g_steal_pointer (&payload), payload_length,
                        g_free, egismoc_task_ssm_next_state_cb);
      break;

    case ENROLL_COMMIT_SENSOR_RESET:
      egismoc_exec_cmd (device, cmd_sensor_reset, cmd_sensor_reset_len,
                        NULL, egismoc_task_ssm_next_state_cb);
      break;

    case ENROLL_COMPLETE:
      egismoc_enroll_status_report (device, enroll_print, ENROLL_STATUS_COMPLETE, NULL);
      fpi_ssm_next_state (ssm);
      break;
    }
}

static void
egismoc_enroll (FpDevice *device)
{
  fp_dbg ("Enroll");
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);
  EnrollPrint *enroll_print = g_new0 (EnrollPrint, 1);

  fpi_device_get_enroll_data (device, &enroll_print->print);
  enroll_print->stage = 0;

  g_assert (self->task_ssm == NULL);
  self->task_ssm = fpi_ssm_new (device, egismoc_enroll_run_state, ENROLL_STATES);
  fpi_ssm_set_data (self->task_ssm, g_steal_pointer (&enroll_print), g_free);
  fpi_ssm_start (self->task_ssm, egismoc_task_ssm_done);
}

static void
egismoc_identify_check_cb (FpDevice *device,
                           guchar   *buffer_in,
                           gsize     length_in,
                           GError   *error)
{
  fp_dbg ("Identify check callback");
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);
  gchar device_print_id[EGISMOC_FINGERPRINT_DATA_SIZE];
  FpPrint *print = NULL;
  FpPrint *verify_print = NULL;
  GPtrArray *prints;
  gboolean found = FALSE;
  guint index;

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }

  /* Check that the read payload indicates "match" */
  if (egismoc_validate_response_suffix (buffer_in,
                                        length_in,
                                        rsp_identify_match_suffix,
                                        rsp_identify_match_suffix_len))
    {
      /*
         On success, there is a 32 byte array of "something"(?) in chars 14-45
         and then the 32 byte array ID of the matched print comes as chars 46-77
       */
      memcpy (device_print_id,
              buffer_in + EGISMOC_IDENTIFY_RESPONSE_PRINT_ID_OFFSET,
              EGISMOC_FINGERPRINT_DATA_SIZE);

      /* Create a new print from this device_print_id and then see if it matches
       * the one indicated
       */
      print = fp_print_new (device);
      egismoc_set_print_data (print, device_print_id, NULL);

      if (!print)
        {
          fpi_ssm_mark_failed (self->task_ssm,
                               fpi_device_error_new_msg (FP_DEVICE_ERROR_DATA_INVALID,
                                                         "Failed to build a print from "
                                                         "device response."));
          return;
        }

      fp_info ("Identify successful for: %s", fp_print_get_description (print));

      if (fpi_device_get_current_action (device) == FPI_DEVICE_ACTION_IDENTIFY)
        {
          fpi_device_get_identify_data (device, &prints);
          found = g_ptr_array_find_with_equal_func (prints,
                                                    print,
                                                    (GEqualFunc) fp_print_equal,
                                                    &index);

          if (found)
            fpi_device_identify_report (device, g_ptr_array_index (prints, index), print, NULL);
          else
            fpi_device_identify_report (device, NULL, print, NULL);
        }
      else
        {
          fpi_device_get_verify_data (device, &verify_print);
          fp_info ("Verifying against: %s", fp_print_get_description (verify_print));

          if (fp_print_equal (verify_print, print))
            fpi_device_verify_report (device, FPI_MATCH_SUCCESS, print, NULL);
          else
            fpi_device_verify_report (device, FPI_MATCH_FAIL, print, NULL);
        }
    }
  /* If device was successfully read but it was a "not matched" */
  else if (egismoc_validate_response_suffix (buffer_in,
                                             length_in,
                                             rsp_identify_notmatch_suffix,
                                             rsp_identify_notmatch_suffix_len))
    {
      fp_info ("Print was not identified by the device");

      if (fpi_device_get_current_action (device) == FPI_DEVICE_ACTION_VERIFY)
        fpi_device_verify_report (device, FPI_MATCH_FAIL, NULL, NULL);
      else
        fpi_device_identify_report (device, NULL, NULL, NULL);
    }
  else
    {
      fpi_ssm_mark_failed (self->task_ssm,
                           fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                     "Unrecognized response from device."));
      return;
    }

  fpi_ssm_next_state (self->task_ssm);
}

static void
egismoc_identify_run_state (FpiSsm   *ssm,
                            FpDevice *device)
{
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);
  g_autofree guchar *payload = NULL;
  gsize payload_length = 0;

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case IDENTIFY_GET_ENROLLED_IDS:
      /* get enrolled_ids and enrolled_num from device for use in check stages below */
      egismoc_exec_cmd (device, cmd_list, cmd_list_len,
                        NULL, egismoc_list_fill_enrolled_ids_cb);
      break;

    case IDENTIFY_CHECK_ENROLLED_NUM:
      if (self->enrolled_num == 0)
        {
          fpi_ssm_mark_failed (g_steal_pointer (&self->task_ssm),
                               fpi_device_error_new (FP_DEVICE_ERROR_DATA_NOT_FOUND));
          return;
        }
      fpi_ssm_next_state (ssm);
      break;

    case IDENTIFY_SENSOR_RESET:
      egismoc_exec_cmd (device, cmd_sensor_reset, cmd_sensor_reset_len,
                        NULL, egismoc_task_ssm_next_state_cb);
      break;

    case IDENTIFY_SENSOR_IDENTIFY:
      egismoc_exec_cmd (device, cmd_sensor_identify, cmd_sensor_identify_len,
                        NULL, egismoc_task_ssm_next_state_cb);
      break;

    case IDENTIFY_WAIT_FINGER:
      egismoc_wait_finger_on_sensor (ssm, device);
      break;

    case IDENTIFY_SENSOR_CHECK:
      egismoc_exec_cmd (device, cmd_sensor_check, cmd_sensor_check_len,
                        NULL, egismoc_task_ssm_next_state_cb);
      break;

    case IDENTIFY_CHECK:
      payload = egismoc_get_check_cmd (device, &payload_length);
      egismoc_exec_cmd (device, g_steal_pointer (&payload), payload_length,
                        g_free, egismoc_identify_check_cb);
      break;

    case IDENTIFY_COMPLETE_SENSOR_RESET:
      egismoc_exec_cmd (device, cmd_sensor_reset, cmd_sensor_reset_len,
                        NULL, egismoc_task_ssm_next_state_cb);
      break;

    /*
     * In Windows, the driver seems at this point to then immediately take
     * another read from the sensor; this is suspected to be an on-chip
     * "verify". However, because the user's finger is still on the sensor from
     * the identify, then it seems to always return positive. We will consider
     * this extra step unnecessary and just skip it in this driver. This driver
     * will instead handle matching of the FpPrint from the gallery in the
     * "verify" case of the callback egismoc_identify_check_cb.
     */
    case IDENTIFY_COMPLETE:
      if (fpi_device_get_current_action (device) == FPI_DEVICE_ACTION_IDENTIFY)
        fpi_device_identify_complete (device, NULL);
      else
        fpi_device_verify_complete (device, NULL);

      fpi_ssm_mark_completed (ssm);
      break;
    }
}

static void
egismoc_identify_verify (FpDevice *device)
{
  fp_dbg ("Identify or Verify");
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);

  g_assert (self->task_ssm == NULL);
  self->task_ssm = fpi_ssm_new (device, egismoc_identify_run_state, IDENTIFY_STATES);
  fpi_ssm_start (self->task_ssm, egismoc_task_ssm_done);
}

static void
egismoc_fw_version_cb (FpDevice *device,
                       guchar   *buffer_in,
                       gsize     length_in,
                       GError   *error)
{
  fp_dbg ("Firmware version callback");
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);
  g_autofree gchar *fw_version = NULL;
  gsize prefix_length;
  guchar *fw_version_start;
  gsize fw_version_length;

  if (error)
    {
      fpi_ssm_mark_failed (self->task_ssm, error);
      return;
    }

  /* Check that the read payload indicates "success" */
  if (!egismoc_validate_response_suffix (buffer_in,
                                         length_in,
                                         rsp_fw_version_suffix,
                                         rsp_fw_version_suffix_len))
    {
      fpi_ssm_mark_failed (self->task_ssm,
                           fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                     "Device firmware response "
                                                     "was not valid."));
      return;
    }

  /*
   * FW Version is 12 bytes: a carriage return (0x0d) plus the version string
   * itself. Always skip [the read prefix] + [2 * check bytes] + [3 * 0x00] that
   * come with every payload Then we will also skip the carriage return and take
   * all but the last 2 bytes as the FW Version
   */
  prefix_length = egismoc_read_prefix_len + 2 + 3 + 1;
  fw_version_start = buffer_in + prefix_length;
  fw_version_length = length_in - prefix_length - rsp_fw_version_suffix_len;
  fw_version = g_strndup ((gchar *) fw_version_start, fw_version_length);

  fp_info ("Device firmware version is %s", fw_version);

  fpi_ssm_next_state (self->task_ssm);
}

static void
egismoc_dev_init_done (FpiSsm   *ssm,
                       FpDevice *device,
                       GError   *error)
{
  if (error)
    {
      g_usb_device_release_interface (
        fpi_device_get_usb_device (device), 0, 0, NULL);
      egismoc_task_ssm_done (ssm, device, error);
      return;
    }

  egismoc_task_ssm_done (ssm, device, NULL);
  fpi_device_open_complete (device, NULL);
}

static void
egismoc_dev_init_handler (FpiSsm   *ssm,
                          FpDevice *device)
{
  g_autoptr(FpiUsbTransfer) transfer = fpi_usb_transfer_new (device);

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case DEV_INIT_CONTROL1:
      fpi_usb_transfer_fill_control (transfer,
                                     G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
                                     G_USB_DEVICE_REQUEST_TYPE_VENDOR,
                                     G_USB_DEVICE_RECIPIENT_DEVICE,
                                     32, 0x0000, 4, 16);
      break;

    case DEV_INIT_CONTROL2:
      fpi_usb_transfer_fill_control (transfer,
                                     G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
                                     G_USB_DEVICE_REQUEST_TYPE_VENDOR,
                                     G_USB_DEVICE_RECIPIENT_DEVICE,
                                     32, 0x0000, 4, 40);
      break;

    case DEV_INIT_CONTROL3:
      fpi_usb_transfer_fill_control (transfer,
                                     G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
                                     G_USB_DEVICE_REQUEST_TYPE_STANDARD,
                                     G_USB_DEVICE_RECIPIENT_DEVICE,
                                     0, 0x0000, 0, 2);
      break;

    case DEV_INIT_CONTROL4:
      fpi_usb_transfer_fill_control (transfer,
                                     G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
                                     G_USB_DEVICE_REQUEST_TYPE_STANDARD,
                                     G_USB_DEVICE_RECIPIENT_DEVICE,
                                     0, 0x0000, 0, 2);
      break;

    case DEV_INIT_CONTROL5:
      fpi_usb_transfer_fill_control (transfer,
                                     G_USB_DEVICE_DIRECTION_DEVICE_TO_HOST,
                                     G_USB_DEVICE_REQUEST_TYPE_VENDOR,
                                     G_USB_DEVICE_RECIPIENT_DEVICE,
                                     82, 0x0000, 0, 8);
      break;

    case DEV_GET_FW_VERSION:
      egismoc_exec_cmd (device, cmd_fw_version, cmd_fw_version_len,
                        NULL, egismoc_fw_version_cb);
      return;

    default:
      g_assert_not_reached ();
    }

  transfer->ssm = ssm;
  transfer->short_is_error = TRUE;
  fpi_usb_transfer_submit (g_steal_pointer (&transfer),
                           EGISMOC_USB_CONTROL_TIMEOUT,
                           fpi_device_get_cancellable (device),
                           fpi_ssm_usb_transfer_cb,
                           NULL);
}

static void
egismoc_open (FpDevice *device)
{
  fp_dbg ("Opening device");
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);
  GError *error = NULL;

  self->interrupt_cancellable = g_cancellable_new ();

  if (!g_usb_device_reset (fpi_device_get_usb_device (device), &error))
    {
      fpi_device_open_complete (device, error);
      return;
    }

  if (!g_usb_device_claim_interface (fpi_device_get_usb_device (device),
                                     0, 0, &error))
    {
      fpi_device_open_complete (device, error);
      return;
    }

  g_assert (self->task_ssm == NULL);
  self->task_ssm = fpi_ssm_new (device, egismoc_dev_init_handler, DEV_INIT_STATES);
  fpi_ssm_start (self->task_ssm, egismoc_dev_init_done);
}

static void
egismoc_cancel (FpDevice *device)
{
  fp_dbg ("Cancel");
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);

  g_cancellable_cancel (self->interrupt_cancellable);
  g_clear_object (&self->interrupt_cancellable);
  self->interrupt_cancellable = g_cancellable_new ();
}

static void
egismoc_suspend (FpDevice *device)
{
  fp_dbg ("Suspend");

  egismoc_cancel (device);
  g_cancellable_cancel (fpi_device_get_cancellable (device));
  fpi_device_suspend_complete (device, NULL);
}

static void
egismoc_close (FpDevice *device)
{
  fp_dbg ("Closing device");
  FpiDeviceEgisMoc *self = FPI_DEVICE_EGISMOC (device);
  GError *error = NULL;

  egismoc_cancel (device);
  g_clear_object (&self->interrupt_cancellable);

  g_usb_device_release_interface (fpi_device_get_usb_device (device),
                                  0, 0, &error);
  fpi_device_close_complete (device, error);
}

static void
fpi_device_egismoc_init (FpiDeviceEgisMoc *self)
{
  G_DEBUG_HERE ();
}

static void
fpi_device_egismoc_class_init (FpiDeviceEgisMocClass *klass)
{
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);

  dev_class->id = FP_COMPONENT;
  dev_class->full_name = EGISMOC_DRIVER_FULLNAME;

  dev_class->type = FP_DEVICE_TYPE_USB;
  dev_class->scan_type = FP_SCAN_TYPE_PRESS;
  dev_class->id_table = egismoc_id_table;
  dev_class->nr_enroll_stages = EGISMOC_ENROLL_TIMES;
  /* device should be "always off" unless being used */
  dev_class->temp_hot_seconds = 0;

  dev_class->open = egismoc_open;
  dev_class->cancel = egismoc_cancel;
  dev_class->suspend = egismoc_suspend;
  dev_class->close = egismoc_close;
  dev_class->identify = egismoc_identify_verify;
  dev_class->verify = egismoc_identify_verify;
  dev_class->enroll = egismoc_enroll;
  dev_class->delete = egismoc_delete;
  dev_class->clear_storage = egismoc_clear_storage;
  dev_class->list = egismoc_list;

  fpi_device_class_auto_initialize_features (dev_class);
  dev_class->features |= FP_DEVICE_FEATURE_DUPLICATES_CHECK;
}
