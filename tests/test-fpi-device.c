/*
 * Example fingerprint device prints listing and deletion
 * Enrolls your right index finger and saves the print to disk
 * Copyright (C) 2019 Marco Trevisan <marco.trevisan@canonical.com>
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

#include <libfprint/fprint.h>

#define FP_COMPONENT "device"

#include "fpi-device.h"
#include "fpi-log.h"
#include "test-device-fake.h"

/* Utility functions */

typedef FpDevice FpAutoCloseDevice;

static FpAutoCloseDevice *
auto_close_fake_device_new (void)
{
  FpAutoCloseDevice *device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);

  g_assert_true (fp_device_open_sync (device, NULL, NULL));

  return device;
}

static void
auto_close_fake_device_free (FpAutoCloseDevice *device)
{
  if (fp_device_is_open (device))
    g_assert_true (fp_device_close_sync (device, NULL, NULL));

  g_object_unref (device);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC (FpAutoCloseDevice, auto_close_fake_device_free)

typedef FpDeviceClass FpAutoResetClass;
static FpAutoResetClass default_fake_dev_class = {0};

static FpAutoResetClass *
auto_reset_device_class (void)
{
  g_autoptr(GTypeClass) type_class = NULL;
  FpDeviceClass *dev_class = g_type_class_peek_static (FPI_TYPE_DEVICE_FAKE);

  if (!dev_class)
    {
      type_class = g_type_class_ref (FPI_TYPE_DEVICE_FAKE);
      dev_class = (FpDeviceClass *) type_class;
      g_assert_nonnull (dev_class);
    }

  default_fake_dev_class = *dev_class;

  return dev_class;
}

static void
auto_reset_device_class_cleanup (FpAutoResetClass *dev_class)
{
  *dev_class = default_fake_dev_class;

  g_assert_cmpint (memcmp (dev_class, &default_fake_dev_class,
                           sizeof (FpAutoResetClass)), ==, 0);
}
G_DEFINE_AUTOPTR_CLEANUP_FUNC (FpAutoResetClass, auto_reset_device_class_cleanup)

static void
on_device_notify (FpDevice *device, GParamSpec *spec, gpointer user_data)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);

  fake_dev->last_called_function = on_device_notify;
  fake_dev->user_data = g_param_spec_ref (spec);
}

static void
test_driver_action_error_vfunc (FpDevice *device)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);

  fake_dev->last_called_function = test_driver_action_error_vfunc;

  fpi_device_action_error (device, fake_dev->user_data);
}

/* Tests */

static void
test_driver_get_driver (void)
{
  g_autoptr(FpAutoResetClass) dev_class = auto_reset_device_class ();
  g_autoptr(FpDevice) device = NULL;

  dev_class->id = "test-fpi-device-driver";
  device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);

  g_assert_cmpstr (fp_device_get_driver (device), ==, "test-fpi-device-driver");
}

static void
test_driver_get_device_id (void)
{
  g_autoptr(FpDevice) device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);

  g_assert_cmpstr (fp_device_get_device_id (device), ==, "0");
}

static void
test_driver_get_name (void)
{
  g_autoptr(FpAutoResetClass) dev_class = auto_reset_device_class ();
  g_autoptr(FpDevice) device = NULL;

  dev_class->full_name = "Test Device Full Name!";
  device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);

  g_assert_cmpstr (fp_device_get_name (device), ==, "Test Device Full Name!");
}

static void
test_driver_is_open (void)
{
  g_autoptr(FpDevice) device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);

  g_assert_false (fp_device_is_open (device));
  fp_device_open_sync (device, NULL, NULL);
  g_assert_true (fp_device_is_open (device));
  fp_device_close_sync (FP_DEVICE (device), NULL, NULL);
  g_assert_false (fp_device_is_open (device));
}

static void
test_driver_get_scan_type_press (void)
{
  g_autoptr(FpAutoResetClass) dev_class = auto_reset_device_class ();
  g_autoptr(FpDevice) device = NULL;

  dev_class->scan_type = FP_SCAN_TYPE_PRESS;
  device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);
  g_assert_cmpuint (fp_device_get_scan_type (device), ==, FP_SCAN_TYPE_PRESS);
}

static void
test_driver_get_scan_type_swipe (void)
{
  g_autoptr(FpAutoResetClass) dev_class = auto_reset_device_class ();
  g_autoptr(FpDevice) device = NULL;

  dev_class->scan_type = FP_SCAN_TYPE_SWIPE;
  device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);
  g_assert_cmpuint (fp_device_get_scan_type (device), ==, FP_SCAN_TYPE_SWIPE);
}

static void
test_driver_set_scan_type_press (void)
{
  g_autoptr(FpDevice) device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);
  g_autoptr(GParamSpec) pspec = NULL;
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);

  g_signal_connect (device, "notify::scan-type", G_CALLBACK (on_device_notify), NULL);

  fpi_device_set_scan_type (device, FP_SCAN_TYPE_PRESS);
  g_assert_cmpuint (fp_device_get_scan_type (device), ==, FP_SCAN_TYPE_PRESS);
  g_assert (fake_dev->last_called_function == on_device_notify);

  pspec = g_steal_pointer (&fake_dev->user_data);
  g_assert_cmpstr (pspec->name, ==, "scan-type");
}

static void
test_driver_set_scan_type_swipe (void)
{
  g_autoptr(FpDevice) device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);
  g_autoptr(GParamSpec) pspec = NULL;
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);

  g_signal_connect (device, "notify::scan-type", G_CALLBACK (on_device_notify), NULL);

  fpi_device_set_scan_type (device, FP_SCAN_TYPE_SWIPE);
  g_assert_cmpuint (fp_device_get_scan_type (device), ==, FP_SCAN_TYPE_SWIPE);
  g_assert (fake_dev->last_called_function == on_device_notify);

  pspec = g_steal_pointer (&fake_dev->user_data);
  g_assert_cmpstr (pspec->name, ==, "scan-type");
}

static void
test_driver_get_nr_enroll_stages (void)
{
  g_autoptr(FpAutoResetClass) dev_class = auto_reset_device_class ();
  g_autoptr(FpDevice) device = NULL;
  int expected_stages = g_random_int_range (G_MININT32, G_MAXINT32);

  dev_class->nr_enroll_stages = expected_stages;
  device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);

  g_assert_cmpint (fp_device_get_nr_enroll_stages (device), ==, expected_stages);
}

static void
test_driver_set_nr_enroll_stages (void)
{
  g_autoptr(FpDevice) device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);
  g_autoptr(GParamSpec) pspec = NULL;
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  int expected_stages = g_random_int_range (G_MININT32, G_MAXINT32);

  g_signal_connect (device, "notify::nr-enroll-stages", G_CALLBACK (on_device_notify), NULL);
  fpi_device_set_nr_enroll_stages (device, expected_stages);

  g_assert_cmpint (fp_device_get_nr_enroll_stages (device), ==, expected_stages);
  g_assert (fake_dev->last_called_function == on_device_notify);

  pspec = g_steal_pointer (&fake_dev->user_data);
  g_assert_cmpstr (pspec->name, ==, "nr-enroll-stages");
}

static void
test_driver_get_usb_device (void)
{
  g_autoptr(FpAutoResetClass) dev_class = auto_reset_device_class ();
  g_autoptr(FpDevice) device = NULL;

  dev_class->type = FP_DEVICE_TYPE_USB;
  device = g_object_new (FPI_TYPE_DEVICE_FAKE, "fp-usb-device", NULL);
  g_assert_null (fpi_device_get_usb_device (device));

  g_clear_object (&device);
  dev_class->type = FP_DEVICE_TYPE_VIRTUAL;
  device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);
  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,
                         "*assertion*type*FP_DEVICE_TYPE_USB*failed*");
  g_assert_null (fpi_device_get_usb_device (device));
  g_test_assert_expected_messages ();
}

static void
test_driver_get_virtual_env (void)
{
  g_autoptr(FpAutoResetClass) dev_class = auto_reset_device_class ();
  g_autoptr(FpDevice) device = NULL;

  dev_class->type = FP_DEVICE_TYPE_VIRTUAL;
  device = g_object_new (FPI_TYPE_DEVICE_FAKE, "fp-environ", "TEST_VIRTUAL_ENV_GETTER", NULL);
  g_assert_cmpstr (fpi_device_get_virtual_env (device), ==, "TEST_VIRTUAL_ENV_GETTER");

  g_clear_object (&device);
  dev_class->type = FP_DEVICE_TYPE_USB;
  device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);
  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,
                         "*assertion*type*FP_DEVICE_TYPE_VIRTUAL*failed*");
  g_assert_null (fpi_device_get_virtual_env (device));
  g_test_assert_expected_messages ();
}

static void
test_driver_get_driver_data (void)
{
  g_autoptr(FpDevice) device = NULL;
  guint64 driver_data;

  driver_data = g_random_int ();
  device = g_object_new (FPI_TYPE_DEVICE_FAKE, "fp-driver-data", driver_data, NULL);
  g_assert_cmpuint (fpi_device_get_driver_data (device), ==, driver_data);
}

static void
on_driver_probe_async (GObject *initable, GAsyncResult *res, gpointer user_data)
{
  g_autoptr(GError) error = NULL;
  FpDevice **out_device = user_data;
  FpDevice *device;
  FpDeviceClass *dev_class;
  FpiDeviceFake *fake_dev;

  device = FP_DEVICE (g_async_initable_new_finish (G_ASYNC_INITABLE (initable), res, &error));
  dev_class = FP_DEVICE_GET_CLASS (device);
  fake_dev = FPI_DEVICE_FAKE (device);

  g_assert (fake_dev->last_called_function == dev_class->probe);
  g_assert_no_error (error);

  g_assert_false (fp_device_is_open (device));

  *out_device = device;
}

static void
test_driver_probe (void)
{
  g_autoptr(FpAutoResetClass) dev_class = auto_reset_device_class ();
  g_autoptr(FpDevice) device = NULL;

  dev_class->id = "Probed device ID";
  dev_class->full_name = "Probed device name";
  g_async_initable_new_async (FPI_TYPE_DEVICE_FAKE, G_PRIORITY_DEFAULT, NULL,
                              on_driver_probe_async, &device, NULL);

  while (!FP_IS_DEVICE (device))
    g_main_context_iteration (NULL, TRUE);

  g_assert_false (fp_device_is_open (device));
  g_assert_cmpstr (fp_device_get_device_id (device), ==, "Probed device ID");
  g_assert_cmpstr (fp_device_get_name (device), ==, "Probed device name");
}

static void
test_driver_open (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(FpDevice) device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);
  FpDeviceClass *dev_class = FP_DEVICE_GET_CLASS (device);
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);

  g_assert (fake_dev->last_called_function != dev_class->probe);

  fp_device_open_sync (device, NULL, &error);
  g_assert (fake_dev->last_called_function == dev_class->open);
  g_assert_no_error (error);
  g_assert_true (fp_device_is_open (device));

  fp_device_close_sync (FP_DEVICE (device), NULL, &error);
  g_assert_no_error (error);
}

static void
test_driver_open_error (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(FpDevice) device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);
  FpDeviceClass *dev_class = FP_DEVICE_GET_CLASS (device);
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);

  fake_dev->ret_error = fpi_device_error_new (FP_DEVICE_ERROR_GENERAL);
  fp_device_open_sync (device, NULL, &error);
  g_assert (fake_dev->last_called_function == dev_class->open);
  g_assert_error (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_GENERAL);
  g_assert_false (fp_device_is_open (device));
  g_assert (error == g_steal_pointer (&fake_dev->ret_error));
}

static void
test_driver_close (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(FpAutoCloseDevice) device = auto_close_fake_device_new ();
  FpDeviceClass *dev_class = FP_DEVICE_GET_CLASS (device);
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);

  fp_device_close_sync (device, NULL, &error);
  g_assert (fake_dev->last_called_function == dev_class->close);

  g_assert_no_error (error);
  g_assert_false (fp_device_is_open (device));
}

static void
test_driver_close_error (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(FpAutoCloseDevice) device = auto_close_fake_device_new ();
  FpDeviceClass *dev_class = FP_DEVICE_GET_CLASS (device);
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);

  fake_dev->ret_error = fpi_device_error_new (FP_DEVICE_ERROR_GENERAL);
  fp_device_close_sync (device, NULL, &error);

  g_assert (fake_dev->last_called_function == dev_class->close);
  g_assert_error (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_GENERAL);
  g_assert (error == g_steal_pointer (&fake_dev->ret_error));
}

static void
test_driver_enroll (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(FpAutoCloseDevice) device = auto_close_fake_device_new ();
  FpDeviceClass *dev_class = FP_DEVICE_GET_CLASS (device);
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  FpPrint *template_print = fp_print_new (device);
  FpPrint *out_print = NULL;

  out_print =
    fp_device_enroll_sync (device, template_print, NULL, NULL, NULL, &error);

  g_assert (fake_dev->last_called_function == dev_class->enroll);
  g_assert (fake_dev->action_data == template_print);

  g_assert_no_error (error);
  g_assert (out_print == template_print);
}

static void
test_driver_enroll_error (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(FpAutoCloseDevice) device = auto_close_fake_device_new ();
  FpDeviceClass *dev_class = FP_DEVICE_GET_CLASS (device);
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  FpPrint *template_print = fp_print_new (device);
  FpPrint *out_print = NULL;

  fake_dev->ret_error = fpi_device_error_new (FP_DEVICE_ERROR_GENERAL);
  out_print =
    fp_device_enroll_sync (device, template_print, NULL, NULL, NULL, &error);

  g_assert (fake_dev->last_called_function == dev_class->enroll);
  g_assert_error (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_GENERAL);
  g_assert (error == g_steal_pointer (&fake_dev->ret_error));
  g_assert_null (out_print);
}

typedef struct
{
  gint     completed_stages;
  FpPrint *print;
  GError  *error;
} ExpectedEnrollData;

static void
test_driver_enroll_progress_callback (FpDevice *device,
                                      gint      completed_stages,
                                      FpPrint  *print,
                                      gpointer  user_data,
                                      GError   *error)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  ExpectedEnrollData *expected_data = user_data;

  g_assert_cmpint (expected_data->completed_stages, ==, completed_stages);
  g_assert (expected_data->print == print);
  g_assert_true (print == NULL || FP_IS_PRINT (print));
  g_assert (expected_data->error == error);

  fake_dev->last_called_function = test_driver_enroll_progress_callback;
}

static void
test_driver_enroll_progress_vfunc (FpDevice *device)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  ExpectedEnrollData *expected_data = fake_dev->user_data;

  g_autoptr(GError) error = NULL;

  expected_data->completed_stages =
    g_random_int_range (fp_device_get_nr_enroll_stages (device), G_MAXINT32);
  expected_data->print = fp_print_new (device);
  expected_data->error = NULL;

  g_object_add_weak_pointer (G_OBJECT (expected_data->print),
                             (gpointer) & expected_data->print);

  fpi_device_enroll_progress (device, expected_data->completed_stages,
                              expected_data->print, expected_data->error);
  g_assert (fake_dev->last_called_function == test_driver_enroll_progress_callback);
  g_assert_null (expected_data->print);


  expected_data->completed_stages =
    g_random_int_range (fp_device_get_nr_enroll_stages (device), G_MAXINT32);
  expected_data->print = NULL;
  expected_data->error = fpi_device_retry_new (FP_DEVICE_RETRY_TOO_SHORT);

  fpi_device_enroll_progress (device, expected_data->completed_stages,
                              expected_data->print, expected_data->error);
  g_assert (fake_dev->last_called_function == test_driver_enroll_progress_callback);


  expected_data->completed_stages =
    g_random_int_range (fp_device_get_nr_enroll_stages (device), G_MAXINT32);
  expected_data->print = fp_print_new (device);
  expected_data->error = NULL;

  error = fpi_device_error_new (FP_DEVICE_ERROR_GENERAL);
  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,
                         "*assertion*error*FP_DEVICE_RETRY*failed");
  fpi_device_enroll_progress (device, expected_data->completed_stages,
                              expected_data->print, error);
  g_assert (fake_dev->last_called_function == test_driver_enroll_progress_callback);
  g_clear_object (&expected_data->print);
  g_test_assert_expected_messages ();

  expected_data->completed_stages =
    g_random_int_range (fp_device_get_nr_enroll_stages (device), G_MAXINT32);
  expected_data->print = NULL;
  expected_data->error = fpi_device_retry_new (FP_DEVICE_RETRY_TOO_SHORT);

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                         "*Driver passed an error and also provided a print*");
  fpi_device_enroll_progress (device, expected_data->completed_stages,
                              fp_print_new (device), expected_data->error);
  g_assert (fake_dev->last_called_function == test_driver_enroll_progress_callback);
  g_test_assert_expected_messages ();

  default_fake_dev_class.enroll (device);
  fake_dev->last_called_function = test_driver_enroll_progress_vfunc;
}

static void
test_driver_enroll_progress (void)
{
  g_autoptr(FpAutoResetClass) dev_class = auto_reset_device_class ();
  g_autoptr(FpAutoCloseDevice) device = NULL;
  ExpectedEnrollData expected_enroll_data = {0};
  FpiDeviceFake *fake_dev;

  dev_class->nr_enroll_stages = g_random_int_range (10, G_MAXINT32);
  dev_class->enroll = test_driver_enroll_progress_vfunc;
  device = auto_close_fake_device_new ();

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,
                         "*assertion*current_action*FP_DEVICE_ACTION_ENROLL*failed");
  fpi_device_enroll_progress (device, 0, NULL, NULL);
  g_test_assert_expected_messages ();

  fake_dev = FPI_DEVICE_FAKE (device);
  fake_dev->user_data = &expected_enroll_data;

  fp_device_enroll_sync (device, fp_print_new (device), NULL,
                         test_driver_enroll_progress_callback, &expected_enroll_data, NULL);

  g_assert (fake_dev->last_called_function == test_driver_enroll_progress_vfunc);
}

static void
test_driver_verify (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(FpAutoCloseDevice) device = auto_close_fake_device_new ();
  g_autoptr(FpPrint) enrolled_print = fp_print_new (device);
  FpDeviceClass *dev_class = FP_DEVICE_GET_CLASS (device);
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  FpPrint *out_print = NULL;
  gboolean match;

  fake_dev->ret_result = FPI_MATCH_SUCCESS;
  fp_device_verify_sync (device, enrolled_print, NULL, &match, &out_print, &error);

  g_assert (fake_dev->last_called_function == dev_class->verify);
  g_assert (fake_dev->action_data == enrolled_print);
  g_assert_no_error (error);

  g_assert (out_print == enrolled_print);
  g_assert_true (match);
}

static void
test_driver_verify_fail (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(FpAutoCloseDevice) device = auto_close_fake_device_new ();
  g_autoptr(FpPrint) enrolled_print = fp_print_new (device);
  FpDeviceClass *dev_class = FP_DEVICE_GET_CLASS (device);
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  FpPrint *out_print = NULL;
  gboolean match;

  fake_dev->ret_result = FPI_MATCH_FAIL;
  fp_device_verify_sync (device, enrolled_print, NULL, &match, &out_print, &error);

  g_assert (fake_dev->last_called_function == dev_class->verify);
  g_assert_no_error (error);

  g_assert (out_print == enrolled_print);
  g_assert_false (match);
}

static void
test_driver_verify_error (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(FpAutoCloseDevice) device = auto_close_fake_device_new ();
  g_autoptr(FpPrint) enrolled_print = fp_print_new (device);
  FpDeviceClass *dev_class = FP_DEVICE_GET_CLASS (device);
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  FpPrint *out_print = NULL;
  gboolean match;

  fake_dev->ret_result = FPI_MATCH_ERROR;
  fake_dev->ret_error = fpi_device_error_new (FP_DEVICE_ERROR_GENERAL);
  fp_device_verify_sync (device, enrolled_print, NULL, &match, &out_print, &error);

  g_assert (fake_dev->last_called_function == dev_class->verify);
  g_assert_error (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_GENERAL);
  g_assert (error == g_steal_pointer (&fake_dev->ret_error));
  g_assert_false (match);
}

static void
fake_device_stub_identify (FpDevice *device)
{
}

static void
test_driver_supports_identify (void)
{
  g_autoptr(FpAutoResetClass) dev_class = auto_reset_device_class ();
  g_autoptr(FpDevice) device = NULL;

  dev_class->identify = fake_device_stub_identify;

  device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);
  g_assert_true (fp_device_supports_identify (device));
}

static void
test_driver_do_not_support_identify (void)
{
  g_autoptr(FpAutoResetClass) dev_class = auto_reset_device_class ();
  g_autoptr(FpDevice) device = NULL;

  dev_class->identify = NULL;

  device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);
  g_assert_false (fp_device_supports_identify (device));
}

static void
test_driver_identify (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(FpPrint) print = NULL;
  g_autoptr(FpAutoCloseDevice) device = auto_close_fake_device_new ();
  g_autoptr(GPtrArray) prints = g_ptr_array_new_with_free_func (g_object_unref);
  FpDeviceClass *dev_class = FP_DEVICE_GET_CLASS (device);
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  FpPrint *matched_print;
  FpPrint *expected_matched;
  unsigned int i;

  for (i = 0; i < 500; ++i)
    g_ptr_array_add (prints, fp_print_new (device));

  expected_matched = g_ptr_array_index (prints, g_random_int_range (0, 499));
  fp_print_set_description (expected_matched, "fake-verified");

  g_assert_true (fp_device_supports_identify (device));

  fake_dev->ret_print = fp_print_new (device);
  fp_device_identify_sync (device, prints, NULL, &matched_print, &print, &error);

  g_assert (fake_dev->last_called_function == dev_class->identify);
  g_assert (fake_dev->action_data == prints);
  g_assert_no_error (error);

  g_assert (print != NULL && print == fake_dev->ret_print);
  g_assert (expected_matched == matched_print);
}

static void
test_driver_identify_fail (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(FpPrint) print = NULL;
  g_autoptr(FpAutoCloseDevice) device = auto_close_fake_device_new ();
  g_autoptr(GPtrArray) prints = g_ptr_array_new_with_free_func (g_object_unref);
  FpDeviceClass *dev_class = FP_DEVICE_GET_CLASS (device);
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  FpPrint *matched_print;
  unsigned int i;

  for (i = 0; i < 500; ++i)
    g_ptr_array_add (prints, fp_print_new (device));

  g_assert_true (fp_device_supports_identify (device));

  fake_dev->ret_print = fp_print_new (device);
  fp_device_identify_sync (device, prints, NULL, &matched_print, &print, &error);

  g_assert (fake_dev->last_called_function == dev_class->identify);
  g_assert_no_error (error);

  g_assert (print != NULL && print == fake_dev->ret_print);
  g_assert_null (matched_print);
}

static void
test_driver_identify_error (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(FpPrint) print = NULL;
  g_autoptr(FpAutoCloseDevice) device = auto_close_fake_device_new ();
  g_autoptr(GPtrArray) prints = g_ptr_array_new_with_free_func (g_object_unref);
  FpDeviceClass *dev_class = FP_DEVICE_GET_CLASS (device);
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  FpPrint *matched_print;
  FpPrint *expected_matched;
  unsigned int i;

  for (i = 0; i < 500; ++i)
    g_ptr_array_add (prints, fp_print_new (device));

  expected_matched = g_ptr_array_index (prints, g_random_int_range (0, 499));
  fp_print_set_description (expected_matched, "fake-verified");

  g_assert_true (fp_device_supports_identify (device));

  fake_dev->ret_error = fpi_device_error_new (FP_DEVICE_ERROR_GENERAL);
  fp_device_identify_sync (device, prints, NULL, &matched_print, &print, &error);

  g_assert (fake_dev->last_called_function == dev_class->identify);
  g_assert_error (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_GENERAL);
  g_assert (error == g_steal_pointer (&fake_dev->ret_error));
  g_assert_null (matched_print);
  g_assert_null (print);
}

static void
fake_device_stub_capture (FpDevice *device)
{
}

static void
test_driver_supports_capture (void)
{
  g_autoptr(FpAutoResetClass) dev_class = auto_reset_device_class ();
  g_autoptr(FpDevice) device = NULL;

  dev_class->capture = fake_device_stub_capture;

  device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);
  g_assert_true (fp_device_supports_capture (device));
}

static void
test_driver_do_not_support_capture (void)
{
  g_autoptr(FpAutoResetClass) dev_class = auto_reset_device_class ();
  g_autoptr(FpDevice) device = NULL;

  dev_class->capture = NULL;

  device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);
  g_assert_false (fp_device_supports_capture (device));
}

static void
test_driver_capture (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(FpImage) image = NULL;
  g_autoptr(FpAutoCloseDevice) device = auto_close_fake_device_new ();
  FpDeviceClass *dev_class = FP_DEVICE_GET_CLASS (device);
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  gboolean wait_for_finger = TRUE;

  fake_dev->ret_image = fp_image_new (500, 500);
  image = fp_device_capture_sync (device, wait_for_finger, NULL, &error);
  g_assert (fake_dev->last_called_function == dev_class->capture);
  g_assert_true (GPOINTER_TO_UINT (fake_dev->action_data));
  g_assert_no_error (error);

  g_assert (image == fake_dev->ret_image);
}

static void
test_driver_capture_error (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(FpImage) image = NULL;
  g_autoptr(FpAutoCloseDevice) device = auto_close_fake_device_new ();
  FpDeviceClass *dev_class = FP_DEVICE_GET_CLASS (device);
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  gboolean wait_for_finger = TRUE;

  fake_dev->ret_error = fpi_device_error_new (FP_DEVICE_ERROR_GENERAL);
  image = fp_device_capture_sync (device, wait_for_finger, NULL, &error);
  g_assert (fake_dev->last_called_function == dev_class->capture);
  g_assert_error (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_GENERAL);
  g_assert (error == g_steal_pointer (&fake_dev->ret_error));

  g_assert_null (image);
}

static void
fake_device_stub_list (FpDevice *device)
{
}

static void
test_driver_has_storage (void)
{
  g_autoptr(FpAutoResetClass) dev_class = auto_reset_device_class ();
  g_autoptr(FpDevice) device = NULL;

  dev_class->list = fake_device_stub_list;

  device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);
  g_assert_true (fp_device_has_storage (device));
}

static void
test_driver_has_not_storage (void)
{
  g_autoptr(FpAutoResetClass) dev_class = auto_reset_device_class ();
  g_autoptr(FpDevice) device = NULL;

  dev_class->list = NULL;

  device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);
  g_assert_false (fp_device_has_storage (device));
}

static void
test_driver_list (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(FpAutoCloseDevice) device = auto_close_fake_device_new ();
  FpDeviceClass *dev_class = FP_DEVICE_GET_CLASS (device);
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  g_autoptr(GPtrArray) prints = g_ptr_array_new_with_free_func (g_object_unref);
  unsigned int i;

  for (i = 0; i < 500; ++i)
    g_ptr_array_add (prints, fp_print_new (device));

  fake_dev->ret_list = g_steal_pointer (&prints);
  prints = fp_device_list_prints_sync (device, NULL, &error);

  g_assert (fake_dev->last_called_function == dev_class->list);
  g_assert_no_error (error);

  g_assert (prints == fake_dev->ret_list);
}

static void
test_driver_list_error (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(FpAutoCloseDevice) device = auto_close_fake_device_new ();
  FpDeviceClass *dev_class = FP_DEVICE_GET_CLASS (device);
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  g_autoptr(GPtrArray) prints = NULL;

  fake_dev->ret_error = fpi_device_error_new (FP_DEVICE_ERROR_GENERAL);
  prints = fp_device_list_prints_sync (device, NULL, &error);

  g_assert (fake_dev->last_called_function == dev_class->list);
  g_assert_error (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_GENERAL);
  g_assert (error == g_steal_pointer (&fake_dev->ret_error));

  g_assert_null (prints);
}

static void
test_driver_delete (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(FpAutoCloseDevice) device = auto_close_fake_device_new ();
  g_autoptr(FpPrint) enrolled_print = fp_print_new (device);
  FpDeviceClass *dev_class = FP_DEVICE_GET_CLASS (device);
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  gboolean ret;

  ret = fp_device_delete_print_sync (device, enrolled_print, NULL, &error);
  g_assert (fake_dev->last_called_function == dev_class->delete);
  g_assert (fake_dev->action_data == enrolled_print);
  g_assert_no_error (error);
  g_assert_true (ret);
}

static void
test_driver_delete_error (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(FpAutoCloseDevice) device = auto_close_fake_device_new ();
  g_autoptr(FpPrint) enrolled_print = fp_print_new (device);
  FpDeviceClass *dev_class = FP_DEVICE_GET_CLASS (device);
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  gboolean ret;

  fake_dev->ret_error = fpi_device_error_new (FP_DEVICE_ERROR_GENERAL);
  ret = fp_device_delete_print_sync (device, enrolled_print, NULL, &error);
  g_assert (fake_dev->last_called_function == dev_class->delete);
  g_assert_error (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_GENERAL);
  g_assert (error == g_steal_pointer (&fake_dev->ret_error));

  g_assert_false (ret);
}

static gboolean
fake_device_delete_wait_for_cancel_timeout (gpointer data)
{
  FpDevice *device = data;
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  FpDeviceClass *dev_class = FP_DEVICE_GET_CLASS (device);

  g_assert (fake_dev->last_called_function == dev_class->cancel);
  default_fake_dev_class.delete (device);

  g_assert (fake_dev->last_called_function == default_fake_dev_class.delete);
  fake_dev->last_called_function = fake_device_delete_wait_for_cancel_timeout;

  return G_SOURCE_REMOVE;
}

static void
fake_device_delete_wait_for_cancel (FpDevice *device)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);

  fake_dev->last_called_function = fake_device_delete_wait_for_cancel;

  g_timeout_add (100, fake_device_delete_wait_for_cancel_timeout, device);
}

static void
on_driver_cancel_delete (GObject *obj, GAsyncResult *res, gpointer user_data)
{
  g_autoptr(GError) error = NULL;
  FpDevice *device = FP_DEVICE (obj);
  gboolean *completed = user_data;

  fp_device_delete_print_finish (device, res, &error);
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);

  *completed = TRUE;
}

static void
test_driver_cancel (void)
{
  g_autoptr(FpAutoResetClass) dev_class = auto_reset_device_class ();
  g_autoptr(FpAutoCloseDevice) device = NULL;
  g_autoptr(GCancellable) cancellable = NULL;
  g_autoptr(FpPrint) enrolled_print = NULL;
  gboolean completed = FALSE;
  FpiDeviceFake *fake_dev;

  dev_class->delete = fake_device_delete_wait_for_cancel;

  device = auto_close_fake_device_new ();
  fake_dev = FPI_DEVICE_FAKE (device);
  cancellable = g_cancellable_new ();
  enrolled_print = fp_print_new (device);

  fp_device_delete_print (device, enrolled_print, cancellable,
                          on_driver_cancel_delete, &completed);
  g_cancellable_cancel (cancellable);

  while (!completed)
    g_main_context_iteration (NULL, TRUE);

  g_assert (fake_dev->last_called_function == fake_device_delete_wait_for_cancel_timeout);
}

static void
test_driver_cancel_fail (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(FpAutoCloseDevice) device = auto_close_fake_device_new ();
  g_autoptr(GCancellable) cancellable = g_cancellable_new ();
  g_autoptr(FpPrint) enrolled_print = fp_print_new (device);
  FpDeviceClass *dev_class = FP_DEVICE_GET_CLASS (device);
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);

  fp_device_delete_print_sync (device, enrolled_print, cancellable, &error);
  g_assert (fake_dev->last_called_function == dev_class->delete);
  g_cancellable_cancel (cancellable);

  while (g_main_context_iteration (NULL, FALSE))
    ;

  g_assert (fake_dev->last_called_function == dev_class->delete);
  g_assert_no_error (error);
}

static void
test_driver_current_action (void)
{
  g_autoptr(FpDevice) device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);

  g_assert_cmpint (fpi_device_get_current_action (device), ==, FP_DEVICE_ACTION_NONE);
}

static void
test_driver_current_action_open_vfunc (FpDevice *device)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);

  g_assert_cmpuint (fpi_device_get_current_action (device), ==, FP_DEVICE_ACTION_OPEN);
  fake_dev->last_called_function = test_driver_current_action_open_vfunc;

  fpi_device_open_complete (device, NULL);
}

static void
test_driver_current_action_open (void)
{
  g_autoptr(FpAutoResetClass) dev_class = auto_reset_device_class ();
  g_autoptr(FpAutoCloseDevice) device = NULL;
  FpiDeviceFake *fake_dev;

  dev_class->open = test_driver_current_action_open_vfunc;
  device = auto_close_fake_device_new ();
  fake_dev = FPI_DEVICE_FAKE (device);
  g_assert (fake_dev->last_called_function == test_driver_current_action_open_vfunc);

  g_assert_cmpint (fpi_device_get_current_action (device), ==, FP_DEVICE_ACTION_NONE);
}

static void
test_driver_action_get_cancellable_open_vfunc (FpDevice *device)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);

  g_assert_cmpuint (fpi_device_get_current_action (device), ==, FP_DEVICE_ACTION_OPEN);
  fake_dev->last_called_function = test_driver_action_get_cancellable_open_vfunc;

  g_assert_true (G_IS_CANCELLABLE (fpi_device_get_cancellable (device)));

  fpi_device_open_complete (device, NULL);
}

static void
test_driver_action_get_cancellable_open (void)
{
  g_autoptr(FpAutoResetClass) dev_class = auto_reset_device_class ();
  g_autoptr(FpAutoCloseDevice) device = NULL;
  g_autoptr(GCancellable) cancellable = NULL;
  FpiDeviceFake *fake_dev;

  dev_class->open = test_driver_action_get_cancellable_open_vfunc;
  device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);
  fake_dev = FPI_DEVICE_FAKE (device);

  cancellable = g_cancellable_new ();
  fp_device_open_sync (device, cancellable, NULL);

  g_assert (fake_dev->last_called_function == test_driver_action_get_cancellable_open_vfunc);
}

static void
test_driver_action_get_cancellable_open_fail_vfunc (FpDevice *device)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);

  g_assert_cmpuint (fpi_device_get_current_action (device), ==, FP_DEVICE_ACTION_OPEN);
  fake_dev->last_called_function = test_driver_action_get_cancellable_open_fail_vfunc;

  g_assert_false (G_IS_CANCELLABLE (fpi_device_get_cancellable (device)));

  fpi_device_open_complete (device, NULL);
}

static void
test_driver_action_get_cancellable_open_fail (void)
{
  g_autoptr(FpAutoResetClass) dev_class = auto_reset_device_class ();
  g_autoptr(FpAutoCloseDevice) device = NULL;
  FpiDeviceFake *fake_dev;

  dev_class->open = test_driver_action_get_cancellable_open_fail_vfunc;
  device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);
  fake_dev = FPI_DEVICE_FAKE (device);

  g_assert_true (fp_device_open_sync (device, NULL, NULL));

  g_assert (fake_dev->last_called_function == test_driver_action_get_cancellable_open_fail_vfunc);
}

static void
test_driver_action_get_cancellable_error (void)
{
  g_autoptr(FpDevice) device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,
                         "*assertion*current_action*FP_DEVICE_ACTION_NONE*failed");
  g_assert_null (fpi_device_get_cancellable (device));
  g_test_assert_expected_messages ();
}

static void
test_driver_action_is_cancelled_open_vfunc (FpDevice *device)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);

  g_assert_cmpuint (fpi_device_get_current_action (device), ==, FP_DEVICE_ACTION_OPEN);
  fake_dev->last_called_function = test_driver_action_is_cancelled_open_vfunc;

  g_assert_true (G_IS_CANCELLABLE (fpi_device_get_cancellable (device)));
  g_assert_false (fpi_device_action_is_cancelled (device));

  g_cancellable_cancel (fpi_device_get_cancellable (device));
  g_assert_true (fpi_device_action_is_cancelled (device));

  fpi_device_open_complete (device, NULL);
}

static void
test_driver_action_is_cancelled_open (void)
{
  g_autoptr(FpAutoResetClass) dev_class = auto_reset_device_class ();
  g_autoptr(FpAutoCloseDevice) device = NULL;
  g_autoptr(GCancellable) cancellable = NULL;
  g_autoptr(GError) error = NULL;
  FpiDeviceFake *fake_dev;

  dev_class->open = test_driver_action_is_cancelled_open_vfunc;
  device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);
  fake_dev = FPI_DEVICE_FAKE (device);

  cancellable = g_cancellable_new ();
  g_assert_false (fp_device_open_sync (device, cancellable, &error));
  g_assert_error (error, G_IO_ERROR, G_IO_ERROR_CANCELLED);

  g_assert (fake_dev->last_called_function == test_driver_action_is_cancelled_open_vfunc);
}

static void
test_driver_action_is_cancelled_error (void)
{
  g_autoptr(FpDevice) device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,
                         "*assertion*current_action*FP_DEVICE_ACTION_NONE*failed");
  g_assert_true (fpi_device_action_is_cancelled (device));
  g_test_assert_expected_messages ();
}

static void
test_driver_complete_actions_errors (void)
{
  g_autoptr(FpDevice) device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,
                         "*assertion*current_action*failed");
  fpi_device_probe_complete (device, NULL, NULL, NULL);
  g_test_assert_expected_messages ();

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,
                         "*assertion*current_action*failed");
  fpi_device_open_complete (device, NULL);
  g_test_assert_expected_messages ();

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,
                         "*assertion*current_action*failed");
  fpi_device_close_complete (device, NULL);
  g_test_assert_expected_messages ();

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,
                         "*assertion*current_action*failed");
  fpi_device_enroll_complete (device, NULL, NULL);
  g_test_assert_expected_messages ();

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,
                         "*assertion*current_action*failed");
  fpi_device_verify_complete (device, FPI_MATCH_FAIL, NULL, NULL);
  g_test_assert_expected_messages ();

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,
                         "*assertion*current_action*failed");
  fpi_device_identify_complete (device, NULL, NULL, NULL);
  g_test_assert_expected_messages ();

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,
                         "*assertion*current_action*failed");
  fpi_device_capture_complete (device, NULL, NULL);
  g_test_assert_expected_messages ();

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,
                         "*assertion*current_action*failed");
  fpi_device_delete_complete (device, NULL);
  g_test_assert_expected_messages ();

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,
                         "*assertion*current_action*failed");
  fpi_device_list_complete (device, NULL, NULL);
  g_test_assert_expected_messages ();
}

static void
test_driver_action_error_error (void)
{
  g_autoptr(FpDevice) device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_CRITICAL,
                         "*assertion*current_action*FP_DEVICE_ACTION_NONE*failed");
  fpi_device_action_error (device, NULL);
  g_test_assert_expected_messages ();
}

static void
test_driver_action_error_open (void)
{
  g_autoptr(FpAutoResetClass) dev_class = auto_reset_device_class ();
  g_autoptr(FpAutoCloseDevice) device = NULL;
  g_autoptr(GError) error = NULL;
  FpiDeviceFake *fake_dev;

  dev_class->open = test_driver_action_error_vfunc;
  device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);
  fake_dev = FPI_DEVICE_FAKE (device);
  fake_dev->user_data = fpi_device_error_new (FP_DEVICE_ERROR_DATA_INVALID);

  g_assert_false (fp_device_open_sync (device, NULL, &error));
  g_assert_error (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_DATA_INVALID);

  g_assert (fake_dev->last_called_function == test_driver_action_error_vfunc);
}

static void
test_driver_action_error_fallback_open (void)
{
  g_autoptr(FpAutoResetClass) dev_class = auto_reset_device_class ();
  g_autoptr(FpAutoCloseDevice) device = NULL;
  g_autoptr(GError) error = NULL;
  FpiDeviceFake *fake_dev;

  dev_class->open = test_driver_action_error_vfunc;
  device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);
  fake_dev = FPI_DEVICE_FAKE (device);

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING,
                         "*Device failed to pass an error to generic action "
                         "error function*");

  g_assert_false (fp_device_open_sync (device, NULL, &error));
  g_assert_error (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_GENERAL);

  g_assert (fake_dev->last_called_function == test_driver_action_error_vfunc);
  g_test_assert_expected_messages ();
}

static void
test_driver_add_timeout_func (FpDevice *device, gpointer user_data)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);

  fake_dev->last_called_function = test_driver_add_timeout_func;
}

static void
test_driver_add_timeout (void)
{
  g_autoptr(FpDevice) device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  FpDevice *data_check = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);

  g_object_add_weak_pointer (G_OBJECT (data_check), (gpointer) & data_check);
  fpi_device_add_timeout (device, 50, test_driver_add_timeout_func,
                          data_check, g_object_unref);

  g_assert_nonnull (data_check);

  while (FP_IS_DEVICE (data_check))
    g_main_context_iteration (NULL, TRUE);

  g_assert_null (data_check);
  g_assert (fake_dev->last_called_function == test_driver_add_timeout_func);
}

static gboolean
test_driver_add_timeout_cancelled_timeout (gpointer data)
{
  GSource *source = data;

  g_source_destroy (source);

  return G_SOURCE_REMOVE;
}

static void
test_driver_add_timeout_cancelled (void)
{
  g_autoptr(FpDevice) device = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  FpDevice *data_check = g_object_new (FPI_TYPE_DEVICE_FAKE, NULL);
  GSource *source;

  g_object_add_weak_pointer (G_OBJECT (data_check), (gpointer) & data_check);
  source = fpi_device_add_timeout (device, 2000, test_driver_add_timeout_func,
                                   data_check, g_object_unref);

  g_timeout_add (20, test_driver_add_timeout_cancelled_timeout, source);
  g_assert_nonnull (data_check);

  while (FP_IS_DEVICE (data_check))
    g_main_context_iteration (NULL, TRUE);

  g_assert_null (data_check);
  g_assert_null (fake_dev->last_called_function);
}

static void
test_driver_error_types (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GEnumClass) errors_enum = g_type_class_ref (FP_TYPE_DEVICE_ERROR);
  int i;

  for (i = 0; g_enum_get_value (errors_enum, i); ++i)
    {
      error = fpi_device_error_new (i);
      g_assert_error (error, FP_DEVICE_ERROR, i);
      g_clear_error (&error);
    }

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, "*Unsupported error*");
  error = fpi_device_error_new (i + 1);
  g_assert_error (error, FP_DEVICE_ERROR, FP_DEVICE_ERROR_GENERAL);
  g_test_assert_expected_messages ();
}

static void
test_driver_retry_error_types (void)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GEnumClass) errors_enum = g_type_class_ref (FP_TYPE_DEVICE_RETRY);
  int i;

  for (i = 0; g_enum_get_value (errors_enum, i); ++i)
    {
      error = fpi_device_retry_new (i);
      g_assert_error (error, FP_DEVICE_RETRY, i);
      g_clear_error (&error);
    }

  g_test_expect_message (G_LOG_DOMAIN, G_LOG_LEVEL_WARNING, "*Unsupported error*");
  error = fpi_device_retry_new (i + 1);
  g_assert_error (error, FP_DEVICE_RETRY, FP_DEVICE_RETRY_GENERAL);
  g_test_assert_expected_messages ();
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/driver/get_driver", test_driver_get_driver);
  g_test_add_func ("/driver/get_device_id", test_driver_get_device_id);
  g_test_add_func ("/driver/get_name", test_driver_get_name);
  g_test_add_func ("/driver/is_open", test_driver_is_open);
  g_test_add_func ("/driver/get_scan_type/press", test_driver_get_scan_type_press);
  g_test_add_func ("/driver/get_scan_type/swipe", test_driver_get_scan_type_swipe);
  g_test_add_func ("/driver/set_scan_type/press", test_driver_set_scan_type_press);
  g_test_add_func ("/driver/set_scan_type/swipe", test_driver_set_scan_type_swipe);
  g_test_add_func ("/driver/get_nr_enroll_stages", test_driver_get_nr_enroll_stages);
  g_test_add_func ("/driver/set_nr_enroll_stages", test_driver_set_nr_enroll_stages);
  g_test_add_func ("/driver/supports_identify", test_driver_supports_identify);
  g_test_add_func ("/driver/supports_capture", test_driver_supports_capture);
  g_test_add_func ("/driver/has_storage", test_driver_has_storage);
  g_test_add_func ("/driver/do_not_support_identify", test_driver_do_not_support_identify);
  g_test_add_func ("/driver/do_not_support_capture", test_driver_do_not_support_capture);
  g_test_add_func ("/driver/has_not_storage", test_driver_has_not_storage);
  g_test_add_func ("/driver/get_usb_device", test_driver_get_usb_device);
  g_test_add_func ("/driver/get_virtual_env", test_driver_get_virtual_env);
  g_test_add_func ("/driver/get_driver_data", test_driver_get_driver_data);

  g_test_add_func ("/driver/probe", test_driver_probe);
  g_test_add_func ("/driver/open", test_driver_open);
  g_test_add_func ("/driver/open/error", test_driver_open_error);
  g_test_add_func ("/driver/close", test_driver_close);
  g_test_add_func ("/driver/close/error", test_driver_close_error);
  g_test_add_func ("/driver/enroll", test_driver_enroll);
  g_test_add_func ("/driver/enroll/error", test_driver_enroll_error);
  g_test_add_func ("/driver/enroll/progress", test_driver_enroll_progress);
  g_test_add_func ("/driver/verify", test_driver_verify);
  g_test_add_func ("/driver/verify/fail", test_driver_verify_fail);
  g_test_add_func ("/driver/verify/error", test_driver_verify_error);
  g_test_add_func ("/driver/identify", test_driver_identify);
  g_test_add_func ("/driver/identify/fail", test_driver_identify_fail);
  g_test_add_func ("/driver/identify/error", test_driver_identify_error);
  g_test_add_func ("/driver/capture", test_driver_capture);
  g_test_add_func ("/driver/capture/error", test_driver_capture_error);
  g_test_add_func ("/driver/list", test_driver_list);
  g_test_add_func ("/driver/list/error", test_driver_list_error);
  g_test_add_func ("/driver/delete", test_driver_delete);
  g_test_add_func ("/driver/delete/error", test_driver_delete_error);
  g_test_add_func ("/driver/cancel", test_driver_cancel);
  g_test_add_func ("/driver/cancel/fail", test_driver_cancel_fail);

  g_test_add_func ("/driver/get_current_action", test_driver_current_action);
  g_test_add_func ("/driver/get_current_action/open", test_driver_current_action_open);
  g_test_add_func ("/driver/get_cancellable/error", test_driver_action_get_cancellable_error);
  g_test_add_func ("/driver/get_cancellable/open", test_driver_action_get_cancellable_open);
  g_test_add_func ("/driver/get_cancellable/open/fail", test_driver_action_get_cancellable_open_fail);
  g_test_add_func ("/driver/action_is_cancelled/open", test_driver_action_is_cancelled_open);
  g_test_add_func ("/driver/action_is_cancelled/error", test_driver_action_is_cancelled_error);
  g_test_add_func ("/driver/complete_action/all/error", test_driver_complete_actions_errors);
  g_test_add_func ("/driver/action_error/error", test_driver_action_error_error);
  g_test_add_func ("/driver/action_error/open", test_driver_action_error_open);
  g_test_add_func ("/driver/action_error/fail/open", test_driver_action_error_fallback_open);

  g_test_add_func ("/driver/timeout", test_driver_add_timeout);
  g_test_add_func ("/driver/timeout/cancelled", test_driver_add_timeout_cancelled);

  g_test_add_func ("/driver/error_types", test_driver_error_types);
  g_test_add_func ("/driver/retry_error_types", test_driver_retry_error_types);

  return g_test_run ();
}
