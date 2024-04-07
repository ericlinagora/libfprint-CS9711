/*
 * Chipsailing CS9711Fingprint driver
 *
 * Modified based on driver vfs301* so keeping original notice:
 *
 * Copyright (c) 2011-2012 Andrej Krutak <dev@andree.sk>
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

#define FP_COMPONENT "cs9711"

#include "drivers_api.h"
#include "cs9711.h"

G_DEFINE_TYPE (FpDeviceCs9711, fpi_device_cs9711, FP_TYPE_IMAGE_DEVICE)

#define CS9711_DEFAULT_WAIT_TIMEOUT 300
#define CS9711_DEFAULT_RESET_SLEEP  250

#define CS9711_SEND_ENDPOINT    0x01
#define CS9711_RECEIVE_ENDPOINT 0x81

#define CS9711_FP_CMD_LEN_1    8
#define CS9711_FP_RECV_LEN_1   8000
#define CS9711_FP_RECV_LEN_2   24
#define CS9711_FP_RECV_LEN_MAX CS9711_FP_RECV_LEN_1

#define CS9711_FP_CMD_TYPE_INIT  1
#define CS9711_FP_CMD_TYPE_RESET 2
#define CS9711_FP_CMD_TYPE_SCAN  4

#define CS9711_FP_CMD_STATE_RESULT_EXPECTED { 0xea, 0x01, 0x62, 0xa0, 0x00, 0x00, 0xc3, 0xea }


/************************** GENERIC STUFF *************************************/

/** If error isn't NULL then fail the ssm or move it to the next state */
static void
m_util_fail_if_error_or_next (FpiSsm *ssm, GError *error)
{
  if (error)
    fpi_ssm_mark_failed (ssm, error);
  else
    fpi_ssm_next_state (ssm);
}

/** Synchroneous USB bulk write OUT helper*/
static void
usb_send_out_sync (FpDevice *dev, guint8 type, GError **error)
{
  GError *err = NULL;

  guint8 *data = g_malloc0(CS9711_FP_CMD_LEN_1);

  data[0] = data[CS9711_FP_CMD_LEN_1 - 1] = 0xEA;
  data[1] = data[CS9711_FP_CMD_LEN_1 - 2] = type;

  g_autoptr(FpiUsbTransfer) transfer = NULL;

  transfer = fpi_usb_transfer_new (FP_DEVICE (dev));
  transfer->short_is_error = FALSE;
  fpi_usb_transfer_fill_bulk_full (transfer, CS9711_SEND_ENDPOINT, (guint8 *) data, CS9711_FP_CMD_LEN_1, g_free);
  fpi_usb_transfer_submit_sync (transfer, CS9711_DEFAULT_WAIT_TIMEOUT, &err);
  if (err)
    {
      g_warning ("Error while sending command 0x%X, continuing anyway: %s", type, err->message);
      g_propagate_error (error, err);
    }
  else
    fp_dbg("Sent command 0x%X", type);
}

/** Asynchroneous USB bulk write IN helper */
static void
usb_read_in (FpDevice *dev,
             FpiSsm *ssm,
             gsize length,
             gboolean short_is_error,
             guint timeout_in_ms,
             FpiUsbTransferCallback callback,
             gpointer user_data)
{
  FpiUsbTransfer *transfer = NULL;
  fp_dbg("Reading %lu bytes", length);
  // If the response is larger than the buffer, than the usb helpers
  // error before. The reader doesn't seem to care about requestend
  // length, and occasionally answers out of sequence with an invalid
  // data size. So just request the max expected, and deal with errors
  // in the callback. For the same reason, cannot use short_is_error
  // facility
  length = CS9711_FP_RECV_LEN_MAX;
  short_is_error = FALSE;
  transfer = fpi_usb_transfer_new (FP_DEVICE (dev));
  transfer->short_is_error = short_is_error;
  transfer->ssm = ssm;
  fpi_usb_transfer_fill_bulk (transfer, CS9711_RECEIVE_ENDPOINT, length);
  fpi_usb_transfer_submit (transfer, timeout_in_ms, NULL, callback, user_data);
}

/************************** INIT SSM *************************************/

static void
m_init_read_cb_check_expected (FpiUsbTransfer *transfer,
                               FpDevice       *dev,
                               gpointer        user_data_is_ignore_mismatch_if_non_null,
                               GError         *error)
{
  const guint8 expected[] = CS9711_FP_CMD_STATE_RESULT_EXPECTED;

  g_assert(CS9711_FP_CMD_LEN_1 == sizeof(expected));
  g_assert(transfer->ssm != NULL);

  if (error)
    {
      fp_err ("Read failed: %s, aborting", error->message);
      fpi_ssm_mark_failed(transfer->ssm, error);
    }
  else
    {
      fp_dbg ("Read %lu of requested %lu", transfer->length, transfer->actual_length);
      if (transfer->actual_length != CS9711_FP_CMD_LEN_1)
        fp_warn ("Error; expected %lu bytes but got %lu, continuing", (gsize)CS9711_FP_CMD_LEN_1, transfer->actual_length);
      else if (memcmp (transfer->buffer, expected, CS9711_FP_CMD_LEN_1)) {
        if (!user_data_is_ignore_mismatch_if_non_null) {
          fp_warn ("Error; got different state response than expected, but don't understand it anyway, continuing");
        }
      }
      else
        fp_dbg ("Init response valid");
      fpi_ssm_next_state(transfer->ssm);
    }
}

enum {
  M_INIT_STATE_SEND_INI_QUERY = 0,
  M_INIT_STATE_RECOVER_READ_IGNORED,
  M_INIT_STATE_RECOVER_SEND_RESET,
  M_INIT_STATE_RECOVER_READ_IGNORED_RESET,
  M_INIT_STATE_RECOVER_SEND_INIT,
  M_INIT_STATE_RECEIVE_STATUS,
  M_INIT_STATE_COUNT,
};

/* Exec init sequential state machine */
static void
m_init_state (FpiSsm *ssm, FpDevice *_dev)
{
  FpDeviceCs9711 *self = FPI_DEVICE_CS9711 (_dev);
  GError *error = NULL;

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case M_INIT_STATE_SEND_INI_QUERY:
      usb_send_out_sync (_dev, CS9711_FP_CMD_TYPE_INIT, &error);
      if (error)
        {
          fp_dbg("Error details: '%s', quark: %u code: %d", error->message, error->domain, error->code);
          // if (g_error_matches (error, G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_TIMED_OUT))
          if (error->code == G_USB_DEVICE_ERROR_TIMED_OUT && error->domain == G_USB_DEVICE_ERROR)
            fpi_ssm_next_state (ssm);
          else
            fpi_ssm_mark_failed (ssm, error);
        }
      else
        fpi_ssm_jump_to_state (ssm, M_INIT_STATE_RECEIVE_STATUS);
      break;

    case M_INIT_STATE_RECOVER_READ_IGNORED:
      fp_warn("Send operation had a timeout. Switching to reset procedure. Ignore next message about the result not matching the expected data.");
      usb_read_in (_dev, ssm, CS9711_FP_CMD_LEN_1, FALSE, CS9711_DEFAULT_WAIT_TIMEOUT, m_init_read_cb_check_expected, NULL);
      break;

    case M_INIT_STATE_RECOVER_SEND_RESET:
      usb_send_out_sync (_dev, CS9711_FP_CMD_TYPE_RESET, &error);
      m_util_fail_if_error_or_next (ssm, error);
      break;

    case M_INIT_STATE_RECOVER_READ_IGNORED_RESET:
      fp_warn("Send operation had a timeout. Switching to reset procedure.");
      usb_read_in (_dev, ssm, CS9711_FP_CMD_LEN_1, FALSE, CS9711_DEFAULT_WAIT_TIMEOUT, m_init_read_cb_check_expected, self);
      break;

    case M_INIT_STATE_RECOVER_SEND_INIT:
      usb_send_out_sync (_dev, CS9711_FP_CMD_TYPE_INIT, &error); // Do not reuse state to only try reset once
      m_util_fail_if_error_or_next (ssm, error);
      break;

    case M_INIT_STATE_RECEIVE_STATUS:
      usb_read_in (_dev, ssm, CS9711_FP_CMD_LEN_1, TRUE, CS9711_DEFAULT_WAIT_TIMEOUT, m_init_read_cb_check_expected, NULL);
      break;

    default:
      g_assert_not_reached ();
    }
  // cs9711_proto_init (self);

  // fpi_ssm_mark_completed (ssm);
}

/* Complete init sequential state machine */
static void
m_init_complete (FpiSsm *ssm, FpDevice *dev, GError *error) //TODO: done
{
  fpi_image_device_activate_complete (FP_IMAGE_DEVICE (dev), error);
}

/************************** SCAN SSM *************************************/

enum {
  M_SCAN_INIT_SLEEP = 0,
  M_SCAN_INIT_READ,
  M_SCAN_WAIT_FOR_READ_TO_COMPLETE,
  M_SCAN_GET_IMAGE_TAIL,
  M_SCAN_SEND_POST_SCAN,
  M_SCAN_IMAGE_COMPLETE,
  M_SCAN_STATE_COUNT,
};

static const gpointer M_SCAN_READ_CB_BULK_UD_FIRST_BLOCK = (gpointer)1;
static const gpointer M_SCAN_READ_CB_BULK_UD_SECOND_BLOCK = (gpointer)2;

/* Read into FpDeviceCs9711->image_buffer if one of the two expected chunk sizes */
static void
m_scan_read_cb_bulk (FpiUsbTransfer *transfer,
                     FpDevice       *dev,
                     gpointer        user_data,
                     GError         *error)
{
  FpDeviceCs9711 *self = FPI_DEVICE_CS9711 (dev);

  g_assert (transfer->ssm != NULL);

  gsize expected_size = 0;
  gpointer offset = NULL;

  g_assert (FALSE
    || user_data == M_SCAN_READ_CB_BULK_UD_FIRST_BLOCK
    || user_data == M_SCAN_READ_CB_BULK_UD_SECOND_BLOCK
  );

  if (user_data == M_SCAN_READ_CB_BULK_UD_FIRST_BLOCK)
    {
      expected_size = CS9711_FP_RECV_LEN_1;
      offset = self->image_buffer;
    }
  else if (user_data == M_SCAN_READ_CB_BULK_UD_SECOND_BLOCK)
    {
      expected_size = CS9711_FP_RECV_LEN_2;
      offset = self->image_buffer + CS9711_FP_RECV_LEN_1;
    }
  else
    g_assert_not_reached ();

  if (error)
    {
      fp_err ("Read failed: %s, aborting", error->message);
      fpi_ssm_mark_failed (transfer->ssm, error);
    }
  else
    {
      if (transfer->actual_length != expected_size)
        {
          fp_dbg("\tSkipping buffer print - got %lu bytes, expected %lu", transfer->actual_length, expected_size);
          error = g_error_new (FP_DEVICE_ERROR, FP_DEVICE_ERROR_DATA_INVALID, "expected %lu bytes but got %lu, can't continue", expected_size, transfer->actual_length);
          fpi_ssm_mark_failed (transfer->ssm, error);
        }
      else {
        memcpy (offset, transfer->buffer, expected_size);
        fpi_ssm_next_state (transfer->ssm);
      }
    }
}

static int
m_scan_submit_image (FpiSsm        *ssm,
                     FpImageDevice *dev)
{
  FpDeviceCs9711 *self = FPI_DEVICE_CS9711 (dev);
  FpImage *img;

  img = fp_image_new (CS9711_OUT_WIDTH, CS9711_OUT_HEIGHT);
  if (img == NULL)
    return 1;
#if CS9711_FP_WIDTH_FACTOR != 1
  for (gsize i = 0; i < CS9711_FP_SIZE; i++)
    {
      guint8 *target = img->data + i * CS9711_FP_WIDTH_FACTOR;
      memset(target, self->image_buffer[i], CS9711_FP_WIDTH_FACTOR);
    }
#elif CS9711_FP_WIDTH_INTERPOLATE != 0
  float ratios[CS9711_FP_WIDTH_INTERPOLATE + 1] = {0};

  for (gsize r = 1; r <= CS9711_FP_WIDTH_INTERPOLATE; r++) // 0 is unused to simplify expressions
    ratios[r] = (float)r / (CS9711_FP_WIDTH_INTERPOLATE + 1);
  for (gsize i = 0; i < CS9711_FP_SIZE; i++)
    {
      guint8 *target = img->data + i * (CS9711_FP_WIDTH_INTERPOLATE + 1) - (i / CS9711_FP_WIDTH) * CS9711_FP_WIDTH_INTERPOLATE;
      guint8 start = target[0] = self->image_buffer[i];
      if ((i % CS9711_FP_WIDTH) != (CS9711_FP_WIDTH - 1))
        {
          guint8 end = self->image_buffer[i + 1];
          for (gsize j = 1; j <= CS9711_FP_WIDTH_INTERPOLATE; j++)
            {
              float ratio = ratios[j];
              guint32 output = (1. - ratio) * start + ratio * end;
              target[j] = output < 0 ? 0 : output > 0xff ? 0xff : output;
            }
        }
    }
#else
  memcpy (img->data, self->image_buffer, CS9711_FP_SIZE);
#endif

  img->flags = FPI_IMAGE_PARTIAL | FPI_IMAGE_REMOVE_LESS_MINUTIAE;

  fpi_image_device_image_captured (dev, img);

  return 0;
}

/* Exec scan sequential state machine */
static void
m_scan_state (FpiSsm *ssm, FpDevice *_dev)
{
  FpImageDevice *image_device = FP_IMAGE_DEVICE (_dev);
  GError *error = NULL;

  switch (fpi_ssm_get_cur_state (ssm))
    {
    case M_SCAN_INIT_SLEEP:
      fpi_ssm_next_state_delayed (ssm, CS9711_DEFAULT_RESET_SLEEP);
      break;

    case M_SCAN_INIT_READ:
      usb_read_in (_dev, ssm, CS9711_FP_RECV_LEN_1, FALSE, 0, m_scan_read_cb_bulk, M_SCAN_READ_CB_BULK_UD_FIRST_BLOCK);
      usb_send_out_sync (_dev, CS9711_FP_CMD_TYPE_SCAN, &error);
      fpi_image_device_report_finger_status (image_device, TRUE);
      m_util_fail_if_error_or_next (ssm, error);
      break;

    case M_SCAN_WAIT_FOR_READ_TO_COMPLETE:
      /* Wait for usb_read_in's callback m_scan_read_cb_bulk to advance the state */
      break;

    case M_SCAN_GET_IMAGE_TAIL:
      usb_read_in (_dev, ssm, CS9711_FP_RECV_LEN_2, TRUE, CS9711_DEFAULT_WAIT_TIMEOUT, m_scan_read_cb_bulk, M_SCAN_READ_CB_BULK_UD_SECOND_BLOCK);
      break;

    case M_SCAN_SEND_POST_SCAN:
      usb_send_out_sync (_dev, CS9711_FP_CMD_TYPE_RESET, &error);
      m_util_fail_if_error_or_next (ssm, error);
      break;

    case M_SCAN_IMAGE_COMPLETE:
      m_scan_submit_image (ssm, image_device);
      fpi_image_device_report_finger_status (image_device, FALSE);
      fpi_ssm_mark_completed (ssm);
      break;

    default:
      g_assert_not_reached ();
    }
}

/************************** ImageDevice impl. *************************************/

/* Activate device */
static void
dev_activate (FpImageDevice *dev)
{
  FpiSsm *ssm;

  /* Start init ssm */
  ssm = fpi_ssm_new (FP_DEVICE (dev), m_init_state, M_INIT_STATE_COUNT);
  fpi_ssm_start (ssm, m_init_complete);
}

/* Deactivate device */
static void
dev_deactivate (FpImageDevice *dev)
{
  fpi_image_device_deactivate_complete (dev, NULL);
}

static void
dev_change_state (FpImageDevice *dev, FpiImageDeviceState state)
{
  FpiSsm *ssm_loop;

  if (state != FPI_IMAGE_DEVICE_STATE_AWAIT_FINGER_ON)
    return;

  /* Start a capture operation. */
  ssm_loop = fpi_ssm_new (FP_DEVICE (dev), m_scan_state, M_SCAN_STATE_COUNT);
  fpi_ssm_start (ssm_loop, NULL);
}

static void
dev_open (FpImageDevice *dev)
{
  FpDeviceCs9711 *self = FPI_DEVICE_CS9711 (dev);
  GError *error = NULL;

  /* Claim usb interface */
  g_usb_device_claim_interface (fpi_device_get_usb_device (FP_DEVICE (dev)), 0, 0, &error);

  /* Initialize private structure */
  memset(self->image_buffer, 0, CS9711_FP_SIZE);

  /* Notify open complete */
  fpi_image_device_open_complete (dev, error);
}

static void
dev_close (FpImageDevice *dev)
{
  GError *error = NULL;

  /* Release usb interface */
  g_usb_device_release_interface (fpi_device_get_usb_device (FP_DEVICE (dev)),
                                  0, 0, &error);

  /* Notify close complete */
  fpi_image_device_close_complete (dev, error);
}

/* Usb id table of device */
static const FpIdEntry id_table[] = {
  { .vid = 0x2541,  .pid = 0x0236, },
  { .vid = 0x2541,  .pid = 0x9711, },
  { .vid = 0, .pid = 0, .driver_data = 0 },
};

static void
fpi_device_cs9711_init (FpDeviceCs9711 *self)
{
}

static void
fpi_device_cs9711_class_init (FpDeviceCs9711Class *klass)
{
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);
  FpImageDeviceClass *img_class = FP_IMAGE_DEVICE_CLASS (klass);

  g_assert ((CS9711_FP_SIZE) == (CS9711_FP_RECV_LEN_1 + CS9711_FP_RECV_LEN_2));

  dev_class->id = "cs9711";
  dev_class->full_name = "Chipsailing CS9711Fingprint";
  dev_class->type = FP_DEVICE_TYPE_USB;
  dev_class->id_table = id_table;
  dev_class->scan_type = FP_SCAN_TYPE_PRESS;
  dev_class->nr_enroll_stages = 15;

  img_class->img_open = dev_open;
  img_class->img_close = dev_close;
  img_class->activate = dev_activate;
  img_class->deactivate = dev_deactivate;
  img_class->change_state = dev_change_state;

  //TODO: Makes very marginal improvement, stick with default in case
  //      it changes with a better implementation in the future
  // img_class->bz3_threshold = 24;

  img_class->img_width = CS9711_OUT_WIDTH;
  img_class->img_height = CS9711_OUT_HEIGHT;
}
