/*
 * FpImageDevice - An image based fingerprint reader device
 * Copyright (C) 2019 Benjamin Berg <bberg@redhat.com>
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

#define FP_COMPONENT "image_device"
#include "fpi-log.h"

#include "fpi-image-device.h"
#include "fpi-print.h"
#include "fpi-image.h"

#define MIN_ACCEPTABLE_MINUTIAE 10
#define BOZORTH3_DEFAULT_THRESHOLD 40
#define IMG_ENROLL_STAGES 5

/**
 * SECTION: fp-image-device
 * @title: FpImageDevice
 * @short_description: Image device subclass
 *
 * This is a helper class for the commonly found image based devices.
 */

/**
 * SECTION: fpi-image-device
 * @title: Internal FpImageDevice
 * @short_description: Internal image device routines
 *
 * See #FpImageDeviceClass for more details. Also see the public
 * #FpImageDevice routines.
 */

typedef struct
{
  FpImageDeviceState state;
  gboolean           active;

  gint               enroll_stage;

  guint              pending_activation_timeout_id;
  gboolean           pending_activation_timeout_waiting_finger_off;

  gint               bz3_threshold;
} FpImageDevicePrivate;

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (FpImageDevice, fp_image_device, FP_TYPE_DEVICE)


/*******************************************************/

/* TODO:
 * - sanitize_image seems a bit odd, in particular the sizing stuff.
 **/

/* Static helper functions */

static void
fp_image_device_change_state (FpImageDevice *self, FpImageDeviceState state)
{
  FpImageDevicePrivate *priv = fp_image_device_get_instance_private (self);
  FpImageDeviceClass *cls = FP_IMAGE_DEVICE_GET_CLASS (self);

  /* Cannot change to inactive using this function. */
  g_assert (state != FP_IMAGE_DEVICE_STATE_INACTIVE);

  /* We might have been waiting for the finger to go OFF to start the
   * next operation. */
  if (priv->pending_activation_timeout_id)
    {
      g_source_remove (priv->pending_activation_timeout_id);
      priv->pending_activation_timeout_id = 0;
    }

  fp_dbg ("Image device internal state change from %d to %d\n", priv->state, state);

  priv->state = state;

  /* change_state is the only callback which is optional and does not
   * have a default implementation. */
  if (cls->change_state)
    cls->change_state (self, state);
}

static void
fp_image_device_activate (FpImageDevice *self)
{
  FpImageDevicePrivate *priv = fp_image_device_get_instance_private (self);
  FpImageDeviceClass *cls = FP_IMAGE_DEVICE_GET_CLASS (self);

  g_assert (!priv->active);

  /* We don't have a neutral ACTIVE state, but we always will
   * go into WAIT_FINGER_ON afterwards. */
  priv->state = FP_IMAGE_DEVICE_STATE_AWAIT_FINGER_ON;

  /* We might have been waiting for deactivation to finish before
   * starting the next operation. */
  if (priv->pending_activation_timeout_id)
    {
      g_source_remove (priv->pending_activation_timeout_id);
      priv->pending_activation_timeout_id = 0;
    }

  fp_dbg ("Activating image device\n");
  cls->activate (self);
}

static void
fp_image_device_deactivate (FpDevice *device)
{
  FpImageDevice *self = FP_IMAGE_DEVICE (device);
  FpImageDevicePrivate *priv = fp_image_device_get_instance_private (self);
  FpImageDeviceClass *cls = FP_IMAGE_DEVICE_GET_CLASS (device);

  if (!priv->active)
    {
      /* XXX: We currently deactivate both from minutiae scan result
       *      and finger off report. */
      fp_dbg ("Already deactivated, ignoring request.");
      return;
    }
  priv->state = FP_IMAGE_DEVICE_STATE_INACTIVE;

  fp_dbg ("Deactivating image device\n");
  cls->deactivate (self);
}

static gboolean
pending_activation_timeout (gpointer user_data)
{
  FpImageDevice *self = FP_IMAGE_DEVICE (user_data);
  FpImageDevicePrivate *priv = fp_image_device_get_instance_private (self);

  priv->pending_activation_timeout_id = 0;

  if (priv->pending_activation_timeout_waiting_finger_off)
    fpi_device_action_error (FP_DEVICE (self),
                             fpi_device_retry_new_msg (FP_DEVICE_RETRY_REMOVE_FINGER,
                                                       "Remove finger before requesting another scan operation"));
  else
    fpi_device_action_error (FP_DEVICE (self),
                             fpi_device_retry_new (FP_DEVICE_RETRY_GENERAL));

  return G_SOURCE_REMOVE;
}

/* Callbacks/vfuncs */
static void
fp_image_device_open (FpDevice *device)
{
  FpImageDeviceClass *cls = FP_IMAGE_DEVICE_GET_CLASS (device);

  /* Nothing special about opening an image device, just
   * forward the request. */
  cls->img_open (FP_IMAGE_DEVICE (device));
}

static void
fp_image_device_close (FpDevice *device)
{
  FpImageDevice *self = FP_IMAGE_DEVICE (device);
  FpImageDeviceClass *cls = FP_IMAGE_DEVICE_GET_CLASS (self);
  FpImageDevicePrivate *priv = fp_image_device_get_instance_private (self);

  /* In the close case we may need to wait/force deactivation first.
   * Three possible cases:
   *  1. We are inactive
   *     -> immediately close
   *  2. We are waiting for finger off
   *     -> imediately deactivate
   *  3. We are deactivating
   *     -> handled by deactivate_complete */

  if (!priv->active)
    cls->img_close (self);
  else if (priv->state != FP_IMAGE_DEVICE_STATE_INACTIVE)
    fp_image_device_deactivate (device);
}

static void
fp_image_device_cancel_action (FpDevice *device)
{
  FpImageDevice *self = FP_IMAGE_DEVICE (device);
  FpDeviceAction action;

  action = fpi_device_get_current_action (device);

  /* We can only cancel capture operations, in that case, deactivate and return
   * an error immediately. */
  if (action == FP_DEVICE_ACTION_ENROLL ||
      action == FP_DEVICE_ACTION_VERIFY ||
      action == FP_DEVICE_ACTION_IDENTIFY ||
      action == FP_DEVICE_ACTION_CAPTURE)
    {
      fp_image_device_deactivate (FP_DEVICE (self));

      /* XXX: Some nicer way of doing this would be good. */
      fpi_device_action_error (FP_DEVICE (self),
                               g_error_new (G_IO_ERROR,
                                            G_IO_ERROR_CANCELLED,
                                            "Device operation was cancelled"));
    }
}

static void
fp_image_device_start_capture_action (FpDevice *device)
{
  FpImageDevice *self = FP_IMAGE_DEVICE (device);
  FpImageDevicePrivate *priv = fp_image_device_get_instance_private (self);
  FpDeviceAction action;

  /* There is just one action that we cannot support out
   * of the box, which is a capture without first waiting
   * for a finger to be on the device.
   */
  action = fpi_device_get_current_action (device);
  if (action == FP_DEVICE_ACTION_CAPTURE)
    {
      gboolean wait_for_finger;

      fpi_device_get_capture_data (device, &wait_for_finger);

      if (!wait_for_finger)
        {
          fpi_device_action_error (device, fpi_device_error_new (FP_DEVICE_ERROR_NOT_SUPPORTED));
          return;
        }
    }
  else if (action == FP_DEVICE_ACTION_ENROLL)
    {
      FpPrint *enroll_print = NULL;

      fpi_device_get_enroll_data (device, &enroll_print);
      fpi_print_set_type (enroll_print, FP_PRINT_NBIS);
    }

  priv->enroll_stage = 0;

  /* The device might still be deactivating from a previous call.
   * In that situation, try to wait for a bit before reporting back an
   * error (which will usually say that the user should remove the
   * finger).
   */
  if (priv->state != FP_IMAGE_DEVICE_STATE_INACTIVE || priv->active)
    {
      g_debug ("Got a new request while the device was still active");
      g_assert (priv->pending_activation_timeout_id == 0);
      priv->pending_activation_timeout_id =
        g_timeout_add (100, pending_activation_timeout, device);

      if (priv->state == FP_IMAGE_DEVICE_STATE_AWAIT_FINGER_OFF)
        priv->pending_activation_timeout_waiting_finger_off = TRUE;
      else
        priv->pending_activation_timeout_waiting_finger_off = FALSE;

      return;
    }

  /* And activate the device; we rely on fpi_image_device_activate_complete()
   * to be called when done (or immediately). */
  fp_image_device_activate (self);
}


/*********************************************************/

static void
fp_image_device_finalize (GObject *object)
{
  FpImageDevice *self = (FpImageDevice *) object;
  FpImageDevicePrivate *priv = fp_image_device_get_instance_private (self);

  g_assert (priv->active == FALSE);

  G_OBJECT_CLASS (fp_image_device_parent_class)->finalize (object);
}

static void
fp_image_device_default_activate (FpImageDevice *self)
{
  fpi_image_device_activate_complete (self, NULL);
}

static void
fp_image_device_default_deactivate (FpImageDevice *self)
{
  fpi_image_device_deactivate_complete (self, NULL);
}

static void
fp_image_device_class_init (FpImageDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  FpDeviceClass *fp_device_class = FP_DEVICE_CLASS (klass);

  object_class->finalize = fp_image_device_finalize;

  fp_device_class->open = fp_image_device_open;
  fp_device_class->close = fp_image_device_close;
  fp_device_class->enroll = fp_image_device_start_capture_action;
  fp_device_class->verify = fp_image_device_start_capture_action;
  fp_device_class->identify = fp_image_device_start_capture_action;
  fp_device_class->capture = fp_image_device_start_capture_action;

  fp_device_class->cancel = fp_image_device_cancel_action;

  /* Default implementations */
  klass->activate = fp_image_device_default_activate;
  klass->deactivate = fp_image_device_default_deactivate;
}

static void
fp_image_device_init (FpImageDevice *self)
{
  FpImageDevicePrivate *priv = fp_image_device_get_instance_private (self);
  FpImageDeviceClass *cls = FP_IMAGE_DEVICE_GET_CLASS (self);

  /* Set default values. */
  fpi_device_set_nr_enroll_stages (FP_DEVICE (self), IMG_ENROLL_STAGES);

  priv->bz3_threshold = BOZORTH3_DEFAULT_THRESHOLD;
  if (cls->bz3_threshold > 0)
    priv->bz3_threshold = cls->bz3_threshold;

}

static void
fpi_image_device_minutiae_detected (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  g_autoptr(FpImage) image = FP_IMAGE (source_object);
  GError *error = NULL;
  FpPrint *print = NULL;
  FpDevice *device = FP_DEVICE (user_data);
  FpImageDevicePrivate *priv;
  FpDeviceAction action;

  /* Note: We rely on the device to not disappear during an operation. */

  if (!fp_image_detect_minutiae_finish (image, res, &error))
    {
      /* Cancel operation . */
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        {
          fpi_device_action_error (device, g_steal_pointer (&error));
          fp_image_device_deactivate (device);
          return;
        }

      /* Replace error with a retry condition. */
      g_warning ("Failed to detect minutiae: %s", error->message);
      g_clear_pointer (&error, g_error_free);

      error = fpi_device_retry_new_msg (FP_DEVICE_RETRY_GENERAL, "Minutiae detection failed, please retry");
    }

  priv = fp_image_device_get_instance_private (FP_IMAGE_DEVICE (device));
  action = fpi_device_get_current_action (device);

  if (action == FP_DEVICE_ACTION_CAPTURE)
    {
      fpi_device_capture_complete (device, g_steal_pointer (&image), error);
      fp_image_device_deactivate (device);
      return;
    }

  if (!error)
    {
      print = fp_print_new (device);
      fpi_print_set_type (print, FP_PRINT_NBIS);
      if (!fpi_print_add_from_image (print, image, &error))
        g_clear_object (&print);
    }

  if (action == FP_DEVICE_ACTION_ENROLL)
    {
      FpPrint *enroll_print;
      fpi_device_get_enroll_data (device, &enroll_print);

      if (print)
        {
          fpi_print_add_print (enroll_print, print);
          priv->enroll_stage += 1;
        }

      fpi_device_enroll_progress (device, priv->enroll_stage, print, error);

      if (priv->enroll_stage == IMG_ENROLL_STAGES)
        {
          fpi_device_enroll_complete (device, g_object_ref (enroll_print), NULL);
          fp_image_device_deactivate (device);
        }
    }
  else if (action == FP_DEVICE_ACTION_VERIFY)
    {
      FpPrint *template;
      FpiMatchResult result;

      fpi_device_get_verify_data (device, &template);
      if (print)
        result = fpi_print_bz3_match (template, print, priv->bz3_threshold, &error);
      else
        result = FPI_MATCH_ERROR;

      fpi_device_verify_complete (device, result, print, error);
      fp_image_device_deactivate (device);
    }
  else if (action == FP_DEVICE_ACTION_IDENTIFY)
    {
      gint i;
      GPtrArray *templates;
      FpPrint *result = NULL;

      fpi_device_get_identify_data (device, &templates);
      for (i = 0; !error && i < templates->len; i++)
        {
          FpPrint *template = g_ptr_array_index (templates, i);

          if (fpi_print_bz3_match (template, print, priv->bz3_threshold, &error) == FPI_MATCH_SUCCESS)
            {
              result = g_object_ref (template);
              break;
            }
        }

      fpi_device_identify_complete (device, result, print, error);
      fp_image_device_deactivate (device);
    }
  else
    {
      /* XXX: This can be hit currently due to a race condition in the enroll code!
       *      In that case we scan a further image even though the minutiae for the previous
       *      one have not yet been detected.
       *      We need to keep track on the pending minutiae detection and the fact that
       *      it will finish eventually (or we may need to retry on error and activate the
       *      device again). */
      g_assert_not_reached ();
    }
}

/*********************************************************/
/* Private API */

/**
 * fpi_image_device_set_bz3_threshold:
 * @self: a #FpImageDevice imaging fingerprint device
 * @bz3_threshold: BZ3 threshold to use
 *
 * Dynamically adjust the bz3 threshold. This is only needed for drivers
 * that support devices with different properties. It should generally be
 * called from the probe callback, but is acceptable to call from the open
 * callback.
 */
void
fpi_image_device_set_bz3_threshold (FpImageDevice *self,
                                    gint           bz3_threshold)
{
  FpImageDevicePrivate *priv = fp_image_device_get_instance_private (self);

  g_return_if_fail (FP_IS_IMAGE_DEVICE (self));
  g_return_if_fail (bz3_threshold > 0);

  priv->bz3_threshold = bz3_threshold;
}

/**
 * fpi_image_device_report_finger_status:
 * @self: a #FpImageDevice imaging fingerprint device
 * @present: whether the finger is present on the sensor
 *
 * Reports from the driver whether the user's finger is on
 * the sensor.
 */
void
fpi_image_device_report_finger_status (FpImageDevice *self,
                                       gboolean       present)
{
  FpDevice *device = FP_DEVICE (self);
  FpImageDevicePrivate *priv = fp_image_device_get_instance_private (self);
  FpDeviceAction action;

  action = fpi_device_get_current_action (device);

  if (priv->state == FP_IMAGE_DEVICE_STATE_INACTIVE)
    {
      /* Do we really want to always ignore such reports? We could
       * also track the state in case the user had the finger on
       * the device at initialisation time and the driver reports
       * this early.
       */
      g_debug ("Ignoring finger presence report as the device is not active!");
      return;
    }

  action = fpi_device_get_current_action (device);

  g_assert (action != FP_DEVICE_ACTION_OPEN);
  g_assert (action != FP_DEVICE_ACTION_CLOSE);

  g_debug ("Image device reported finger status: %s", present ? "on" : "off");

  if (present && priv->state == FP_IMAGE_DEVICE_STATE_AWAIT_FINGER_ON)
    {
      fp_image_device_change_state (self, FP_IMAGE_DEVICE_STATE_CAPTURE);
    }
  else if (!present && priv->state == FP_IMAGE_DEVICE_STATE_AWAIT_FINGER_OFF)
    {
      /* We need to deactivate or continue to await finger */

      /* There are three possible situations:
       *  1. We are deactivating the device and the action is still in progress
       *     (minutiae detection).
       *  2. We are still deactivating the device after an action completed
       *  3. We were waiting for finger removal to start the new action
       * Either way, we always end up deactivating except for the enroll case.
       * XXX: This is not quite correct though, as we assume we need another finger
       *      scan even though we might be processing the last one (successfully).
       */
      if (action != FP_DEVICE_ACTION_ENROLL)
        fp_image_device_deactivate (device);
      else
        fp_image_device_change_state (self, FP_IMAGE_DEVICE_STATE_AWAIT_FINGER_ON);
    }
}

/**
 * fpi_image_device_image_captured:
 * @self: a #FpImageDevice imaging fingerprint device
 * @image: whether the finger is present on the sensor
 *
 * Reports an image capture. Only use this function if the image was
 * captured successfully. If there was an issue where the user should
 * retry, use fpi_image_device_retry_scan() to report the retry condition.
 *
 * In the event of a fatal error for the operation use
 * fpi_image_device_session_error(). This will abort the entire operation
 * including e.g. an enroll operation which captures multiple images during
 * one session.
 */
void
fpi_image_device_image_captured (FpImageDevice *self, FpImage *image)
{
  FpImageDevicePrivate *priv = fp_image_device_get_instance_private (self);
  FpDeviceAction action;

  action = fpi_device_get_current_action (FP_DEVICE (self));

  g_return_if_fail (image != NULL);
  g_return_if_fail (priv->state == FP_IMAGE_DEVICE_STATE_CAPTURE);
  g_return_if_fail (action == FP_DEVICE_ACTION_ENROLL ||
                    action == FP_DEVICE_ACTION_VERIFY ||
                    action == FP_DEVICE_ACTION_IDENTIFY ||
                    action == FP_DEVICE_ACTION_CAPTURE);

  fp_image_device_change_state (self, FP_IMAGE_DEVICE_STATE_AWAIT_FINGER_OFF);

  g_debug ("Image device captured an image");

  /* XXX: We also detect minutiae in capture mode, we solely do this
   *      to normalize the image which will happen as a by-product. */
  fp_image_detect_minutiae (image,
                            fpi_device_get_cancellable (FP_DEVICE (self)),
                            fpi_image_device_minutiae_detected,
                            self);
}

/**
 * fpi_image_device_retry_scan:
 * @self: a #FpImageDevice imaging fingerprint device
 * @retry: The #FpDeviceRetry error code to report
 *
 * Reports a scan failure to the user. This may or may not abort the
 * current session. It is the equivalent of fpi_image_device_image_captured()
 * in the case of a retryable error condition (e.g. short swipe).
 */
void
fpi_image_device_retry_scan (FpImageDevice *self, FpDeviceRetry retry)
{
  FpImageDevicePrivate *priv = fp_image_device_get_instance_private (self);
  FpDeviceAction action;
  GError *error;

  action = fpi_device_get_current_action (FP_DEVICE (self));

  /* We might be waiting for a finger at this point, so just accept
   * all but INACTIVE */
  g_return_if_fail (priv->state != FP_IMAGE_DEVICE_STATE_INACTIVE);
  g_return_if_fail (action == FP_DEVICE_ACTION_ENROLL ||
                    action == FP_DEVICE_ACTION_VERIFY ||
                    action == FP_DEVICE_ACTION_IDENTIFY ||
                    action == FP_DEVICE_ACTION_CAPTURE);

  error = fpi_device_retry_new (retry);

  if (action == FP_DEVICE_ACTION_ENROLL)
    {
      g_debug ("Reporting retry during enroll");
      fpi_device_enroll_progress (FP_DEVICE (self), priv->enroll_stage, NULL, error);
    }
  else
    {
      /* We abort the operation and let the surrounding code retry in the
       * non-enroll case (this is identical to a session error). */
      g_debug ("Abort current operation due to retry (non-enroll case)");
      fp_image_device_deactivate (FP_DEVICE (self));
      fpi_device_action_error (FP_DEVICE (self), error);
    }
}

/**
 * fpi_image_device_session_error:
 * @self: a #FpImageDevice imaging fingerprint device
 * @error: The #GError to report
 *
 * Report an error while interacting with the device. This effectively
 * aborts the current ongoing action.
 */
void
fpi_image_device_session_error (FpImageDevice *self, GError *error)
{
  FpImageDevicePrivate *priv = fp_image_device_get_instance_private (self);

  g_return_if_fail (self);

  if (!error)
    {
      g_warning ("Driver did not provide an error, generating a generic one");
      error = g_error_new (FP_DEVICE_ERROR, FP_DEVICE_ERROR_GENERAL, "Driver reported session error without an error");
    }

  if (!priv->active)
    {
      FpDeviceAction action = fpi_device_get_current_action (FP_DEVICE (self));
      g_warning ("Driver reported session error, but device is inactive.");

      if (action != FP_DEVICE_ACTION_NONE)
        {
          g_warning ("Translating to activation failure!");
          fpi_image_device_activate_complete (self, error);
          return;
        }
    }
  else if (priv->state == FP_IMAGE_DEVICE_STATE_INACTIVE)
    {
      g_warning ("Driver reported session error; translating to deactivation failure.");
      fpi_image_device_deactivate_complete (self, error);
      return;
    }

  if (error->domain == FP_DEVICE_RETRY)
    g_warning ("Driver should report retries using fpi_image_device_retry_scan!");

  fp_image_device_deactivate (FP_DEVICE (self));
  fpi_device_action_error (FP_DEVICE (self), error);
}

/**
 * fpi_image_device_activate_complete:
 * @self: a #FpImageDevice imaging fingerprint device
 * @error: A #GError or %NULL on success
 *
 * Reports completion of device activation.
 */
void
fpi_image_device_activate_complete (FpImageDevice *self, GError *error)
{
  FpImageDevicePrivate *priv = fp_image_device_get_instance_private (self);
  FpDeviceAction action;

  action = fpi_device_get_current_action (FP_DEVICE (self));

  g_return_if_fail (priv->active == FALSE);
  g_return_if_fail (action == FP_DEVICE_ACTION_ENROLL ||
                    action == FP_DEVICE_ACTION_VERIFY ||
                    action == FP_DEVICE_ACTION_IDENTIFY ||
                    action == FP_DEVICE_ACTION_CAPTURE);

  if (error)
    {
      g_debug ("Image device activation failed");
      fpi_device_action_error (FP_DEVICE (self), error);
      return;
    }

  g_debug ("Image device activation completed");

  priv->active = TRUE;

  /* We always want to capture at this point, move to AWAIT_FINGER
   * state. */
  fp_image_device_change_state (self, FP_IMAGE_DEVICE_STATE_AWAIT_FINGER_ON);
}

/**
 * fpi_image_device_deactivate_complete:
 * @self: a #FpImageDevice imaging fingerprint device
 * @error: A #GError or %NULL on success
 *
 * Reports completion of device deactivation.
 */
void
fpi_image_device_deactivate_complete (FpImageDevice *self, GError *error)
{
  FpImageDevicePrivate *priv = fp_image_device_get_instance_private (self);
  FpImageDeviceClass *cls = FP_IMAGE_DEVICE_GET_CLASS (self);
  FpDeviceAction action;

  g_return_if_fail (priv->active == TRUE);
  g_return_if_fail (priv->state == FP_IMAGE_DEVICE_STATE_INACTIVE);

  g_debug ("Image device deactivation completed");

  priv->active = FALSE;

  /* Deactivation completed. As we deactivate in the background
   * there may already be a new task pending. Check whether we
   * need to do anything. */
  action = fpi_device_get_current_action (FP_DEVICE (self));

  /* Special case, if we should be closing, but didn't due to a running
   * deactivation, then do so now. */
  if (action == FP_DEVICE_ACTION_CLOSE)
    {
      cls->img_close (self);
      return;
    }

  /* We might be waiting to be able to activate again. */
  if (priv->pending_activation_timeout_id)
    fp_image_device_activate (self);
}

/**
 * fpi_image_device_open_complete:
 * @self: a #FpImageDevice imaging fingerprint device
 * @error: A #GError or %NULL on success
 *
 * Reports completion of open operation.
 */
void
fpi_image_device_open_complete (FpImageDevice *self, GError *error)
{
  FpImageDevicePrivate *priv = fp_image_device_get_instance_private (self);
  FpDeviceAction action;

  action = fpi_device_get_current_action (FP_DEVICE (self));

  g_return_if_fail (priv->active == FALSE);
  g_return_if_fail (action == FP_DEVICE_ACTION_OPEN);

  g_debug ("Image device open completed");

  priv->state = FP_IMAGE_DEVICE_STATE_INACTIVE;

  fpi_device_open_complete (FP_DEVICE (self), error);
}

/**
 * fpi_image_device_close_complete:
 * @self: a #FpImageDevice imaging fingerprint device
 * @error: A #GError or %NULL on success
 *
 * Reports completion of close operation.
 */
void
fpi_image_device_close_complete (FpImageDevice *self, GError *error)
{
  FpImageDevicePrivate *priv = fp_image_device_get_instance_private (self);
  FpDeviceAction action;

  action = fpi_device_get_current_action (FP_DEVICE (self));

  g_debug ("Image device close completed");

  g_return_if_fail (priv->active == FALSE);
  g_return_if_fail (action == FP_DEVICE_ACTION_CLOSE);

  priv->state = FP_IMAGE_DEVICE_STATE_INACTIVE;

  fpi_device_close_complete (FP_DEVICE (self), error);
}
