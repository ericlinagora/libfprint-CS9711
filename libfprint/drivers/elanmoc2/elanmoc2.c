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

#define FP_COMPONENT "elanmoc2"

// Library includes
#include <glib.h>
#include <sys/param.h>

// Local includes
#include "drivers_api.h"

#include "elanmoc2.h"

struct _FpiDeviceElanMoC2
{
  FpDevice parent;

  /* Device properties */
  unsigned short dev_type;

  /* USB response data */
  unsigned char *buffer_in;
  gssize         buffer_in_len;

  /* Command status data */
  FpiSsm       *ssm;
  unsigned char enrolled_num;
  unsigned char print_index;
  GPtrArray    *list_result;

  // Enroll
  gint     enroll_stage;
  FpPrint *enroll_print;
};

G_DEFINE_TYPE (FpiDeviceElanMoC2, fpi_device_elanmoc2, FP_TYPE_DEVICE);


static void
elanmoc2_cmd_usb_receive_callback (FpiUsbTransfer *transfer, FpDevice *device, gpointer user_data, GError *error)
{
  FpiDeviceElanMoC2 *self = FPI_DEVICE_ELANMOC2 (device);

  if (self->ssm == NULL)
    {
      fp_info ("Received USB callback with no ongoing action");
      if (error)
        {
          fp_info ("USB callback error: %s", error->message);
          g_error_free (error);
        }
      return;
    }

  if (error)
    {
      fpi_ssm_mark_failed (g_steal_pointer (&self->ssm), error);
    }
  else if (transfer->actual_length > 0 && transfer->buffer[0] != 0x40)
    {
      fpi_ssm_mark_failed (g_steal_pointer (&self->ssm), fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                                                   "Error receiving data from sensor"));
    }
  else
    {
      self->buffer_in = g_malloc0 (transfer->actual_length + 1);
      memcpy (self->buffer_in, transfer->buffer, transfer->actual_length);
      self->buffer_in_len = transfer->actual_length;
      fpi_ssm_next_state (self->ssm);
    }
}

static gboolean
elanmoc2_cmd_send_sync (FpDevice *device, const struct elanmoc2_cmd *cmd, guint8 *buffer_out, GError **error)
{
  g_autoptr(FpiUsbTransfer) transfer_out = fpi_usb_transfer_new (device);
  transfer_out->short_is_error = TRUE;
  fpi_usb_transfer_fill_bulk_full (transfer_out, ELANMOC2_EP_CMD_OUT, g_steal_pointer (&buffer_out), cmd->out_len,
                                   g_free);
  return fpi_usb_transfer_submit_sync (transfer_out, ELANMOC2_USB_SEND_TIMEOUT, error);
}

static void
elanmoc2_cmd_transceive (FpDevice *device, FpiSsm *ssm, const struct elanmoc2_cmd *cmd, guint8 *buffer_out)
{
  FpiDeviceElanMoC2 *self = FPI_DEVICE_ELANMOC2 (device);

  GError *error = NULL;
  gboolean send_status = elanmoc2_cmd_send_sync (device, cmd, g_steal_pointer (&buffer_out), &error);

  if (!send_status)
    return fpi_ssm_mark_failed (g_steal_pointer (&self->ssm), error);

  if (cmd->in_len == 0)
    // Nothing to receive
    return;

  FpiUsbTransfer *transfer_in = fpi_usb_transfer_new (device);

  transfer_in->short_is_error = TRUE;

  fpi_usb_transfer_fill_bulk (transfer_in, cmd->ep_in, cmd->in_len);
  fpi_usb_transfer_submit (transfer_in,
                           ELANMOC2_USB_RECV_TIMEOUT,
                           cmd->cancellable ? fpi_device_get_cancellable (device) : NULL,
                           elanmoc2_cmd_usb_receive_callback,
                           NULL);
}

static uint8_t *
elanmoc2_prepare_cmd (FpiDeviceElanMoC2 *self, const struct elanmoc2_cmd *cmd)
{
  if (cmd->devices != ELANMOC2_ALL_DEV && !(cmd->devices & self->dev_type))
    return NULL;

  g_autofree uint8_t *buffer = g_malloc0 (cmd->out_len);
  buffer[0] = 0x40;
  memcpy (&buffer[1], cmd->cmd, cmd->is_single_byte_command ? 1 : 2);
  return g_steal_pointer (&buffer);
}

static void
elanmoc2_print_set_data (FpPrint *print, guchar finger_id, guchar user_id_len, const guchar *user_id)
{
  fpi_print_set_type (print, FPI_PRINT_RAW);
  fpi_print_set_device_stored (print, TRUE);

  GVariant *user_id_v = g_variant_new_fixed_array (G_VARIANT_TYPE_BYTE, user_id, user_id_len, sizeof (guchar));
  GVariant *fpi_data = g_variant_new ("(y@ay)", finger_id, user_id_v);
  g_object_set (print, "fpi-data", fpi_data, NULL);
}

static void
elanmoc2_print_get_data (FpPrint *print, guchar *finger_id, guchar *user_id_len, const guchar **user_id)
{
  g_autoptr(GVariant) fpi_data = NULL;
  g_autoptr(GVariant) user_id_v = NULL;

  g_object_get (print, "fpi-data", &fpi_data, NULL);
  g_assert_nonnull (fpi_data);

  g_variant_get (fpi_data, "(y@ay)", finger_id, &user_id_v);
  g_assert_nonnull (user_id_v);

  gsize user_id_len_s = 0;
  gconstpointer user_id_tmp = g_variant_get_fixed_array (user_id_v, &user_id_len_s, sizeof (guchar));
  g_assert (user_id_len_s <= 255);

  *user_id_len = user_id_len_s;
  *user_id = g_memdup (user_id_tmp, user_id_len_s);
}

static FpPrint *
elanmoc2_print_new_with_user_id (FpiDeviceElanMoC2 *self, guchar finger_id, guchar user_id_len, const guchar *user_id)
{
  FpPrint *print = fp_print_new (FP_DEVICE (self));

  elanmoc2_print_set_data (print, finger_id, user_id_len, user_id);
  return g_steal_pointer (&print);
}

static void
elanmoc2_get_user_id_string (FpiDeviceElanMoC2 *self, const guint8 *finger_info_response, guint8 *user_id, guint8 max_len)
{
  if (self->dev_type == ELANMOC2_DEV_0C5E)
    memcpy (user_id, &finger_info_response[3], max_len);
  else
    memcpy (user_id, &finger_info_response[2], max_len);

  user_id[max_len] = '\0';
}

static FpPrint *
elanmoc2_print_new_from_finger_info (FpiDeviceElanMoC2 *self, guint8 finger_id, const guint8 *finger_info_response)
{
  g_autofree guint8 *user_id = g_malloc (ELANMOC2_USER_ID_MAX_LEN + 1);
  guint user_id_max_len = self->dev_type == ELANMOC2_DEV_0C5E ?
                          ELANMOC2_USER_ID_MAX_LEN_0C5E :
                          ELANMOC2_USER_ID_MAX_LEN;

  elanmoc2_get_user_id_string (self, finger_info_response, user_id, user_id_max_len);

  guint8 user_id_len = user_id_max_len;

  if (g_str_has_prefix ((const gchar *) user_id, "FP1-"))
    {
      user_id_len = strnlen ((const char *) user_id, user_id_max_len);
      fp_info ("Creating new print: finger %d, user id[%d]: %s", finger_id, user_id_len, user_id);
    }
  else
    {
      fp_info ("Creating new print: finger %d, user id[%d]: raw data", finger_id, user_id_len);
    }

  FpPrint *print = elanmoc2_print_new_with_user_id (self, finger_id, user_id_len, user_id);

  if (!fpi_print_fill_from_user_id (print, (const char *) user_id))
    {
      // Fingerprint matched with on-sensor print, but the on-sensor print was not added by libfprint.
      // Wipe it and report a failure.
      fp_info ("Finger info not generated by libfprint");
    }
  else
    {
      fp_info ("Finger info with libfprint user ID");
    }

  return g_steal_pointer (&print);
}

static gboolean
elanmoc2_finger_info_is_present (FpiDeviceElanMoC2 *self, const guint8 *finger_info_response)
{
  // Report true if the user ID was set by libfprint. This is not accurate since after wiping the sensor the user IDs
  // are not reset.
  const gchar *user_id = self->dev_type == ELANMOC2_DEV_0C5E ?
                         (gchar *) &finger_info_response[3] :
                         (gchar *) &finger_info_response[2];

  return memcmp (user_id, "FP1-", 4) == 0;
}


static void
elanmoc2_cancel (FpDevice *device)
{
  FpiDeviceElanMoC2 *self = FPI_DEVICE_ELANMOC2 (device);

  fp_info ("Cancelling any ongoing requests");

  GError *error = NULL;
  g_autofree uint8_t *buffer_out = elanmoc2_prepare_cmd (self, &cmd_abort);

  elanmoc2_cmd_send_sync (device, &cmd_abort, g_steal_pointer (&buffer_out), &error);

  if (error)
    {
      fp_warn ("Error while cancelling action: %s", error->message);
      g_clear_error (&error);
    }
}

static void
elanmoc2_open (FpDevice *device)
{
  GError *error = NULL;
  FpiDeviceElanMoC2 *self;

  if (!g_usb_device_reset (fpi_device_get_usb_device (device), &error))
    return fpi_device_open_complete (device, error);

  if (!g_usb_device_claim_interface (fpi_device_get_usb_device (FP_DEVICE (device)), 0, 0, &error))
    return fpi_device_open_complete (device, error);

  self = FPI_DEVICE_ELANMOC2 (device);
  self->dev_type = fpi_device_get_driver_data (FP_DEVICE (device));
  fpi_device_open_complete (device, NULL);
}

static void
elanmoc2_close (FpDevice *device)
{
  GError *error = NULL;

  fp_info ("Closing device");
  elanmoc2_cancel (device);
  g_usb_device_release_interface (fpi_device_get_usb_device (FP_DEVICE (device)), 0, 0, &error);
  fpi_device_close_complete (device, error);
}

static void
elanmoc2_ssm_completed_callback (FpiSsm *ssm, FpDevice *device, GError *error)
{
  if (error)
    fpi_device_action_error (device, error);
}

static void
elanmoc2_perform_get_num_enrolled (FpiDeviceElanMoC2 *self, FpiSsm *ssm)
{
  g_autofree uint8_t *buffer_out = NULL;

  if ((buffer_out = elanmoc2_prepare_cmd (self, &cmd_get_enrolled_count)) == NULL)
    {
      fpi_ssm_next_state (ssm);
      return;
    }
  elanmoc2_cmd_transceive (FP_DEVICE (self), ssm, &cmd_get_enrolled_count, g_steal_pointer (&buffer_out));
  fp_info ("Sent query for number of enrolled fingers");
}

/**
 * Checks a command status code and, if an error has occurred, creates a new error object.
 * Returns whether the operation needs to be retried..
 * @param self FpiDeviceElanMoC2 pointer
 * @param error A pointer where the GError will be placed in case of errors
 * @return Whether the current action should be retried
 */
static gboolean
elanmoc2_get_finger_error (FpiDeviceElanMoC2 *self, GError **error)
{
  g_assert (self->buffer_in != NULL);

  // Regular status codes never have the most-significant nibble set; errors do
  if ((self->buffer_in[1] & 0xF0) == 0)
    {
      *error = NULL;
      return TRUE;
    }
  switch ((unsigned char) self->buffer_in[1])
    {
    case ELANMOC2_RESP_MOVE_DOWN:
      *error = fpi_device_retry_new_msg (FP_DEVICE_RETRY_CENTER_FINGER,
                                         "Move your finger slightly downwards");
      return TRUE;

    case ELANMOC2_RESP_MOVE_RIGHT:
      *error = fpi_device_retry_new_msg (FP_DEVICE_RETRY_CENTER_FINGER,
                                         "Move your finger slightly to the right");
      return TRUE;

    case ELANMOC2_RESP_MOVE_UP:
      *error = fpi_device_retry_new_msg (FP_DEVICE_RETRY_CENTER_FINGER,
                                         "Move your finger slightly upwards");
      return TRUE;

    case ELANMOC2_RESP_MOVE_LEFT:
      *error = fpi_device_retry_new_msg (FP_DEVICE_RETRY_CENTER_FINGER,
                                         "Move your finger slightly to the left");
      return TRUE;

    case ELANMOC2_RESP_SENSOR_DIRTY:
      *error = fpi_device_retry_new_msg (FP_DEVICE_RETRY_REMOVE_FINGER,
                                         "Sensor is dirty or wet");
      return TRUE;

    case ELANMOC2_RESP_NOT_ENOUGH_SURFACE:
      *error = fpi_device_retry_new_msg (FP_DEVICE_RETRY_REMOVE_FINGER,
                                         "Press your finger slightly harder on the sensor");
      return TRUE;

    case ELANMOC2_RESP_NOT_ENROLLED:
      *error = fpi_device_error_new_msg (FP_DEVICE_ERROR_DATA_NOT_FOUND,
                                         "Finger not recognized");
      return FALSE;

    case ELANMOC2_RESP_MAX_ENROLLED_REACHED:
      *error = fpi_device_error_new_msg (FP_DEVICE_ERROR_DATA_FULL,
                                         "Maximum number of fingers already enrolled");
      return FALSE;

    default:
      *error = fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                         "Unknown error");
      return FALSE;
    }
}

static void
elanmoc2_identify_verify_complete (FpDevice *device, GError *error)
{
  if (fpi_device_get_current_action (device) == FPI_DEVICE_ACTION_IDENTIFY)
    fpi_device_identify_complete (device, error);
  else
    fpi_device_verify_complete (device, error);
}

/**
 * Calls the correct verify or identify report function based on the input data.
 * Returns whether the action should be completed.
 * @param device FpDevice
 * @param print Identified fingerprint
 * @param error Optional error
 * @return Whether to complete the action.
 */
static gboolean
elanmoc2_identify_verify_report (FpDevice *device, FpPrint *print, GError **error)
{
  if (*error != NULL && (*error)->domain != FP_DEVICE_RETRY)
    return TRUE;

  if (fpi_device_get_current_action (device) == FPI_DEVICE_ACTION_IDENTIFY)
    {
      if (print != NULL)
        {
          g_autoptr(GPtrArray) gallery = NULL;
          fpi_device_get_identify_data (device, &gallery);
          g_ptr_array_ref (gallery);

          for (int i = 0; i < gallery->len; i++)
            {
              FpPrint *to_match = g_ptr_array_index (gallery, i);
              if (fp_print_equal (to_match, print))
                {
                  fp_info ("Identify: finger matches");
                  fpi_device_identify_report (device, to_match, print, NULL);
                  return TRUE;
                }
            }
          fp_info ("Identify: no match");
          g_clear_pointer (&print, g_object_unref);
        }
      fpi_device_identify_report (device, NULL, NULL, *error);
      return TRUE;
    }
  else
    {
      FpiMatchResult result = FPI_MATCH_FAIL;
      if (print != NULL)
        {
          FpPrint *to_match = NULL;
          fpi_device_get_verify_data (device, &to_match);
          g_assert_nonnull (to_match);

          if (fp_print_equal (to_match, print))
            {
              fp_info ("Verify: finger matches");
              result = FPI_MATCH_SUCCESS;
            }
          else
            {
              fp_info ("Verify: finger does not match");
              g_clear_pointer (&print, g_object_unref);
            }
        }
      fpi_device_verify_report (device, result, print, *error);
      return result != FPI_MATCH_FAIL;
    }
}

static void
elanmoc2_identify_run_state (FpiSsm *ssm, FpDevice *device)
{
  FpiDeviceElanMoC2 *self = FPI_DEVICE_ELANMOC2 (device);
  g_autofree uint8_t *buffer_out = NULL;
  GError *error = NULL;

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case IDENTIFY_GET_NUM_ENROLLED: {
        elanmoc2_perform_get_num_enrolled (self, ssm);
        break;
      }

    case IDENTIFY_CHECK_NUM_ENROLLED: {
        self->enrolled_num = self->buffer_in[1];
        if (self->enrolled_num == 0)
          {
            fp_info ("No fingers enrolled, no need to identify finger");
            error = NULL;
            elanmoc2_identify_verify_report (device, NULL, &error);
            elanmoc2_identify_verify_complete (device, NULL);
            fpi_ssm_mark_completed (g_steal_pointer (&self->ssm));
            break;
          }
        fpi_ssm_next_state (ssm);
      }

    case IDENTIFY_IDENTIFY: {
        if ((buffer_out = elanmoc2_prepare_cmd (self, &cmd_identify)) == NULL)
          {
            fpi_ssm_next_state (ssm);
            break;
          }
        elanmoc2_cmd_transceive (device, ssm, &cmd_identify, g_steal_pointer (&buffer_out));
        fpi_device_report_finger_status (device, FP_FINGER_STATUS_NEEDED);
        fp_info ("Sent identification request");
        break;
      }

    case IDENTIFY_GET_FINGER_INFO: {
        fpi_device_report_finger_status (device, FP_FINGER_STATUS_PRESENT);
        error = NULL;
        gboolean retry = elanmoc2_get_finger_error (self, &error);
        if (error != NULL)
          {
            fp_info ("Identify failed: %s", error->message);
            if (retry)
              {
                elanmoc2_identify_verify_report (device, NULL, &error);
                fpi_ssm_jump_to_state (ssm, IDENTIFY_IDENTIFY);
              }
            else
              {
                elanmoc2_identify_verify_complete (device, g_steal_pointer (&error));
                fpi_ssm_mark_completed (g_steal_pointer (&self->ssm));
              }
            break;
          }
        self->print_index = self->buffer_in[1];
        fp_info ("Identified finger %d; requesting finger info", self->print_index);
        if ((buffer_out = elanmoc2_prepare_cmd (self, &cmd_finger_info)) == NULL)
          {
            fpi_ssm_next_state (ssm);
            break;
          }
        buffer_out[3] = self->print_index;
        elanmoc2_cmd_transceive (device, ssm, &cmd_finger_info, g_steal_pointer (&buffer_out));
        break;
      }

    case IDENTIFY_CHECK_FINGER_INFO: {
        fpi_device_report_finger_status (device, FP_FINGER_STATUS_NONE);

        FpPrint *print = elanmoc2_print_new_from_finger_info (self, self->print_index, self->buffer_in);

        error = NULL;
        if (elanmoc2_identify_verify_report (device, g_steal_pointer (&print), &error))
          {
            elanmoc2_identify_verify_complete (device, error);
            fpi_ssm_mark_completed (g_steal_pointer (&self->ssm));
          }
        else
          {
            fpi_ssm_jump_to_state (ssm, IDENTIFY_IDENTIFY);
          }

        break;
      }

    default:
      break;
    }

  g_clear_pointer (&self->buffer_in, g_free);
}

static void
elanmoc2_identify_verify (FpDevice *device)
{
  FpiDeviceElanMoC2 *self = FPI_DEVICE_ELANMOC2 (device);

  fp_info ("[elanmoc2] New identify/verify operation");
  self->ssm = fpi_ssm_new (device, elanmoc2_identify_run_state, IDENTIFY_NUM_STATES);
  fpi_ssm_start (self->ssm, elanmoc2_ssm_completed_callback);
}

static void
elanmoc2_list_ssm_completed_callback (FpiSsm *ssm, FpDevice *device, GError *error)
{
  FpiDeviceElanMoC2 *self = FPI_DEVICE_ELANMOC2 (device);

  g_clear_pointer (&self->list_result, g_ptr_array_unref);
  elanmoc2_ssm_completed_callback (ssm, device, error);
}

static void
elanmoc2_list_run_state (FpiSsm *ssm, FpDevice *device)
{
  FpiDeviceElanMoC2 *self = FPI_DEVICE_ELANMOC2 (device);
  g_autofree uint8_t *buffer_out = NULL;

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case LIST_GET_NUM_ENROLLED:
      elanmoc2_perform_get_num_enrolled (self, ssm);
      break;

    case LIST_CHECK_NUM_ENROLLED:
      self->enrolled_num = self->buffer_in[1];
      fp_info ("List: fingers enrolled: %d", self->enrolled_num);
      if (self->enrolled_num == 0)
        {
          fpi_device_list_complete (device, g_steal_pointer (&self->list_result), NULL);
          fpi_ssm_mark_completed (g_steal_pointer (&self->ssm));
          break;
        }
      self->print_index = 0;
      fpi_ssm_next_state (ssm);
      break;

    case LIST_GET_FINGER_INFO:
      if ((buffer_out = elanmoc2_prepare_cmd (self, &cmd_finger_info)) == NULL)
        {
          fpi_ssm_next_state (ssm);
          break;
        }
      buffer_out[3] = self->print_index;
      elanmoc2_cmd_transceive (device, ssm, &cmd_finger_info, g_steal_pointer (&buffer_out));
      fp_info ("Sent get finger info command for finger %d", self->print_index);
      break;

    case LIST_CHECK_FINGER_INFO:
      fpi_device_report_finger_status (device, FP_FINGER_STATUS_NONE);
      fp_info ("Successfully retrieved finger info for %d", self->print_index);

      if (elanmoc2_finger_info_is_present (self, self->buffer_in))
        {
          FpPrint *print = elanmoc2_print_new_from_finger_info (self, self->print_index, self->buffer_in);
          g_ptr_array_add (self->list_result, g_object_ref_sink (print));
        }

      if (++(self->print_index) < ELANMOC2_MAX_PRINTS)
        {
          fpi_ssm_jump_to_state (ssm, LIST_GET_FINGER_INFO);
        }
      else
        {
          fpi_device_list_complete (device, g_steal_pointer (&self->list_result), NULL);
          fpi_ssm_mark_completed (g_steal_pointer (&self->ssm));
        }
      break;
    }

  g_clear_pointer (&self->buffer_in, g_free);
}

static void
elanmoc2_list (FpDevice *device)
{
  FpiDeviceElanMoC2 *self = FPI_DEVICE_ELANMOC2 (device);

  fp_info ("[elanmoc2] New list operation");
  self->ssm = fpi_ssm_new (device, elanmoc2_list_run_state, LIST_NUM_STATES);
  self->list_result = g_ptr_array_new_with_free_func (g_object_unref);
  fpi_ssm_start (self->ssm, elanmoc2_list_ssm_completed_callback);
}

static void
elanmoc2_enroll_ssm_completed_callback (FpiSsm *ssm, FpDevice *device, GError *error)
{
  FpiDeviceElanMoC2 *self = FPI_DEVICE_ELANMOC2 (device);

  // Pointer is either stolen by fpi_device_enroll_complete() or otherwise unref'd by libfprint behind the scenes.
  self->enroll_print = NULL;
  elanmoc2_ssm_completed_callback (ssm, device, error);
}

static void
elanmoc2_enroll_run_state (FpiSsm *ssm, FpDevice *device)
{
  FpiDeviceElanMoC2 *self = FPI_DEVICE_ELANMOC2 (device);

  g_assert_nonnull (self->enroll_print);

  g_autofree uint8_t *buffer_out = NULL;
  GError *error = NULL;

  switch (fpi_ssm_get_cur_state (ssm))
    {
    // First check how many fingers are already enrolled
    case ENROLL_GET_NUM_ENROLLED: {
        elanmoc2_perform_get_num_enrolled (self, ssm);
        break;
      }

    case ENROLL_CHECK_NUM_ENROLLED: {
        self->enrolled_num = self->buffer_in[1];
        if (self->enrolled_num >= ELANMOC2_MAX_PRINTS)
          {
            fp_info ("Can't enroll, sensor storage is full");
            error = fpi_device_error_new_msg (FP_DEVICE_ERROR_DATA_FULL,
                                              "Sensor storage is full");
            fpi_device_enroll_complete (device, NULL, g_steal_pointer (&error));
            fpi_ssm_mark_completed (g_steal_pointer (&self->ssm));
          }
        else if (self->enrolled_num == 0)
          {
            fp_info ("Enrolled count is 0, proceeding with enroll stage");
            fpi_ssm_jump_to_state (ssm, ENROLL_ENROLL);
          }
        else
          {
            fp_info ("Fingers enrolled: %d, need to check for re-enroll", self->enrolled_num);
            fpi_ssm_next_state (ssm);
          }
        break;
      }

    case ENROLL_EARLY_REENROLL_CHECK: {
        if ((buffer_out = elanmoc2_prepare_cmd (self, &cmd_identify)) == NULL)
          {
            fpi_ssm_next_state (ssm);
            break;
          }
        elanmoc2_cmd_transceive (device, ssm, &cmd_identify, g_steal_pointer (&buffer_out));
        fpi_device_report_finger_status (device, FP_FINGER_STATUS_NEEDED);
        fp_info ("Sent identification request");
        break;
      }

    case ENROLL_GET_ENROLLED_FINGER_INFO: {
        fpi_device_report_finger_status (device, FP_FINGER_STATUS_PRESENT);

        // Not enrolled - skip to enroll stage
        if (self->buffer_in[1] == ELANMOC2_RESP_NOT_ENROLLED)
          {
            fp_info ("Finger not enrolled, proceeding with enroll stage");
            fpi_device_enroll_progress (device, self->enroll_stage, NULL, NULL);
            fpi_ssm_jump_to_state (ssm, ENROLL_ENROLL);
            break;
          }

        // Identification failed (i.e. dirty) - retry
        error = NULL;
        gboolean retry = elanmoc2_get_finger_error (self, &error);
        if (error != NULL)
          {
            fp_info ("Identify failed: %s", error->message);
            if (retry)
              {
                fpi_device_enroll_progress (device, self->enroll_stage, NULL, error);
                fpi_ssm_jump_to_state (ssm, ENROLL_EARLY_REENROLL_CHECK);
              }
            else
              {
                fpi_device_enroll_complete (device, NULL, g_steal_pointer (&error));
                fpi_ssm_mark_completed (g_steal_pointer (&self->ssm));
                self->enroll_print = NULL;
              }
            break;
          }

        // Finger already enrolled - fetch finger info for deletion
        self->print_index = self->buffer_in[1];
        fp_info ("Finger enrolled as %d; fetching finger info", self->print_index);
        if ((buffer_out = elanmoc2_prepare_cmd (self, &cmd_finger_info)) == NULL)
          {
            fpi_ssm_next_state (ssm);
            break;
          }
        buffer_out[3] = self->print_index;
        elanmoc2_cmd_transceive (device, ssm, &cmd_finger_info, g_steal_pointer (&buffer_out));
        break;
      }

    case ENROLL_ATTEMPT_DELETE: {
        fpi_device_report_finger_status (device, FP_FINGER_STATUS_NONE);
        fp_info ("Deleting enrolled finger %d", self->print_index);

        // Attempt to delete the finger
        g_autofree guint8 *user_id = g_malloc (ELANMOC2_USER_ID_MAX_LEN + 1);
        elanmoc2_get_user_id_string (self, self->buffer_in, user_id, ELANMOC2_USER_ID_MAX_LEN);

        if ((buffer_out = elanmoc2_prepare_cmd (self, &cmd_delete)) == NULL)
          {
            fpi_ssm_next_state (ssm);
            break;
          }
        buffer_out[3] = 0xf0 | (self->print_index + 5);
        memcpy ((char *) &buffer_out[4], (char *) user_id, MIN (cmd_delete.out_len - 4, ELANMOC2_USER_ID_MAX_LEN));
        elanmoc2_cmd_transceive (device, ssm, &cmd_delete, g_steal_pointer (&buffer_out));

        break;
      }

    case ENROLL_CHECK_DELETED: {
        if (self->buffer_in[1] != 0)
          {
            fp_info ("Failed to delete finger %d, wiping sensor", self->print_index);
            fpi_ssm_jump_to_state (ssm, ENROLL_WIPE_SENSOR);
          }
        else
          {
            fp_info ("Finger %d deleted, proceeding with enroll stage", self->print_index);
            self->enrolled_num--;
            fpi_device_enroll_progress (device, self->enroll_stage, NULL, NULL);
            fpi_ssm_jump_to_state (ssm, ENROLL_ENROLL);
          }
        break;
      }

    case ENROLL_WIPE_SENSOR: {
        if ((buffer_out = elanmoc2_prepare_cmd (self, &cmd_wipe_sensor)) == NULL)
          {
            fpi_ssm_next_state (ssm);
            break;
          }
        elanmoc2_cmd_transceive (device, ssm, &cmd_wipe_sensor, g_steal_pointer (&buffer_out));
        self->enrolled_num = 0;
        self->print_index = 0;
        fp_info ("Wipe sensor command sent - next operation will take a while");
        fpi_ssm_next_state (ssm);
        break;
      }

    case ENROLL_ENROLL: {
        if ((buffer_out = elanmoc2_prepare_cmd (self, &cmd_enroll)) == NULL)
          {
            fpi_ssm_next_state (ssm);
            break;
          }
        buffer_out[3] = self->enrolled_num;
        buffer_out[4] = ELANMOC2_ENROLL_TIMES;
        buffer_out[5] = self->enroll_stage;
        buffer_out[6] = 0;
        elanmoc2_cmd_transceive (device, ssm, &cmd_enroll, g_steal_pointer (&buffer_out));
        fp_info ("Enroll command sent: %d/%d", self->enroll_stage, ELANMOC2_ENROLL_TIMES);
        fpi_device_report_finger_status (device, FP_FINGER_STATUS_NEEDED);
        break;
      }

    case ENROLL_CHECK_ENROLLED: {
        fpi_device_report_finger_status (device, FP_FINGER_STATUS_PRESENT);

        if (self->buffer_in[1] == 0)
          {
            // Stage okay
            fp_info ("Enroll stage succeeded");
            self->enroll_stage++;
            fpi_device_enroll_progress (device, self->enroll_stage, self->enroll_print, NULL);
            if (self->enroll_stage >= ELANMOC2_ENROLL_TIMES)
              {
                fp_info ("Enroll completed");
                fpi_ssm_next_state (ssm);
                break;
              }
          }
        else
          {
            // Detection error
            error = NULL;
            gboolean retry = elanmoc2_get_finger_error (self, &error);
            if (error != NULL)
              {
                fp_info ("Enroll stage failed: %s", error->message);
                if (self->buffer_in[1] == ELANMOC2_RESP_NOT_ENROLLED)
                  {
                    // Not enrolled is a fatal error for "identify" but not for "enroll"
                    error->domain = FP_DEVICE_RETRY;
                    error->code = FP_DEVICE_RETRY_TOO_SHORT;
                    retry = false;
                  }
                if (retry)
                  {
                    fpi_device_enroll_progress (device, self->enroll_stage, NULL, error);
                  }
                else
                  {
                    fpi_device_enroll_complete (device, NULL, g_steal_pointer (&error));
                    fpi_ssm_mark_completed (g_steal_pointer (&self->ssm));
                  }
              }
            else
              {
                fp_info ("Enroll stage failed for unknown reasons");
              }
          }
        fp_info ("Performing another enroll");
        fpi_ssm_jump_to_state (ssm, ENROLL_ENROLL);
        break;
      }

    case ENROLL_LATE_REENROLL_CHECK: {
        fpi_device_report_finger_status (device, FP_FINGER_STATUS_NONE);
        if ((buffer_out = elanmoc2_prepare_cmd (self, &cmd_check_enroll_collision)) == NULL)
          {
            fpi_ssm_next_state (ssm);
            break;
          }
        elanmoc2_cmd_transceive (device, ssm, &cmd_check_enroll_collision, g_steal_pointer (&buffer_out));
        fp_info ("Check re-enroll command sent");
        break;
      }

    case ENROLL_COMMIT: {
        error = NULL;
        if (self->buffer_in[1] != 0)
          {
            fp_info ("Finger is already enrolled at position %d, cannot commit", self->buffer_in[2]);
            error = fpi_device_error_new_msg (FP_DEVICE_ERROR_DATA_DUPLICATE,
                                              "Finger is already enrolled");
            fpi_device_enroll_complete (device, NULL, g_steal_pointer (&error));
            fpi_ssm_mark_completed (g_steal_pointer (&self->ssm));
            self->enroll_print = NULL;
            break;
          }

        fp_info ("Finger is not enrolled, committing");
        if ((buffer_out = elanmoc2_prepare_cmd (self, &cmd_commit)) == NULL)
          {
            fpi_ssm_next_state (ssm);
            break;
          }
        g_autofree gchar *user_id = fpi_print_generate_user_id (self->enroll_print);
        elanmoc2_print_set_data (self->enroll_print, self->enrolled_num, strlen (user_id), (guint8 *) user_id);

        buffer_out[3] = 0xf0 | (self->enrolled_num + 5);
        strncpy ((char *) &buffer_out[4], user_id, cmd_commit.out_len - 4);
        elanmoc2_cmd_transceive (device, ssm, &cmd_commit, g_steal_pointer (&buffer_out));
        fp_info ("Commit command sent");
        break;
      }

    case ENROLL_CHECK_COMMITTED: {
        error = NULL;
        if (self->buffer_in[1] != 0)
          {
            fp_info ("Commit failed with error code %d", self->buffer_in[1]);
            error = fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                              "Failed to store fingerprint for unknown reasons");
            fpi_device_enroll_complete (device, NULL, error);
            fpi_ssm_mark_failed (g_steal_pointer (&self->ssm), error);
          }
        else
          {
            fp_info ("Commit succeeded");
            fpi_device_enroll_complete (device, g_object_ref (self->enroll_print), NULL);
            fpi_ssm_mark_completed (g_steal_pointer (&self->ssm));
          }
        break;
      }
    }

  g_clear_pointer (&self->buffer_in, g_free);
}

static void
elanmoc2_enroll (FpDevice *device)
{
  FpiDeviceElanMoC2 *self = FPI_DEVICE_ELANMOC2 (device);

  fp_info ("[elanmoc2] New enroll operation");

  self->enroll_stage = 0;
  fpi_device_get_enroll_data (device, &self->enroll_print);

  self->ssm = fpi_ssm_new (device, elanmoc2_enroll_run_state, ENROLL_NUM_STATES);
  fpi_ssm_start (self->ssm, elanmoc2_enroll_ssm_completed_callback);
}

static void
elanmoc2_delete_run_state (FpiSsm *ssm, FpDevice *device)
{
  FpiDeviceElanMoC2 *self = FPI_DEVICE_ELANMOC2 (device);
  g_autofree guint8 *buffer_out = NULL;
  g_autofree const guint8 *user_id = NULL;
  GError *error = NULL;

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case DELETE_GET_NUM_ENROLLED:
      elanmoc2_perform_get_num_enrolled (self, ssm);
      break;

    case DELETE_DELETE: {
        self->enrolled_num = self->buffer_in[1];
        if (self->enrolled_num == 0)
          {
            fp_info ("No fingers enrolled, nothing to delete");
            fpi_device_delete_complete (device, NULL);
            fpi_ssm_mark_completed (g_steal_pointer (&self->ssm));
            break;
          }
        FpPrint *print = NULL;
        fpi_device_get_delete_data (device, &print);

        guint8 finger_id = 0xFF;
        guint8 user_id_len = 0;
        elanmoc2_print_get_data (print, &finger_id, &user_id_len, &user_id);

        if ((buffer_out = elanmoc2_prepare_cmd (self, &cmd_delete)) == NULL)
          {
            fpi_ssm_next_state (ssm);
            break;
          }
        buffer_out[3] = 0xf0 | (finger_id + 5);
        memcpy ((char *) &buffer_out[4], (char *) user_id, MIN (cmd_delete.out_len - 4, user_id_len));
        elanmoc2_cmd_transceive (device, ssm, &cmd_delete, g_steal_pointer (&buffer_out));
        break;
      }

    case DELETE_CHECK_DELETED: {
        error = NULL;

        // If the finger is actually still enrolled (but i.e. we provided the wrong user ID), enroll will attempt the
        // deletion again with the device-stored user ID after the user performs an identify op with that finger to
        // re-enroll it.
        if (self->buffer_in[1] != 0 && self->buffer_in[1] != ELANMOC2_RESP_NOT_ENROLLED)
          fp_info ("Delete failed with error code %d, assuming no longer enrolled", self->buffer_in[1]);

        fpi_ssm_mark_completed (g_steal_pointer (&self->ssm));
        fpi_device_delete_complete (device, error);

        break;
      }
    }

  g_clear_pointer (&self->buffer_in, g_free);
}

static void
elanmoc2_delete (FpDevice *device)
{
  FpiDeviceElanMoC2 *self = FPI_DEVICE_ELANMOC2 (device);

  fp_info ("[elanmoc2] New delete operation");
  self->ssm = fpi_ssm_new (device, elanmoc2_delete_run_state, DELETE_NUM_STATES);
  fpi_ssm_start (self->ssm, elanmoc2_ssm_completed_callback);
}

static void
elanmoc2_clear_storage_run_state (FpiSsm *ssm, FpDevice *device)
{
  FpiDeviceElanMoC2 *self = FPI_DEVICE_ELANMOC2 (device);
  g_autofree uint8_t *buffer_out = NULL;
  GError *error = NULL;

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case CLEAR_STORAGE_WIPE_SENSOR:
      if ((buffer_out = elanmoc2_prepare_cmd (self, &cmd_wipe_sensor)) == NULL)
        {
          fpi_ssm_next_state (ssm);
          break;
        }
      elanmoc2_cmd_transceive (device, ssm, &cmd_wipe_sensor, g_steal_pointer (&buffer_out));
      fp_info ("Sent sensor wipe command");
      fpi_ssm_next_state (ssm);
      break;

    case CLEAR_STORAGE_GET_NUM_ENROLLED:
      elanmoc2_perform_get_num_enrolled (self, ssm);
      break;

    case CLEAR_STORAGE_CHECK_NUM_ENROLLED:
      // It should take around 5 seconds to arrive here after the wipe sensor command
      self->enrolled_num = self->buffer_in[1];
      if (self->enrolled_num == 0)
        {
          fpi_device_clear_storage_complete (device, NULL);
          fpi_ssm_mark_completed (g_steal_pointer (&self->ssm));
        }
      else
        {
          error = fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                            "Sensor erase requested but storage is not empty");
          fpi_device_clear_storage_complete (device, error);
          fpi_ssm_mark_failed (g_steal_pointer (&self->ssm), error);
          break;
        }
      break;
    }

  g_clear_pointer (&self->buffer_in, g_free);
}

static void
elanmoc2_clear_storage (FpDevice *device)
{
  FpiDeviceElanMoC2 *self = FPI_DEVICE_ELANMOC2 (device);

  fp_info ("[elanmoc2] New clear storage operation");
  self->ssm = fpi_ssm_new (device, elanmoc2_clear_storage_run_state, CLEAR_STORAGE_NUM_STATES);
  fpi_ssm_start (self->ssm, elanmoc2_ssm_completed_callback);
}

static void
fpi_device_elanmoc2_init (FpiDeviceElanMoC2 *self)
{
  G_DEBUG_HERE ();
}

static void
fpi_device_elanmoc2_class_init (FpiDeviceElanMoC2Class *klass)
{
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);

  dev_class->id = FP_COMPONENT;
  dev_class->full_name = ELANMOC2_DRIVER_FULLNAME;

  dev_class->type = FP_DEVICE_TYPE_USB;
  dev_class->scan_type = FP_SCAN_TYPE_PRESS;
  dev_class->id_table = elanmoc2_id_table;

  dev_class->nr_enroll_stages = ELANMOC2_ENROLL_TIMES;
  dev_class->temp_hot_seconds = -1;

  dev_class->open = elanmoc2_open;
  dev_class->close = elanmoc2_close;
  dev_class->identify = elanmoc2_identify_verify;
  dev_class->verify = elanmoc2_identify_verify;
  dev_class->enroll = elanmoc2_enroll;
  dev_class->delete = elanmoc2_delete;
  dev_class->clear_storage = elanmoc2_clear_storage;
  dev_class->list = elanmoc2_list;
  dev_class->cancel = elanmoc2_cancel;

  fpi_device_class_auto_initialize_features (dev_class);
  dev_class->features |= FP_DEVICE_FEATURE_DUPLICATES_CHECK;
  dev_class->features |= FP_DEVICE_FEATURE_UPDATE_PRINT;
}
