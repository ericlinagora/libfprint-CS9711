/*
 * FpDevice - A fingerprint reader device
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

#define FP_COMPONENT "device"
#include "fpi-log.h"

#include "fpi-device.h"

/**
 * SECTION: fp-device
 * @title: FpDevice
 * @short_description: Fingerprint device handling
 *
 * The #FpDevice object allows you to interact with fingerprint readers.
 * Befor doing any other operation you need to fp_device_open() the device
 * and after you are done you need to fp_device_close() it again.
 */

/**
 * SECTION: fpi-device
 * @title: Internal FpDevice
 * @short_description: Internal device routines
 *
 * The methods that are availabe for drivers to manipulate a device. See
 * #FpDeviceClass for more information. Also note that most of these are
 * not relevant for image based devices, see #FpImageDeviceClass in that
 * case.
 *
 * Also see the public #FpDevice routines.
 */

typedef struct
{
  FpDeviceType type;

  union
  {
    GUsbDevice  *usb_device;
    const gchar *virtual_env;
  };

  gboolean   is_open;

  gchar     *device_id;
  gchar     *device_name;
  FpScanType scan_type;

  guint64    driver_data;

  gint       nr_enroll_stages;
  GSList    *sources;

  /* We always make sure that only one task is run at a time. */
  FpDeviceAction      current_action;
  GTask              *current_task;
  GAsyncReadyCallback current_user_cb;
  gulong              current_cancellable_id;
  GSource            *current_idle_cancel_source;
  GSource            *current_task_idle_return_source;

  /* State for tasks */
  gboolean wait_for_finger;
} FpDevicePrivate;

static void fp_device_async_initable_iface_init (GAsyncInitableIface *iface);

G_DEFINE_TYPE_EXTENDED (FpDevice, fp_device, G_TYPE_OBJECT, G_TYPE_FLAG_ABSTRACT,
                        G_IMPLEMENT_INTERFACE (G_TYPE_ASYNC_INITABLE,
                                               fp_device_async_initable_iface_init)
                        G_ADD_PRIVATE (FpDevice))

enum {
  PROP_0,
  PROP_DRIVER,
  PROP_DEVICE_ID,
  PROP_NAME,
  PROP_NR_ENROLL_STAGES,
  PROP_SCAN_TYPE,
  PROP_FPI_ENVIRON,
  PROP_FPI_USB_DEVICE,
  PROP_FPI_DRIVER_DATA,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];

typedef struct
{
  FpPrint         *print;

  FpEnrollProgress enroll_progress_cb;
  gpointer         enroll_progress_data;
  GDestroyNotify   enroll_progress_destroy;
} FpEnrollData;

static void
enroll_data_free (gpointer free_data)
{
  FpEnrollData *data = free_data;

  if (data->enroll_progress_destroy)
    data->enroll_progress_destroy (data->enroll_progress_data);
  data->enroll_progress_data = NULL;
  g_clear_object (&data->print);
  g_free (data);
}

/**
 * fp_device_retry_quark:
 *
 * Return value: Quark representing a retryable error.
 **/
G_DEFINE_QUARK (fp - device - retry - quark, fp_device_retry)

/**
 * fp_device_error_quark:
 *
 * Return value: Quark representing a device error.
 **/
G_DEFINE_QUARK (fp - device - error - quark, fp_device_error)

/**
 * fpi_device_retry_new:
 * @error: The #FpDeviceRetry error value describing the issue
 *
 * Create a new retry error code for use with fpi_device_verify_complete()
 * and similar calls.
 */
GError *
fpi_device_retry_new (FpDeviceRetry error)
{
  const gchar *msg;

  switch (error)
    {
    case FP_DEVICE_RETRY_GENERAL:
      msg = "Please try again.";
      break;

    case FP_DEVICE_RETRY_TOO_SHORT:
      msg = "The swipe was too short, please try again.";
      break;

    case FP_DEVICE_RETRY_CENTER_FINGER:
      msg = "The finger was not centered properly, please try again.";
      break;

    case FP_DEVICE_RETRY_REMOVE_FINGER:
      msg = "Please try again after removing the finger first.";
      break;

    default:
      g_warning ("Unsupported error, returning general error instead!");
      error = FP_DEVICE_RETRY_GENERAL;
      msg = "Please try again.";
    }

  return g_error_new_literal (FP_DEVICE_RETRY, error, msg);
}

/**
 * fpi_device_error_new:
 * @error: The #FpDeviceRetry error value describing the issue
 *
 * Create a new error code for use with fpi_device_verify_complete() and
 * similar calls.
 */
GError *
fpi_device_error_new (FpDeviceError error)
{
  const gchar *msg;

  switch (error)
    {
    case FP_DEVICE_ERROR_GENERAL:
      msg = "An unspecified error occured!";
      break;

    case FP_DEVICE_ERROR_NOT_SUPPORTED:
      msg = "The operation is not supported on this device!";
      break;

    case FP_DEVICE_ERROR_NOT_OPEN:
      msg = "The device needs to be opened first!";
      break;

    case FP_DEVICE_ERROR_ALREADY_OPEN:
      msg = "The device has already been opened!";
      break;

    case FP_DEVICE_ERROR_BUSY:
      msg = "The device is still busy with another operation, please try again later.";
      break;

    case FP_DEVICE_ERROR_PROTO:
      msg = "The driver encountered a protocol error with the device.";
      break;

    case FP_DEVICE_ERROR_DATA_INVALID:
      msg = "Passed (print) data is not valid.";
      break;

    case FP_DEVICE_ERROR_DATA_FULL:
      msg = "On device storage space is full.";
      break;

    case FP_DEVICE_ERROR_DATA_NOT_FOUND:
      msg = "Print was not found on the devices storage.";
      break;

    default:
      g_warning ("Unsupported error, returning general error instead!");
      error = FP_DEVICE_ERROR_GENERAL;
      msg = "An unspecified error occured!";
    }

  return g_error_new_literal (FP_DEVICE_ERROR, error, msg);
}

/**
 * fpi_device_retry_new_msg:
 * @error: The #FpDeviceRetry error value describing the issue
 * @msg: Custom message to use
 *
 * Create a new retry error code for use with fpi_device_verify_complete()
 * and similar calls.
 */
GError *
fpi_device_retry_new_msg (FpDeviceRetry error, const gchar *msg)
{
  return g_error_new_literal (FP_DEVICE_RETRY, error, msg);
}

/**
 * fpi_device_error_new_msg:
 * @error: The #FpDeviceRetry error value describing the issue
 * @msg: Custom message to use
 *
 * Create a new error code for use with fpi_device_verify_complete()
 * and similar calls.
 */
GError *
fpi_device_error_new_msg (FpDeviceError error, const gchar *msg)
{
  return g_error_new_literal (FP_DEVICE_ERROR, error, msg);
}

static gboolean
fp_device_cancel_in_idle_cb (gpointer user_data)
{
  FpDevice *self = user_data;
  FpDeviceClass *cls = FP_DEVICE_GET_CLASS (self);
  FpDevicePrivate *priv = fp_device_get_instance_private (self);

  g_assert (cls->cancel);
  g_assert (priv->current_action != FP_DEVICE_ACTION_NONE);

  g_debug ("Idle cancelling on ongoing operation!");

  priv->current_idle_cancel_source = NULL;

  cls->cancel (self);

  return G_SOURCE_REMOVE;
}

/* Notify the class that the task was cancelled; this should be connected
 * with the GTask as the user_data object for automatic cleanup together
 * with the task. */
static void
fp_device_cancelled_cb (GCancellable *cancellable, FpDevice *self)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (self);

  priv->current_idle_cancel_source = g_idle_source_new ();
  g_source_set_callback (priv->current_idle_cancel_source,
                         fp_device_cancel_in_idle_cb,
                         self,
                         NULL);
  g_source_attach (priv->current_idle_cancel_source, NULL);
  g_source_unref (priv->current_idle_cancel_source);
}

static void
maybe_cancel_on_cancelled (FpDevice     *device,
                           GCancellable *cancellable)
{
  FpDeviceClass *cls = FP_DEVICE_GET_CLASS (device);
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  if (!cancellable || !cls->cancel)
    return;

  priv->current_cancellable_id = g_cancellable_connect (cancellable,
                                                        G_CALLBACK (fp_device_cancelled_cb),
                                                        device,
                                                        NULL);
}

static void
clear_device_cancel_action (FpDevice *device)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_clear_pointer (&priv->current_idle_cancel_source, g_source_destroy);

  if (priv->current_cancellable_id)
    {
      g_cancellable_disconnect (g_task_get_cancellable (priv->current_task),
                                priv->current_cancellable_id);
      priv->current_cancellable_id = 0;
    }
}

static void
fp_device_constructed (GObject *object)
{
  FpDevice *self = (FpDevice *) object;
  FpDeviceClass *cls = FP_DEVICE_GET_CLASS (self);
  FpDevicePrivate *priv = fp_device_get_instance_private (self);

  priv->type = cls->type;
  if (cls->nr_enroll_stages)
    priv->nr_enroll_stages = cls->nr_enroll_stages;
  priv->scan_type = cls->scan_type;
  priv->device_name = g_strdup (cls->full_name);
  priv->device_id = g_strdup ("0");

  G_OBJECT_CLASS (fp_device_parent_class)->constructed (object);
}

static void
fp_device_finalize (GObject *object)
{
  FpDevice *self = (FpDevice *) object;
  FpDevicePrivate *priv = fp_device_get_instance_private (self);

  g_assert (priv->current_action == FP_DEVICE_ACTION_NONE);
  g_assert (priv->current_task == NULL);
  if (priv->is_open)
    g_warning ("User destroyed open device! Not cleaning up properly!");

  g_slist_free_full (priv->sources, (GDestroyNotify) g_source_destroy);

  g_clear_pointer (&priv->current_idle_cancel_source, g_source_destroy);
  g_clear_pointer (&priv->current_task_idle_return_source, g_source_destroy);

  g_clear_pointer (&priv->device_id, g_free);
  g_clear_pointer (&priv->device_name, g_free);

  G_OBJECT_CLASS (fp_device_parent_class)->finalize (object);
}

static void
fp_device_get_property (GObject    *object,
                        guint       prop_id,
                        GValue     *value,
                        GParamSpec *pspec)
{
  FpDevice *self = FP_DEVICE (object);
  FpDevicePrivate *priv = fp_device_get_instance_private (self);

  switch (prop_id)
    {
    case PROP_NR_ENROLL_STAGES:
      g_value_set_int (value, priv->nr_enroll_stages);
      break;

    case PROP_SCAN_TYPE:
      g_value_set_enum (value, priv->scan_type);
      break;

    case PROP_DRIVER:
      g_value_set_static_string (value, FP_DEVICE_GET_CLASS (priv)->id);
      break;

    case PROP_DEVICE_ID:
      g_value_set_string (value, priv->device_id);
      break;

    case PROP_NAME:
      g_value_set_string (value, priv->device_name);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
fp_device_set_property (GObject      *object,
                        guint         prop_id,
                        const GValue *value,
                        GParamSpec   *pspec)
{
  FpDevice *self = FP_DEVICE (object);
  FpDevicePrivate *priv = fp_device_get_instance_private (self);
  FpDeviceClass *cls = FP_DEVICE_GET_CLASS (self);

  /* _construct has not run yet, so we cannot use priv->type. */
  switch (prop_id)
    {
    case PROP_FPI_ENVIRON:
      if (cls->type == FP_DEVICE_TYPE_VIRTUAL)
        priv->virtual_env = g_value_dup_string (value);
      else
        g_assert (g_value_get_string (value) == NULL);
      break;

    case PROP_FPI_USB_DEVICE:
      if (cls->type == FP_DEVICE_TYPE_USB)
        priv->usb_device = g_value_dup_object (value);
      else
        g_assert (g_value_get_object (value) == NULL);
      break;

    case PROP_FPI_DRIVER_DATA:
      priv->driver_data = g_value_get_uint64 (value);
      break;

    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
fp_device_async_initable_init_async (GAsyncInitable     *initable,
                                     int                 io_priority,
                                     GCancellable       *cancellable,
                                     GAsyncReadyCallback callback,
                                     gpointer            user_data)
{
  g_autoptr(GTask) task = NULL;
  FpDevice *self = FP_DEVICE (initable);
  FpDevicePrivate *priv = fp_device_get_instance_private (self);

  /* It is next to impossible to call async_init at the wrong time. */
  g_assert (!priv->is_open);
  g_assert (!priv->current_task);

  task = g_task_new (self, cancellable, callback, user_data);
  if (g_task_return_error_if_cancelled (task))
    return;

  if (!FP_DEVICE_GET_CLASS (self)->probe)
    {
      g_task_return_boolean (task, TRUE);
      return;
    }

  priv->current_action = FP_DEVICE_ACTION_PROBE;
  priv->current_task = g_steal_pointer (&task);
  maybe_cancel_on_cancelled (self, cancellable);

  FP_DEVICE_GET_CLASS (self)->probe (self);
}

static gboolean
fp_device_async_initable_init_finish (GAsyncInitable *initable,
                                      GAsyncResult   *res,
                                      GError        **error)
{
  return g_task_propagate_boolean (G_TASK (res), error);
}

static void
fp_device_async_initable_iface_init (GAsyncInitableIface *iface)
{
  iface->init_async = fp_device_async_initable_init_async;
  iface->init_finish = fp_device_async_initable_init_finish;
}

static void
fp_device_class_init (FpDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = fp_device_constructed;
  object_class->finalize = fp_device_finalize;
  object_class->get_property = fp_device_get_property;
  object_class->set_property = fp_device_set_property;

  properties[PROP_NR_ENROLL_STAGES] =
    g_param_spec_uint ("nr-enroll-stages",
                       "EnrollStages",
                       "Number of enroll stages needed on the device",
                       0, G_MAXUINT,
                       0,
                       G_PARAM_STATIC_STRINGS | G_PARAM_READABLE);

  properties[PROP_SCAN_TYPE] =
    g_param_spec_enum ("scan-type",
                       "ScanType",
                       "The scan type of the device",
                       FP_TYPE_SCAN_TYPE, FP_SCAN_TYPE_SWIPE,
                       G_PARAM_STATIC_STRINGS | G_PARAM_READABLE);

  properties[PROP_DRIVER] =
    g_param_spec_string ("driver",
                         "Driver",
                         "String describing the driver",
                         NULL,
                         G_PARAM_STATIC_STRINGS | G_PARAM_READABLE);

  properties[PROP_DEVICE_ID] =
    g_param_spec_string ("device-id",
                         "Device ID",
                         "String describing the device, often generic but may be a serial number",
                         "",
                         G_PARAM_STATIC_STRINGS | G_PARAM_READABLE);

  properties[PROP_NAME] =
    g_param_spec_string ("name",
                         "Device Name",
                         "Human readable name for the device",
                         NULL,
                         G_PARAM_STATIC_STRINGS | G_PARAM_READABLE);

  properties[PROP_FPI_ENVIRON] =
    g_param_spec_string ("fp-environ",
                         "Virtual Environment",
                         "Private: The environment variable for the virtual device",
                         NULL,
                         G_PARAM_STATIC_STRINGS | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);

  properties[PROP_FPI_USB_DEVICE] =
    g_param_spec_object ("fp-usb-device",
                         "USB Device",
                         "Private: The USB device for the device",
                         G_USB_TYPE_DEVICE,
                         G_PARAM_STATIC_STRINGS | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);

  properties[PROP_FPI_DRIVER_DATA] =
    g_param_spec_uint64 ("fp-driver-data",
                         "Driver Data",
                         "Private: The driver data from the ID table entry",
                         0,
                         G_MAXUINT64,
                         0,
                         G_PARAM_STATIC_STRINGS | G_PARAM_WRITABLE | G_PARAM_CONSTRUCT_ONLY);

  g_object_class_install_properties (object_class, N_PROPS, properties);
}

static void
fp_device_init (FpDevice *self)
{
}

/**
 * fp_device_get_driver:
 * @device: A #FpDevice
 *
 * Returns: (transfer none): The ID of the driver
 */
const gchar *
fp_device_get_driver (FpDevice *device)
{
  g_return_val_if_fail (FP_IS_DEVICE (device), NULL);

  return FP_DEVICE_GET_CLASS (device)->id;
}

/**
 * fp_device_get_device_id:
 * @device: A #FpDevice
 *
 * Returns: (transfer none): The ID of the device
 */
const gchar *
fp_device_get_device_id (FpDevice *device)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_val_if_fail (FP_IS_DEVICE (device), NULL);

  return priv->device_id;
}

/**
 * fp_device_get_name:
 * @device: A #FpDevice
 *
 * Returns: (transfer none): The human readable name of the device
 */
const gchar *
fp_device_get_name (FpDevice *device)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_val_if_fail (FP_IS_DEVICE (device), NULL);

  return priv->device_name;
}

/**
 * fp_device_get_scan_type:
 * @device: A #FpDevice
 *
 * Retrieves the scan type of the device.
 *
 * Returns: The #FpScanType
 */
FpScanType
fp_device_get_scan_type (FpDevice *device)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_val_if_fail (FP_IS_DEVICE (device), FP_SCAN_TYPE_SWIPE);

  return priv->scan_type;
}

/**
 * fp_device_get_nr_enroll_stages:
 * @device: A #FpDevice
 *
 * Retrieves the number of enroll stages for this device.
 *
 * Returns: The number of enroll stages
 */
gint
fp_device_get_nr_enroll_stages (FpDevice *device)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_val_if_fail (FP_IS_DEVICE (device), -1);

  return priv->nr_enroll_stages;
}

/**
 * fp_device_supports_identify:
 * @device: A #FpDevice
 *
 * Check whether the device supports identification.
 *
 * Returns: Whether the device supports identification
 */
gboolean
fp_device_supports_identify (FpDevice *device)
{
  FpDeviceClass *cls = FP_DEVICE_GET_CLASS (device);

  g_return_val_if_fail (FP_IS_DEVICE (device), FALSE);

  return cls->identify != NULL;
}

/**
 * fp_device_supports_capture:
 * @device: A #FpDevice
 *
 * Check whether the device supports capturing images.
 *
 * Returns: Whether the device supports image capture
 */
gboolean
fp_device_supports_capture (FpDevice *device)
{
  FpDeviceClass *cls = FP_DEVICE_GET_CLASS (device);

  g_return_val_if_fail (FP_IS_DEVICE (device), FALSE);

  return cls->capture != NULL;
}

/**
 * fp_device_has_storage:
 * @device: A #FpDevice
 *
 * Whether the device has on-chip storage. If it has, you can list the
 * prints stored on the with fp_device_list_prints() and you should
 * always delete prints from the device again using
 * fp_device_delete_print().
 */
gboolean
fp_device_has_storage (FpDevice *device)
{
  FpDeviceClass *cls = FP_DEVICE_GET_CLASS (device);

  g_return_val_if_fail (FP_IS_DEVICE (device), FALSE);

  return cls->list != NULL;
}

/**
 * fp_device_open:
 * @device: a #FpDevice
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: the function to call on completion
 * @user_data: the data to pass to @callback
 *
 * Start an asynchronous operation to open the device. The callback will
 * be called once the operation has finished. Retrieve the result with
 * fp_device_open_finish().
 */
void
fp_device_open (FpDevice           *device,
                GCancellable       *cancellable,
                GAsyncReadyCallback callback,
                gpointer            user_data)
{
  g_autoptr(GTask) task = NULL;
  FpDevicePrivate *priv = fp_device_get_instance_private (device);
  GError *error = NULL;

  task = g_task_new (device, cancellable, callback, user_data);
  if (g_task_return_error_if_cancelled (task))
    return;

  if (priv->is_open)
    {
      g_task_return_error (task,
                           fpi_device_error_new (FP_DEVICE_ERROR_ALREADY_OPEN));
      return;
    }

  if (priv->current_task)
    {
      g_task_return_error (task,
                           fpi_device_error_new (FP_DEVICE_ERROR_BUSY));
      return;
    }

  switch (priv->type)
    {
    case FP_DEVICE_TYPE_USB:
      if (!g_usb_device_open (priv->usb_device, &error))
        {
          g_task_return_error (task, error);
          return;
        }
      break;

    case FP_DEVICE_TYPE_VIRTUAL:
      break;

    default:
      g_assert_not_reached ();
      g_task_return_error (task, fpi_device_error_new (FP_DEVICE_ERROR_GENERAL));
      return;
    }

  priv->current_action = FP_DEVICE_ACTION_OPEN;
  priv->current_task = g_steal_pointer (&task);
  maybe_cancel_on_cancelled (device, cancellable);

  FP_DEVICE_GET_CLASS (device)->open (device);
}

/**
 * fp_device_open_finish:
 * @device: A #FpDevice
 * @result: A #GAsyncResult
 * @error: Return location for errors, or %NULL to ignore
 *
 * Finish an asynchronous operation to open the device.
 * See fp_device_open().
 *
 * Returns: (type void): %FALSE on error, %TRUE otherwise
 */
gboolean
fp_device_open_finish (FpDevice     *device,
                       GAsyncResult *result,
                       GError      **error)
{
  return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * fp_device_close:
 * @device: a #FpDevice
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: the function to call on completion
 * @user_data: the data to pass to @callback
 *
 * Start an asynchronous operation to close the device. The callback will
 * be called once the operation has finished. Retrieve the result with
 * fp_device_close_finish().
 */
void
fp_device_close (FpDevice           *device,
                 GCancellable       *cancellable,
                 GAsyncReadyCallback callback,
                 gpointer            user_data)
{
  g_autoptr(GTask) task = NULL;
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  task = g_task_new (device, cancellable, callback, user_data);
  if (g_task_return_error_if_cancelled (task))
    return;

  if (!priv->is_open)
    {
      g_task_return_error (task,
                           fpi_device_error_new (FP_DEVICE_ERROR_NOT_OPEN));
      return;
    }

  if (priv->current_task)
    {
      g_task_return_error (task,
                           fpi_device_error_new (FP_DEVICE_ERROR_BUSY));
      return;
    }

  priv->current_action = FP_DEVICE_ACTION_CLOSE;
  priv->current_task = g_steal_pointer (&task);
  maybe_cancel_on_cancelled (device, cancellable);

  FP_DEVICE_GET_CLASS (device)->close (device);
}

/**
 * fp_device_close_finish:
 * @device: A #FpDevice
 * @result: A #GAsyncResult
 * @error: Return location for errors, or %NULL to ignore
 *
 * Finish an asynchronous operation to close the device.
 * See fp_device_close().
 *
 * Returns: (type void): %FALSE on error, %TRUE otherwise
 */
gboolean
fp_device_close_finish (FpDevice     *device,
                        GAsyncResult *result,
                        GError      **error)
{
  return g_task_propagate_boolean (G_TASK (result), error);
}


/**
 * fp_device_enroll:
 * @device: a #FpDevice
 * @template_print: (transfer floating): a #FpPrint
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @progress_cb: (nullable) (scope notified): progress reporting callback
 * @progress_data: (closure progress_cb): user data for @progress_cb
 * @progress_destroy: (destroy progress_data): Destroy notify for @progress_data
 * @callback: (scope async): the function to call on completion
 * @user_data: the data to pass to @callback
 *
 * Start an asynchronous operation to enroll a print. The callback will
 * be called once the operation has finished. Retrieve the result with
 * fp_device_enroll_finish().
 *
 * The @template_print parameter is a #FpPrint with available metadata filled
 * in. The driver may make use of this metadata, when e.g. storing the print on
 * device memory. It is undefined whether this print is filled in by the driver
 * and returned, or whether the driver will return a newly created print after
 * enrollment successed.
 */
void
fp_device_enroll (FpDevice           *device,
                  FpPrint            *template_print,
                  GCancellable       *cancellable,
                  FpEnrollProgress    progress_cb,
                  gpointer            progress_data,
                  GDestroyNotify      progress_destroy,
                  GAsyncReadyCallback callback,
                  gpointer            user_data)
{
  g_autoptr(GTask) task = NULL;
  FpDevicePrivate *priv = fp_device_get_instance_private (device);
  FpEnrollData *data;
  FpPrintType print_type;

  task = g_task_new (device, cancellable, callback, user_data);
  if (g_task_return_error_if_cancelled (task))
    return;

  if (!priv->is_open)
    {
      g_task_return_error (task,
                           fpi_device_error_new (FP_DEVICE_ERROR_NOT_OPEN));
      return;
    }

  if (priv->current_task)
    {
      g_task_return_error (task,
                           fpi_device_error_new (FP_DEVICE_ERROR_BUSY));
      return;
    }

  if (!FP_IS_PRINT (template_print))
    {
      g_warning ("User did not pass a print template!");
      g_task_return_error (task,
                           fpi_device_error_new (FP_DEVICE_ERROR_DATA_INVALID));
      return;
    }

  g_object_get (template_print, "fp-type", &print_type, NULL);
  if (print_type != FP_PRINT_UNDEFINED)
    {
      g_warning ("Passed print template must be newly created and blank!");
      g_task_return_error (task,
                           fpi_device_error_new (FP_DEVICE_ERROR_DATA_INVALID));
      return;
    }

  priv->current_action = FP_DEVICE_ACTION_ENROLL;
  priv->current_task = g_steal_pointer (&task);
  maybe_cancel_on_cancelled (device, cancellable);

  data = g_new0 (FpEnrollData, 1);
  data->print = g_object_ref_sink (template_print);
  data->enroll_progress_cb = progress_cb;
  data->enroll_progress_data = progress_data;

  // Attach the progress data as task data so that it is destroyed
  g_task_set_task_data (priv->current_task, data, enroll_data_free);

  FP_DEVICE_GET_CLASS (device)->enroll (device);
}

/**
 * fp_device_enroll_finish:
 * @device: A #FpDevice
 * @result: A #GAsyncResult
 * @error: Return location for errors, or %NULL to ignore
 *
 * Finish an asynchronous operation to enroll a print. You should check
 * for an error of type %FP_DEVICE_RETRY to prompt the user again if there
 * was an interaction issue.
 * See fp_device_enroll().
 *
 * Returns: (transfer full): The enrolled #FpPrint, or %NULL on error
 */
FpPrint *
fp_device_enroll_finish (FpDevice     *device,
                         GAsyncResult *result,
                         GError      **error)
{
  return g_task_propagate_pointer (G_TASK (result), error);
}

/**
 * fp_device_verify:
 * @device: a #FpDevice
 * @enrolled_print: a #FpPrint to verify
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: the function to call on completion
 * @user_data: the data to pass to @callback
 *
 * Start an asynchronous operation to close the device. The callback will
 * be called once the operation has finished. Retrieve the result with
 * fp_device_verify_finish().
 */
void
fp_device_verify (FpDevice           *device,
                  FpPrint            *enrolled_print,
                  GCancellable       *cancellable,
                  GAsyncReadyCallback callback,
                  gpointer            user_data)
{
  g_autoptr(GTask) task = NULL;
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  task = g_task_new (device, cancellable, callback, user_data);
  if (g_task_return_error_if_cancelled (task))
    return;

  if (!priv->is_open)
    {
      g_task_return_error (task,
                           fpi_device_error_new (FP_DEVICE_ERROR_NOT_OPEN));
      return;
    }

  if (priv->current_task)
    {
      g_task_return_error (task,
                           fpi_device_error_new (FP_DEVICE_ERROR_BUSY));
      return;
    }

  priv->current_action = FP_DEVICE_ACTION_VERIFY;
  priv->current_task = g_steal_pointer (&task);
  maybe_cancel_on_cancelled (device, cancellable);

  g_task_set_task_data (priv->current_task,
                        g_object_ref (enrolled_print),
                        g_object_unref);

  FP_DEVICE_GET_CLASS (device)->verify (device);
}

/**
 * fp_device_verify_finish:
 * @device: A #FpDevice
 * @result: A #GAsyncResult
 * @match: (out): Whether the user presented the correct finger
 * @print: (out) (transfer full) (nullable): Location to store the scanned print, or %NULL to ignore
 * @error: Return location for errors, or %NULL to ignore
 *
 * Finish an asynchronous operation to verify an enrolled print. You should check
 * for an error of type %FP_DEVICE_RETRY to prompt the user again if there
 * was an interaction issue.
 *
 * With @print you can fetch the newly created print and retrieve the image data if available.
 *
 * See fp_device_verify().
 *
 * Returns: (type void): %FALSE on error, %TRUE otherwise
 */
gboolean
fp_device_verify_finish (FpDevice     *device,
                         GAsyncResult *result,
                         gboolean     *match,
                         FpPrint     **print,
                         GError      **error)
{
  gint res;

  res = g_task_propagate_int (G_TASK (result), error);

  if (print)
    {
      *print = g_object_get_data (G_OBJECT (result), "print");
      if (*print)
        g_object_ref (*print);
    }

  if (match)
    *match = res == FPI_MATCH_SUCCESS;

  return res != FPI_MATCH_ERROR;
}

/**
 * fp_device_identify:
 * @device: a #FpDevice
 * @prints: (element-type FpPrint) (transfer none): #GPtrArray of #FpPrint
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: the function to call on completion
 * @user_data: the data to pass to @callback
 *
 * Start an asynchronous operation to identify prints. The callback will
 * be called once the operation has finished. Retrieve the result with
 * fp_device_identify_finish().
 */
void
fp_device_identify (FpDevice           *device,
                    GPtrArray          *prints,
                    GCancellable       *cancellable,
                    GAsyncReadyCallback callback,
                    gpointer            user_data)
{
  g_autoptr(GTask) task = NULL;
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  task = g_task_new (device, cancellable, callback, user_data);
  if (g_task_return_error_if_cancelled (task))
    return;

  if (!priv->is_open)
    {
      g_task_return_error (task,
                           fpi_device_error_new (FP_DEVICE_ERROR_NOT_OPEN));
      return;
    }

  if (priv->current_task)
    {
      g_task_return_error (task,
                           fpi_device_error_new (FP_DEVICE_ERROR_BUSY));
      return;
    }

  priv->current_action = FP_DEVICE_ACTION_IDENTIFY;
  priv->current_task = g_steal_pointer (&task);
  maybe_cancel_on_cancelled (device, cancellable);

  g_task_set_task_data (priv->current_task,
                        g_ptr_array_ref (prints),
                        (GDestroyNotify) g_ptr_array_unref);

  FP_DEVICE_GET_CLASS (device)->verify (device);
}

/**
 * fp_device_identify_finish:
 * @device: A #FpDevice
 * @result: A #GAsyncResult
 * @match: (out) (transfer full) (nullable): Location for the matched #FpPrint, or %NULL
 * @print: (out) (transfer full) (nullable): Location for the new #FpPrint, or %NULL
 * @error: Return location for errors, or %NULL to ignore
 *
 * Finish an asynchronous operation to identify a print. You should check
 * for an error of type %FP_DEVICE_RETRY to prompt the user again if there
 * was an interaction issue.
 *
 * Use @match to find the print that matched. With @print you can fetch the
 * newly created print and retrieve the image data if available.
 *
 * See fp_device_identify().
 *
 * Returns: (type void): %FALSE on error, %TRUE otherwise
 */
gboolean
fp_device_identify_finish (FpDevice     *device,
                           GAsyncResult *result,
                           FpPrint     **match,
                           FpPrint     **print,
                           GError      **error)
{
  if (print)
    {
      *print = g_object_get_data (G_OBJECT (result), "print");
      if (*print)
        g_object_ref (*print);
    }
  if (match)
    {
      *match = g_object_get_data (G_OBJECT (result), "match");
      if (*match)
        g_object_ref (*match);
    }

  return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * fp_device_capture:
 * @device: a #FpDevice
 * @wait_for_finger: Whether to wait for a finger or not
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: the function to call on completion
 * @user_data: the data to pass to @callback
 *
 * Start an asynchronous operation to capture an image. The callback will
 * be called once the operation has finished. Retrieve the result with
 * fp_device_capture_finish().
 */
void
fp_device_capture (FpDevice           *device,
                   gboolean            wait_for_finger,
                   GCancellable       *cancellable,
                   GAsyncReadyCallback callback,
                   gpointer            user_data)
{
  g_autoptr(GTask) task = NULL;
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  task = g_task_new (device, cancellable, callback, user_data);
  if (g_task_return_error_if_cancelled (task))
    return;

  if (!priv->is_open)
    {
      g_task_return_error (task,
                           fpi_device_error_new (FP_DEVICE_ERROR_NOT_OPEN));
      return;
    }

  if (priv->current_task)
    {
      g_task_return_error (task,
                           fpi_device_error_new (FP_DEVICE_ERROR_BUSY));
      return;
    }

  priv->current_action = FP_DEVICE_ACTION_CAPTURE;
  priv->current_task = g_steal_pointer (&task);
  maybe_cancel_on_cancelled (device, cancellable);

  priv->wait_for_finger = wait_for_finger;

  FP_DEVICE_GET_CLASS (device)->capture (device);
}

/**
 * fp_device_capture_finish:
 * @device: A #FpDevice
 * @result: A #GAsyncResult
 * @error: Return location for errors, or %NULL to ignore
 *
 * Finish an asynchronous operation to capture an image. You should check
 * for an error of type %FP_DEVICE_RETRY to prompt the user again if there
 * was an interaction issue.
 *
 * See fp_device_capture().
 *
 * Returns: (transfer full): #FpImage or %NULL on error
 */
FpImage *
fp_device_capture_finish (FpDevice     *device,
                          GAsyncResult *result,
                          GError      **error)
{
  return g_task_propagate_pointer (G_TASK (result), error);
}

/**
 * fp_device_delete_print:
 * @device: a #FpDevice
 * @enrolled_print: a #FpPrint to delete
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: the function to call on completion
 * @user_data: the data to pass to @callback
 *
 * Start an asynchronous operation to delete a print from the device.
 * The callback will be called once the operation has finished. Retrieve
 * the result with fp_device_delete_print_finish().
 *
 * This only makes sense on devices that store prints on-chip, but is safe
 * to always call.
 */
void
fp_device_delete_print (FpDevice           *device,
                        FpPrint            *enrolled_print,
                        GCancellable       *cancellable,
                        GAsyncReadyCallback callback,
                        gpointer            user_data)
{
  g_autoptr(GTask) task = NULL;
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  task = g_task_new (device, cancellable, callback, user_data);
  if (g_task_return_error_if_cancelled (task))
    return;

  if (!priv->is_open)
    {
      g_task_return_error (task,
                           fpi_device_error_new (FP_DEVICE_ERROR_NOT_OPEN));
      return;
    }

  if (priv->current_task)
    {
      g_task_return_error (task,
                           fpi_device_error_new (FP_DEVICE_ERROR_BUSY));
      return;
    }

  /* Succeed immediately if delete is not implemented. */
  if (!FP_DEVICE_GET_CLASS (device)->delete)
    {
      g_task_return_boolean (task, TRUE);
      return;
    }

  priv->current_action = FP_DEVICE_ACTION_DELETE;
  priv->current_task = g_steal_pointer (&task);
  maybe_cancel_on_cancelled (device, cancellable);

  g_task_set_task_data (priv->current_task,
                        g_object_ref (enrolled_print),
                        g_object_unref);

  FP_DEVICE_GET_CLASS (device)->delete (device);
}

/**
 * fp_device_delete_print_finish:
 * @device: A #FpDevice
 * @result: A #GAsyncResult
 * @error: Return location for errors, or %NULL to ignore
 *
 * Finish an asynchronous operation to delete an enrolled print.
 *
 * See fp_device_delete_print().
 *
 * Returns: (type void): %FALSE on error, %TRUE otherwise
 */
gboolean
fp_device_delete_print_finish (FpDevice     *device,
                               GAsyncResult *result,
                               GError      **error)
{
  return g_task_propagate_boolean (G_TASK (result), error);
}

/**
 * fp_device_list_prints:
 * @device: a #FpDevice
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @callback: the function to call on completion
 * @user_data: the data to pass to @callback
 *
 * Start an asynchronous operation to list all prints stored on the device.
 * This only makes sense on devices that store prints on-chip.
 *
 * Retrieve the result with fp_device_list_prints_finish().
 */
void
fp_device_list_prints (FpDevice           *device,
                       GCancellable       *cancellable,
                       GAsyncReadyCallback callback,
                       gpointer            user_data)
{
  g_autoptr(GTask) task = NULL;
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  task = g_task_new (device, cancellable, callback, user_data);
  if (g_task_return_error_if_cancelled (task))
    return;

  if (!priv->is_open)
    {
      g_task_return_error (task,
                           fpi_device_error_new (FP_DEVICE_ERROR_NOT_OPEN));
      return;
    }

  if (priv->current_task)
    {
      g_task_return_error (task,
                           fpi_device_error_new (FP_DEVICE_ERROR_BUSY));
      return;
    }

  priv->current_action = FP_DEVICE_ACTION_LIST;
  priv->current_task = g_steal_pointer (&task);
  maybe_cancel_on_cancelled (device, cancellable);

  FP_DEVICE_GET_CLASS (device)->list (device);
}

/**
 * fp_device_list_prints_finish:
 * @device: A #FpDevice
 * @result: A #GAsyncResult
 * @error: Return location for errors, or %NULL to ignore
 *
 * Finish an asynchronous operation to list all device stored prints.
 *
 * See fp_device_list_prints().
 *
 * Returns: (element-type FpPrint) (transfer container): Array of prints or %NULL on error
 */
GPtrArray *
fp_device_list_prints_finish (FpDevice     *device,
                              GAsyncResult *result,
                              GError      **error)
{
  return g_task_propagate_pointer (G_TASK (result), error);
}

typedef struct
{
  GSource       source;
  FpDevice     *device;
  FpTimeoutFunc callback;
  gpointer      user_data;
} FpDeviceTimeoutSource;

gboolean
device_timeout_cb (gpointer user_data)
{
  FpDeviceTimeoutSource *source = user_data;

  source->callback (source->device, source->user_data);

  return G_SOURCE_REMOVE;
}

void
timeout_finalize (GSource *source)
{
  FpDeviceTimeoutSource *timeout_source = (FpDeviceTimeoutSource *) source;
  FpDevicePrivate *priv;

  priv = fp_device_get_instance_private (timeout_source->device);
  priv->sources = g_slist_remove (priv->sources, source);
}

static gboolean
timeout_dispatch (GSource *source, GSourceFunc callback, gpointer user_data)
{
  FpDeviceTimeoutSource *timeout_source = (FpDeviceTimeoutSource *) source;

  ((FpTimeoutFunc) callback)(timeout_source->device, user_data);

  return G_SOURCE_REMOVE;
}

static GSourceFuncs timeout_funcs = {
  NULL, /* prepare */
  NULL, /* check */
  timeout_dispatch,
  timeout_finalize,
  NULL, NULL
};

/* Private API functions */

/**
 * fpi_device_set_nr_enroll_stages:
 * @device: The #FpDevice
 * @enroll_stages: The number of enroll stages
 *
 * Updates the reported number of enroll stages that the device needs.
 * If all supported devices have the same number of stages, then the
 * value can simply be set in the class.
 */
void
fpi_device_set_nr_enroll_stages (FpDevice *device,
                                 gint      enroll_stages)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_if_fail (FP_IS_DEVICE (device));

  priv->nr_enroll_stages = enroll_stages;
  g_object_notify_by_pspec (G_OBJECT (device), properties[PROP_NR_ENROLL_STAGES]);
}

/**
 * fpi_device_set_scan_type:
 * @device: The #FpDevice
 * @scan_type: The scan type of the device
 *
 * Updates the the scan type of the device from the default.
 * If all supported devices have the same scan type, then the
 * value can simply be set in the class.
 */
void
fpi_device_set_scan_type (FpDevice  *device,
                          FpScanType scan_type)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_if_fail (FP_IS_DEVICE (device));

  priv->scan_type = scan_type;
  g_object_notify_by_pspec (G_OBJECT (device), properties[PROP_SCAN_TYPE]);
}

/**
 * fpi_device_add_timeout:
 * @device: The #FpDevice
 * @interval: The interval in milliseconds
 * @func: The #FpTimeoutFunc to call on timeout
 * @user_data: User data to pass to the callback
 *
 * Register a timeout to run. Drivers should always make sure that timers are
 * cancelled when appropriate.
 *
 * Returns: (transfer none): A newly created and attached #GSource
 */
GSource *
fpi_device_add_timeout (FpDevice     *device,
                        gint          interval,
                        FpTimeoutFunc func,
                        gpointer      user_data)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);
  FpDeviceTimeoutSource *source;

  source = (FpDeviceTimeoutSource *) g_source_new (&timeout_funcs,
                                                   sizeof (FpDeviceTimeoutSource));
  source->device = device;
  source->user_data = user_data;

  g_source_attach (&source->source, NULL);
  g_source_set_callback (&source->source, (GSourceFunc) func, user_data, NULL);
  g_source_set_ready_time (&source->source,
                           g_source_get_time (&source->source) + interval * (guint64) 1000);
  priv->sources = g_slist_prepend (priv->sources, source);
  g_source_unref (&source->source);

  return &source->source;
}

/**
 * fpi_device_get_usb_device:
 * @device: The #FpDevice
 *
 * Get the #GUsbDevice for this #FpDevice. Only permissible to call if the
 * #FpDevice is of type %FP_DEVICE_TYPE_USB.
 *
 * Returns: The #GUsbDevice
 */
GUsbDevice *
fpi_device_get_usb_device (FpDevice *device)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_val_if_fail (FP_IS_DEVICE (device), NULL);
  g_return_val_if_fail (priv->type == FP_DEVICE_TYPE_USB, NULL);

  return priv->usb_device;
}

/**
 * fpi_device_get_virtual_env:
 * @device: The #FpDevice
 *
 * Get the value of the environment variable that caused the virtual #FpDevice to be
 * generated. Only permissible to call if the #FpDevice is of type %FP_DEVICE_TYPE_VIRTUAL.
 *
 * Returns: The value of the environment variable
 */
const gchar *
fpi_device_get_virtual_env (FpDevice *device)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_val_if_fail (FP_IS_DEVICE (device), NULL);
  g_return_val_if_fail (priv->type == FP_DEVICE_TYPE_VIRTUAL, NULL);

  return priv->virtual_env;
}

/**
 * fpi_device_get_current_action:
 * @device: The #FpDevice
 *
 * Get the currently ongoing action or %FP_DEVICE_ACTION_NONE if there
 * is no operation at this time.
 *
 * This is useful for drivers that might share code paths between different
 * actions (e.g. verify and identify) and want to find out again later which
 * action was started in the beginning.
 *
 * Returns: The ongoing #FpDeviceAction
 */
FpDeviceAction
fpi_device_get_current_action (FpDevice *device)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_val_if_fail (FP_IS_DEVICE (device), FP_DEVICE_ACTION_NONE);

  return priv->current_action;
}

/**
 * fpi_device_action_is_cancelled:
 * @device: The #FpDevice
 *
 * Checks whether the current action has been cancelled by the user.
 * This is equivalent to first getting the cancellable using
 * fpi_device_get_cancellable() and then checking whether it has been
 * cancelled (if it is non-NULL).
 *
 * Returns: %TRUE if action should be cancelled
 */
gboolean
fpi_device_action_is_cancelled (FpDevice *device)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);
  GCancellable *cancellable;

  g_return_val_if_fail (FP_IS_DEVICE (device), TRUE);
  g_return_val_if_fail (priv->current_action != FP_DEVICE_ACTION_NONE, TRUE);

  cancellable = g_task_get_cancellable (priv->current_task);

  return cancellable ? g_cancellable_is_cancelled (cancellable) : FALSE;
}

/**
 * fpi_device_get_driver_data:
 * @device: The #FpDevice
 *
 * Returns: The driver data from the #FpIdEntry table entry
 */
guint64
fpi_device_get_driver_data (FpDevice *device)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_val_if_fail (FP_IS_DEVICE (device), 0);

  return priv->driver_data;
}

/**
 * fpi_device_get_enroll_data:
 * @device: The #FpDevice
 * @print: (out) (transfer none): The user provided template print
 *
 * Get data for enrollment.
 */
void
fpi_device_get_enroll_data (FpDevice *device,
                            FpPrint **print)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);
  FpEnrollData *data;

  g_return_if_fail (FP_IS_DEVICE (device));
  g_return_if_fail (priv->current_action == FP_DEVICE_ACTION_ENROLL);

  data = g_task_get_task_data (priv->current_task);
  g_assert (data);

  if (print)
    *print = data->print;
}

/**
 * fpi_device_get_capture_data:
 * @device: The #FpDevice
 * @wait_for_finger: (out): Whether to wait for finger or not
 *
 * Get data for capture.
 */
void
fpi_device_get_capture_data (FpDevice *device,
                             gboolean *wait_for_finger)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_if_fail (FP_IS_DEVICE (device));
  g_return_if_fail (priv->current_action == FP_DEVICE_ACTION_CAPTURE);

  if (wait_for_finger)
    *wait_for_finger = priv->wait_for_finger;
}

/**
 * fpi_device_get_verify_data:
 * @device: The #FpDevice
 * @print: (out) (transfer none): The enrolled print
 *
 * Get data for verify.
 */
void
fpi_device_get_verify_data (FpDevice *device,
                            FpPrint **print)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_if_fail (FP_IS_DEVICE (device));
  g_return_if_fail (priv->current_action == FP_DEVICE_ACTION_VERIFY);

  if (print)
    *print = g_task_get_task_data (priv->current_task);
}

/**
 * fpi_device_get_identify_data:
 * @device: The #FpDevice
 * @prints: (out) (transfer none) (element-type FpPrint): The gallery of prints
 *
 * Get data for identify.
 */
void
fpi_device_get_identify_data (FpDevice   *device,
                              GPtrArray **prints)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_if_fail (FP_IS_DEVICE (device));
  g_return_if_fail (priv->current_action == FP_DEVICE_ACTION_IDENTIFY);

  if (prints)
    *prints = g_task_get_task_data (priv->current_task);
}

/**
 * fpi_device_get_delete_data:
 * @device: The #FpDevice
 * @print: (out) (transfer none): The print to delete
 *
 * Get data for delete.
 */
void
fpi_device_get_delete_data (FpDevice *device,
                            FpPrint **print)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_if_fail (FP_IS_DEVICE (device));
  g_return_if_fail (priv->current_action == FP_DEVICE_ACTION_DELETE);

  if (print)
    *print = g_task_get_task_data (priv->current_task);
}

/**
 * fpi_device_get_cancellable:
 * @device: The #FpDevice
 *
 * Retrieve the #GCancellable that may cancel the currently ongoing operation. This
 * is primarily useful to pass directly to e.g. fpi_usb_transfer_submit() for cancellable
 * transfers.
 * In many cases the cancel vfunc may be more convenient to react to cancellation in some
 * way.
 *
 * Returns: (transfer none): The #GCancellable for the current action.
 */
GCancellable *
fpi_device_get_cancellable (FpDevice *device)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_val_if_fail (FP_IS_DEVICE (device), NULL);
  g_return_val_if_fail (priv->current_action != FP_DEVICE_ACTION_NONE, NULL);

  return g_task_get_cancellable (priv->current_task);
}

/**
 * fpi_device_action_error:
 * @device: The #FpDevice
 * @error: The #GError to return
 *
 * Finish an ongoing action with an error. This is the same as calling
 * the corresponding complete function such as fpi_device_open_complete()
 * with an error set. If possible, use the correct complete function as
 * that results in improved error detection.
 */
void
fpi_device_action_error (FpDevice *device,
                         GError   *error)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_if_fail (FP_IS_DEVICE (device));
  g_return_if_fail (priv->current_action != FP_DEVICE_ACTION_NONE);

  if (error != NULL)
    {
      g_debug ("Device reported generic error during action; action was: %i", priv->current_action);
    }
  else
    {
      g_warning ("Device failed to pass an error to generic action error function");
      error = fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL, "Device reported error but did not provide an error condition");
    }


  switch (priv->current_action)
    {
    case FP_DEVICE_ACTION_PROBE:
      fpi_device_probe_complete (device, NULL, NULL, error);
      break;

    case FP_DEVICE_ACTION_OPEN:
      fpi_device_open_complete (device, error);
      break;

    case FP_DEVICE_ACTION_CLOSE:
      fpi_device_close_complete (device, error);
      break;

    case FP_DEVICE_ACTION_ENROLL:
      fpi_device_enroll_complete (device, NULL, error);
      break;

    case FP_DEVICE_ACTION_VERIFY:
      fpi_device_verify_complete (device, FPI_MATCH_ERROR, NULL, error);
      break;

    case FP_DEVICE_ACTION_IDENTIFY:
      fpi_device_identify_complete (device, NULL, NULL, error);
      break;

    case FP_DEVICE_ACTION_CAPTURE:
      fpi_device_capture_complete (device, NULL, error);
      break;

    case FP_DEVICE_ACTION_DELETE:
      fpi_device_delete_complete (device, error);
      break;

    case FP_DEVICE_ACTION_LIST:
      fpi_device_list_complete (device, NULL, error);
      break;

    default:
    case FP_DEVICE_ACTION_NONE:
      g_return_if_reached ();
      break;
    }
}

typedef enum _FpDeviceTaskReturnType {
  FP_DEVICE_TASK_RETURN_INT,
  FP_DEVICE_TASK_RETURN_BOOL,
  FP_DEVICE_TASK_RETURN_OBJECT,
  FP_DEVICE_TASK_RETURN_PTR_ARRAY,
  FP_DEVICE_TASK_RETURN_ERROR,
} FpDeviceTaskReturnType;

typedef struct _FpDeviceTaskReturnData
{
  FpDevice              *device;
  FpDeviceTaskReturnType type;
  gpointer               result;
} FpDeviceTaskReturnData;

static gboolean
fp_device_task_return_in_idle_cb (gpointer user_data)
{
  FpDeviceTaskReturnData *data = user_data;
  FpDevicePrivate *priv = fp_device_get_instance_private (data->device);

  g_autoptr(GTask) task = NULL;

  g_debug ("Completing action %d in idle!", priv->current_action);

  task = g_steal_pointer (&priv->current_task);
  priv->current_action = FP_DEVICE_ACTION_NONE;
  priv->current_task_idle_return_source = NULL;

  switch (data->type)
    {
    case FP_DEVICE_TASK_RETURN_INT:
      g_task_return_int (task, GPOINTER_TO_INT (data->result));
      break;

    case FP_DEVICE_TASK_RETURN_BOOL:
      g_task_return_boolean (task, GPOINTER_TO_UINT (data->result));
      break;

    case FP_DEVICE_TASK_RETURN_OBJECT:
      g_task_return_pointer (task, data->result, g_object_unref);
      break;

    case FP_DEVICE_TASK_RETURN_PTR_ARRAY:
      g_task_return_pointer (task, data->result,
                             (GDestroyNotify) g_ptr_array_unref);
      break;

    case FP_DEVICE_TASK_RETURN_ERROR:
      g_task_return_error (task, data->result);
      break;

    default:
      g_assert_not_reached ();
    }

  return G_SOURCE_REMOVE;
}

static void
fp_device_task_return_data_free (FpDeviceTaskReturnData *data)
{
  g_object_unref (data->device);
  g_free (data);
}

static void
fp_device_return_task_in_idle (FpDevice              *device,
                               FpDeviceTaskReturnType return_type,
                               gpointer               return_data)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);
  FpDeviceTaskReturnData *data;

  data = g_new0 (FpDeviceTaskReturnData, 1);
  data->device = g_object_ref (device);
  data->type = return_type;
  data->result = return_data;

  priv->current_task_idle_return_source = g_idle_source_new ();
  g_source_set_priority (priv->current_task_idle_return_source,
                         g_task_get_priority (priv->current_task));
  g_source_set_callback (priv->current_task_idle_return_source,
                         fp_device_task_return_in_idle_cb,
                         data,
                         (GDestroyNotify) fp_device_task_return_data_free);

  g_source_attach (priv->current_task_idle_return_source, NULL);
  g_source_unref (priv->current_task_idle_return_source);
}

/**
 * fpi_device_probe_complete:
 * @device: The #FpDevice
 * @device_id: Unique ID for the device or %NULL
 * @device_name: Human readable name or %NULL for driver name
 * @error: The #GError or %NULL on success
 *
 * Finish an ongoing probe operation. If error is %NULL success is assumed.
 */
void
fpi_device_probe_complete (FpDevice    *device,
                           const gchar *device_id,
                           const gchar *device_name,
                           GError      *error)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_if_fail (FP_IS_DEVICE (device));
  g_return_if_fail (priv->current_action == FP_DEVICE_ACTION_PROBE);

  g_debug ("Device reported probe completion");

  clear_device_cancel_action (device);

  if (!error)
    {
      if (device_id)
        {
          g_clear_pointer (&priv->device_id, g_free);
          priv->device_id = g_strdup (device_id);
          g_object_notify_by_pspec (G_OBJECT (device), properties[PROP_DEVICE_ID]);
        }
      if (device_name)
        {
          g_clear_pointer (&priv->device_name, g_free);
          priv->device_name = g_strdup (device_name);
          g_object_notify_by_pspec (G_OBJECT (device), properties[PROP_NAME]);
        }
      fp_device_return_task_in_idle (device, FP_DEVICE_TASK_RETURN_BOOL,
                                     GUINT_TO_POINTER (TRUE));
    }
  else
    {
      fp_device_return_task_in_idle (device, FP_DEVICE_TASK_RETURN_ERROR, error);
    }
}

/**
 * fpi_device_open_complete:
 * @device: The #FpDevice
 * @error: The #GError or %NULL on success
 *
 * Finish an ongoing open operation. If error is %NULL success is assumed.
 */
void
fpi_device_open_complete (FpDevice *device, GError *error)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_if_fail (FP_IS_DEVICE (device));
  g_return_if_fail (priv->current_action == FP_DEVICE_ACTION_OPEN);

  g_debug ("Device reported open completion");

  clear_device_cancel_action (device);

  if (!error)
    priv->is_open = TRUE;

  if (!error)
    fp_device_return_task_in_idle (device, FP_DEVICE_TASK_RETURN_BOOL,
                                   GUINT_TO_POINTER (TRUE));
  else
    fp_device_return_task_in_idle (device, FP_DEVICE_TASK_RETURN_ERROR, error);
}

/**
 * fpi_device_close_complete:
 * @device: The #FpDevice
 * @error: The #GError or %NULL on success
 *
 * Finish an ongoing close operation. If error is %NULL success is assumed.
 */
void
fpi_device_close_complete (FpDevice *device, GError *error)
{
  GError *nested_error = NULL;
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_if_fail (FP_IS_DEVICE (device));
  g_return_if_fail (priv->current_action == FP_DEVICE_ACTION_CLOSE);

  g_debug ("Device reported close completion");

  clear_device_cancel_action (device);
  priv->is_open = FALSE;

  switch (priv->type)
    {
    case FP_DEVICE_TYPE_USB:
      if (!g_usb_device_close (priv->usb_device, &nested_error))
        {
          if (error == NULL)
            error = nested_error;
          fp_device_return_task_in_idle (device, FP_DEVICE_TASK_RETURN_ERROR, error);
          return;
        }
      break;

    case FP_DEVICE_TYPE_VIRTUAL:
      break;

    default:
      g_assert_not_reached ();
      fp_device_return_task_in_idle (device, FP_DEVICE_TASK_RETURN_ERROR,
                                     fpi_device_error_new (FP_DEVICE_ERROR_GENERAL));
      return;
    }

  if (!error)
    fp_device_return_task_in_idle (device, FP_DEVICE_TASK_RETURN_BOOL,
                                   GUINT_TO_POINTER (TRUE));
  else
    fp_device_return_task_in_idle (device, FP_DEVICE_TASK_RETURN_ERROR, error);
}

/**
 * fpi_device_enroll_complete:
 * @device: The #FpDevice
 * @print: (nullable) (transfer full): The #FpPrint or %NULL on failure
 * @error: The #GError or %NULL on success
 *
 * Finish an ongoing enroll operation. The #FpPrint can be stored by the
 * caller for later verification.
 */
void
fpi_device_enroll_complete (FpDevice *device, FpPrint *print, GError *error)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_if_fail (FP_IS_DEVICE (device));
  g_return_if_fail (priv->current_action == FP_DEVICE_ACTION_ENROLL);

  g_debug ("Device reported enroll completion");

  clear_device_cancel_action (device);

  if (!error)
    {
      if (FP_IS_PRINT (print))
        {
          fp_device_return_task_in_idle (device, FP_DEVICE_TASK_RETURN_OBJECT, print);
        }
      else
        {
          g_warning ("Driver did not provide a valid print and failed to provide an error!");
          error = fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                            "Driver failed to provide print data!");
          fp_device_return_task_in_idle (device, FP_DEVICE_TASK_RETURN_ERROR, error);
        }
    }
  else
    {
      fp_device_return_task_in_idle (device, FP_DEVICE_TASK_RETURN_ERROR, error);
      if (FP_IS_PRINT (print))
        {
          g_warning ("Driver passed an error but also provided a print, returning error!");
          g_object_unref (print);
        }
    }
}

/**
 * fpi_device_verify_complete:
 * @device: The #FpDevice
 * @result: The #FpiMatchResult of the operation
 * @print: The scanned #FpPrint
 * @error: A #GError if result is %FPI_MATCH_ERROR
 *
 * Finish an ongoing verify operation. The returned print should be
 * representing the new scan and not the one passed for verification.
 */
void
fpi_device_verify_complete (FpDevice      *device,
                            FpiMatchResult result,
                            FpPrint       *print,
                            GError        *error)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_if_fail (FP_IS_DEVICE (device));
  g_return_if_fail (priv->current_action == FP_DEVICE_ACTION_VERIFY);

  g_debug ("Device reported verify completion");

  clear_device_cancel_action (device);

  g_object_set_data_full (G_OBJECT (priv->current_task),
                          "print",
                          print,
                          g_object_unref);

  if (!error)
    {
      if (result != FPI_MATCH_ERROR)
        {
          fp_device_return_task_in_idle (device, FP_DEVICE_TASK_RETURN_INT,
                                         GINT_TO_POINTER (result));
        }
      else
        {
          g_warning ("Driver did not provide an error for a failed verify operation!");
          error = fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                            "Driver failed to provide an error!");
          fp_device_return_task_in_idle (device, FP_DEVICE_TASK_RETURN_ERROR, error);
        }
    }
  else
    {
      fp_device_return_task_in_idle (device, FP_DEVICE_TASK_RETURN_ERROR, error);
      if (result != FPI_MATCH_ERROR)
        {
          g_warning ("Driver passed an error but also provided a match result, returning error!");
          g_object_unref (print);
        }
    }
}

/**
 * fpi_device_identify_complete:
 * @device: The #FpDevice
 * @match: The matching #FpPrint from the passed gallery, or %NULL if none matched
 * @print: The scanned #FpPrint, may be %NULL
 * @error: The #GError or %NULL on success
 *
 * Finish an ongoing identify operation. The match that was identified is
 * returned in @match. The @print parameter returns the newly created scan
 * that was used for matching.
 */
void
fpi_device_identify_complete (FpDevice *device,
                              FpPrint  *match,
                              FpPrint  *print,
                              GError   *error)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_if_fail (FP_IS_DEVICE (device));
  g_return_if_fail (priv->current_action == FP_DEVICE_ACTION_IDENTIFY);

  g_debug ("Device reported identify completion");

  clear_device_cancel_action (device);

  g_object_set_data_full (G_OBJECT (priv->current_task),
                          "print",
                          print,
                          g_object_unref);
  g_object_set_data_full (G_OBJECT (priv->current_task),
                          "match",
                          match,
                          g_object_unref);
  if (!error)
    {
      fp_device_return_task_in_idle (device, FP_DEVICE_TASK_RETURN_BOOL,
                                     GUINT_TO_POINTER (TRUE));
    }
  else
    {
      fp_device_return_task_in_idle (device, FP_DEVICE_TASK_RETURN_ERROR, error);
      if (match)
        {
          g_warning ("Driver passed an error but also provided a match result, returning error!");
          g_clear_object (&match);
        }
    }
}


/**
 * fpi_device_capture_complete:
 * @device: The #FpDevice
 * @image: The #FpImage, or %NULL on error
 * @error: The #GError or %NULL on success
 *
 * Finish an ongoing capture operation.
 */
void
fpi_device_capture_complete (FpDevice *device,
                             FpImage  *image,
                             GError   *error)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_if_fail (FP_IS_DEVICE (device));
  g_return_if_fail (priv->current_action == FP_DEVICE_ACTION_CAPTURE);

  g_debug ("Device reported capture completion");

  clear_device_cancel_action (device);

  if (!error)
    {
      if (image)
        {
          fp_device_return_task_in_idle (device, FP_DEVICE_TASK_RETURN_OBJECT, image);
        }
      else
        {
          g_warning ("Driver did not provide an error for a failed capture operation!");
          error = fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                            "Driver failed to provide an error!");
          fp_device_return_task_in_idle (device, FP_DEVICE_TASK_RETURN_ERROR, error);
        }
    }
  else
    {
      fp_device_return_task_in_idle (device, FP_DEVICE_TASK_RETURN_ERROR, error);
      if (image)
        {
          g_warning ("Driver passed an error but also provided an image, returning error!");
          g_clear_object (&image);
        }
    }
}

/**
 * fpi_device_delete_complete:
 * @device: The #FpDevice
 * @error: The #GError or %NULL on success
 *
 * Finish an ongoing delete operation.
 */
void
fpi_device_delete_complete (FpDevice *device,
                            GError   *error)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_if_fail (FP_IS_DEVICE (device));
  g_return_if_fail (priv->current_action == FP_DEVICE_ACTION_DELETE);

  g_debug ("Device reported deletion completion");

  clear_device_cancel_action (device);

  if (!error)
    fp_device_return_task_in_idle (device, FP_DEVICE_TASK_RETURN_BOOL,
                                   GUINT_TO_POINTER (TRUE));
  else
    fp_device_return_task_in_idle (device, FP_DEVICE_TASK_RETURN_ERROR, error);
}

/**
 * fpi_device_list_complete:
 * @device: The #FpDevice
 * @prints: (element-type FpPrint) (transfer container): Possibly empty array of prints or %NULL on error
 * @error: The #GError or %NULL on success
 *
 * Finish an ongoing list operation.
 *
 * Please note that the @prints array will be free'ed using
 * g_ptr_array_unref() and the elements are destroyed automatically.
 * As such, you must use g_ptr_array_new_with_free_func() with
 * g_object_unref() as free func to create the array.
 */
void
fpi_device_list_complete (FpDevice  *device,
                          GPtrArray *prints,
                          GError    *error)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);

  g_return_if_fail (FP_IS_DEVICE (device));
  g_return_if_fail (priv->current_action == FP_DEVICE_ACTION_LIST);

  g_debug ("Device reported listing completion");

  clear_device_cancel_action (device);

  if (prints && error)
    {
      g_warning ("Driver reported back prints and error, ignoring prints");
      g_clear_pointer (&prints, g_ptr_array_unref);
    }
  else if (!prints && !error)
    {
      g_warning ("Driver did not pass array but failed to provide an error");
      error = fpi_device_error_new_msg (FP_DEVICE_ERROR_GENERAL,
                                        "Driver failed to provide a list of prints");
    }

  if (!error)
    fp_device_return_task_in_idle (device, FP_DEVICE_TASK_RETURN_PTR_ARRAY, prints);
  else
    fp_device_return_task_in_idle (device, FP_DEVICE_TASK_RETURN_ERROR, error);
}

/**
 * fpi_device_enroll_progress:
 * @device: The #FpDevice
 * @completed_stages: The number of stages that are completed at this point
 * @print: The #FpPrint for the newly completed stage or %NULL on failure
 * @error: The #GError or %NULL on success
 *
 * Notify about the progress of the enroll operation. This is important for UI interaction.
 * The passed error may be used if a scan needs to be retried, use fpi_device_retry_new().
 */
void
fpi_device_enroll_progress (FpDevice *device,
                            gint      completed_stages,
                            FpPrint  *print,
                            GError   *error)
{
  FpDevicePrivate *priv = fp_device_get_instance_private (device);
  FpEnrollData *data;

  g_return_if_fail (FP_IS_DEVICE (device));
  g_return_if_fail (priv->current_action == FP_DEVICE_ACTION_ENROLL);
  g_return_if_fail (error == NULL || error->domain == FP_DEVICE_RETRY);

  g_debug ("Device reported enroll progress, reported %i of %i have been completed", completed_stages, priv->nr_enroll_stages);

  if (error && print)
    {
      g_warning ("Driver passed an error and also provided a print, returning error!");
      g_clear_object (&print);
    }

  data = g_task_get_task_data (priv->current_task);

  if (data->enroll_progress_cb)
    {
      data->enroll_progress_cb (device,
                                completed_stages,
                                print,
                                data->enroll_progress_data,
                                error);
    }
  if (error)
    g_error_free (error);
}


static void
async_result_ready (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  GTask **task = user_data;

  *task = g_object_ref (G_TASK (res));
}

/**
 * fp_device_open_sync:
 * @device: a #FpDevice
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @error: Return location for errors, or %NULL to ignore
 *
 * Open the device synchronously.
 *
 * Returns: (type void): %FALSE on error, %TRUE otherwise
 */
gboolean
fp_device_open_sync (FpDevice     *device,
                     GCancellable *cancellable,
                     GError      **error)
{
  g_autoptr(GAsyncResult) task = NULL;

  g_return_val_if_fail (FP_IS_DEVICE (device), FALSE);

  fp_device_open (device, cancellable, async_result_ready, &task);
  while (!task)
    g_main_context_iteration (NULL, TRUE);

  return fp_device_open_finish (device, task, error);
}

/**
 * fp_device_close_sync:
 * @device: a #FpDevice
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @error: Return location for errors, or %NULL to ignore
 *
 * Close the device synchronously.
 *
 * Returns: (type void): %FALSE on error, %TRUE otherwise
 */
gboolean
fp_device_close_sync (FpDevice     *device,
                      GCancellable *cancellable,
                      GError      **error)
{
  g_autoptr(GAsyncResult) task = NULL;

  g_return_val_if_fail (FP_IS_DEVICE (device), FALSE);

  fp_device_close (device, cancellable, async_result_ready, &task);
  while (!task)
    g_main_context_iteration (NULL, TRUE);

  return fp_device_close_finish (device, task, error);
}

/**
 * fp_device_enroll_sync:
 * @device: a #FpDevice
 * @template_print: (transfer floating): A #FpPrint to fill in or use
 *   as a template.
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @progress_cb: (nullable) (scope call): progress reporting callback
 * @progress_data: user data for @progress_cb
 * @error: Return location for errors, or %NULL to ignore
 *
 * Enroll a new print. See fp_device_enroll(). It is undefined whether
 * @template_print is updated or a newly created #FpPrint is returned.
 *
 * Returns:(transfer full): A #FpPrint on success, %NULL otherwise
 */
FpPrint *
fp_device_enroll_sync (FpDevice        *device,
                       FpPrint         *template_print,
                       GCancellable    *cancellable,
                       FpEnrollProgress progress_cb,
                       gpointer         progress_data,
                       GError         **error)
{
  g_autoptr(GAsyncResult) task = NULL;

  g_return_val_if_fail (FP_IS_DEVICE (device), FALSE);

  fp_device_enroll (device, template_print, cancellable,
                    progress_cb, progress_data, NULL,
                    async_result_ready, &task);
  while (!task)
    g_main_context_iteration (NULL, TRUE);

  return fp_device_enroll_finish (device, task, error);
}

/**
 * fp_device_verify_sync:
 * @device: a #FpDevice
 * @enrolled_print: a #FpPrint to verify
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @match: (out): Whether the user presented the correct finger
 * @print: (out) (transfer full) (nullable): Location to store the scanned print, or %NULL to ignore
 * @error: Return location for errors, or %NULL to ignore
 *
 * Verify a given print synchronously.
 *
 * Returns: (type void): %FALSE on error, %TRUE otherwise
 */
gboolean
fp_device_verify_sync (FpDevice     *device,
                       FpPrint      *enrolled_print,
                       GCancellable *cancellable,
                       gboolean     *match,
                       FpPrint     **print,
                       GError      **error)
{
  g_autoptr(GAsyncResult) task = NULL;

  g_return_val_if_fail (FP_IS_DEVICE (device), FALSE);

  fp_device_verify (device,
                    enrolled_print,
                    cancellable,
                    async_result_ready, &task);
  while (!task)
    g_main_context_iteration (NULL, TRUE);

  return fp_device_verify_finish (device, task, match, print, error);
}

/**
 * fp_device_identify_sync:
 * @device: a #FpDevice
 * @prints: (element-type FpPrint) (transfer none): #GPtrArray of #FpPrint
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @match: (out) (transfer full) (nullable): Location for the matched #FpPrint, or %NULL
 * @print: (out) (transfer full) (nullable): Location for the new #FpPrint, or %NULL
 * @error: Return location for errors, or %NULL to ignore
 *
 * Identify a print synchronously.
 *
 * Returns: (type void): %FALSE on error, %TRUE otherwise
 */
gboolean
fp_device_identify_sync (FpDevice     *device,
                         GPtrArray    *prints,
                         GCancellable *cancellable,
                         FpPrint     **match,
                         FpPrint     **print,
                         GError      **error)
{
  g_autoptr(GAsyncResult) task = NULL;

  g_return_val_if_fail (FP_IS_DEVICE (device), FALSE);

  fp_device_identify (device,
                      prints,
                      cancellable,
                      async_result_ready, &task);
  while (!task)
    g_main_context_iteration (NULL, TRUE);

  return fp_device_identify_finish (device, task, match, print, error);
}


/**
 * fp_device_capture_sync:
 * @device: a #FpDevice
 * @wait_for_finger: Whether to wait for a finger or not
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @error: Return location for errors, or %NULL to ignore
 *
 * Start an synchronous operation to capture an image.
 *
 * Returns: (transfer full): A new #FpImage or %NULL on error
 */
FpImage *
fp_device_capture_sync (FpDevice     *device,
                        gboolean      wait_for_finger,
                        GCancellable *cancellable,
                        GError      **error)
{
  g_autoptr(GAsyncResult) task = NULL;

  g_return_val_if_fail (FP_IS_DEVICE (device), FALSE);

  fp_device_capture (device,
                     wait_for_finger,
                     cancellable,
                     async_result_ready, &task);
  while (!task)
    g_main_context_iteration (NULL, TRUE);

  return fp_device_capture_finish (device, task, error);
}

/**
 * fp_device_delete_print_sync:
 * @device: a #FpDevice
 * @enrolled_print: a #FpPrint to verify
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @error: Return location for errors, or %NULL to ignore
 *
 * Delete a given print from the device.
 *
 * Returns: %FALSE on error, %TRUE otherwise
 */
gboolean
fp_device_delete_print_sync (FpDevice     *device,
                             FpPrint      *enrolled_print,
                             GCancellable *cancellable,
                             GError      **error)
{
  g_autoptr(GAsyncResult) task = NULL;

  g_return_val_if_fail (FP_IS_DEVICE (device), FALSE);

  fp_device_delete_print (device,
                          enrolled_print,
                          cancellable,
                          async_result_ready, &task);
  while (!task)
    g_main_context_iteration (NULL, TRUE);

  return fp_device_delete_print_finish (device, task, error);
}

/**
 * fp_device_list_prints_sync:
 * @device: a #FpDevice
 * @cancellable: (nullable): a #GCancellable, or %NULL
 * @error: Return location for errors, or %NULL to ignore
 *
 * List device stored prints synchronously.
 *
 * Returns: (element-type FpPrint) (transfer container): Array of prints, or %NULL on error
 */
GPtrArray *
fp_device_list_prints_sync (FpDevice     *device,
                            GCancellable *cancellable,
                            GError      **error)
{
  g_autoptr(GAsyncResult) task = NULL;

  g_return_val_if_fail (FP_IS_DEVICE (device), FALSE);

  fp_device_list_prints (device,
                         NULL,
                         async_result_ready, &task);
  while (!task)
    g_main_context_iteration (NULL, TRUE);

  return fp_device_list_prints_finish (device, task, error);
}
