/*
 * Virtual driver for device debugging
 *
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

#define FP_COMPONENT "fake_test_dev"

#include "test-device-fake.h"

G_DEFINE_TYPE (FpiDeviceFake, fpi_device_fake, FP_TYPE_DEVICE)

static const FpIdEntry driver_ids[] = {
  { .virtual_envvar = "FP_VIRTUAL_FAKE_DEVICE" },
  { .virtual_envvar = NULL }
};

static void
fpi_device_fake_probe (FpDevice *device)
{
  FpDeviceClass *dev_class = FP_DEVICE_GET_CLASS (device);
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);

  g_assert_cmpuint (fpi_device_get_current_action (device), ==, FP_DEVICE_ACTION_PROBE);

  fake_dev->last_called_function = fpi_device_fake_probe;
  fpi_device_probe_complete (device, dev_class->id, dev_class->full_name,
                             fake_dev->ret_error);
}

static void
fpi_device_fake_open (FpDevice *device)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);

  g_assert_cmpuint (fpi_device_get_current_action (device), ==, FP_DEVICE_ACTION_OPEN);

  fake_dev->last_called_function = fpi_device_fake_open;
  fpi_device_open_complete (device, fake_dev->ret_error);
}

static void
fpi_device_fake_close (FpDevice *device)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);

  g_assert_cmpuint (fpi_device_get_current_action (device), ==, FP_DEVICE_ACTION_CLOSE);

  fake_dev->last_called_function = fpi_device_fake_close;
  fpi_device_close_complete (device, fake_dev->ret_error);
}

static void
fpi_device_fake_enroll (FpDevice *device)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  FpPrint *print = fake_dev->ret_print;

  g_assert_cmpuint (fpi_device_get_current_action (device), ==, FP_DEVICE_ACTION_ENROLL);
  fpi_device_get_enroll_data (device, (FpPrint **) &fake_dev->action_data);

  if (!print && !fake_dev->ret_error)
    fpi_device_get_enroll_data (device, &print);

  fake_dev->last_called_function = fpi_device_fake_enroll;
  fpi_device_enroll_complete (device, print, fake_dev->ret_error);
}

static void
fpi_device_fake_verify (FpDevice *device)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  FpPrint *print = fake_dev->ret_print;

  g_assert_cmpuint (fpi_device_get_current_action (device), ==, FP_DEVICE_ACTION_VERIFY);
  fpi_device_get_verify_data (device, (FpPrint **) &fake_dev->action_data);

  if (!print && !fake_dev->ret_error)
    fpi_device_get_verify_data (device, &print);

  fake_dev->last_called_function = fpi_device_fake_verify;
  fpi_device_verify_complete (device, fake_dev->ret_result, print,
                              fake_dev->ret_error);
}

static void
fpi_device_fake_identify (FpDevice *device)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);
  FpPrint *match = fake_dev->ret_match;

  g_assert_cmpuint (fpi_device_get_current_action (device), ==, FP_DEVICE_ACTION_IDENTIFY);
  fpi_device_get_identify_data (device, (GPtrArray **) &fake_dev->action_data);

  if (!match && !fake_dev->ret_error)
    {
      GPtrArray *prints;
      unsigned int i;

      fpi_device_get_identify_data (device, &prints);

      for (i = 0; prints && i < prints->len; ++i)
        {
          FpPrint *print = g_ptr_array_index (prints, i);

          if (g_strcmp0 (fp_print_get_description (print), "fake-verified") == 0)
            {
              match = print;
              break;
            }
        }
    }

  fake_dev->last_called_function = fpi_device_fake_identify;
  fpi_device_identify_complete (device, match, fake_dev->ret_print,
                                fake_dev->ret_error);
}

static void
fpi_device_fake_capture (FpDevice *device)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);

  g_assert_cmpuint (fpi_device_get_current_action (device), ==, FP_DEVICE_ACTION_CAPTURE);
  fpi_device_get_capture_data (device, (gboolean *) &fake_dev->action_data);

  fake_dev->last_called_function = fpi_device_fake_capture;
  fpi_device_capture_complete (device, fake_dev->ret_image, fake_dev->ret_error);
}

static void
fpi_device_fake_list (FpDevice *device)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);

  g_assert_cmpuint (fpi_device_get_current_action (device), ==, FP_DEVICE_ACTION_LIST);

  fake_dev->last_called_function = fpi_device_fake_list;
  fpi_device_list_complete (device, fake_dev->ret_list, fake_dev->ret_error);
}

static void
fpi_device_fake_delete (FpDevice *device)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);

  g_assert_cmpuint (fpi_device_get_current_action (device), ==, FP_DEVICE_ACTION_DELETE);
  fpi_device_get_delete_data (device, (gpointer) & fake_dev->action_data);

  fake_dev->last_called_function = fpi_device_fake_delete;
  fpi_device_delete_complete (device, fake_dev->ret_error);
}

static void
fpi_device_fake_cancel (FpDevice *device)
{
  FpiDeviceFake *fake_dev = FPI_DEVICE_FAKE (device);

  g_assert_cmpuint (fpi_device_get_current_action (device), !=, FP_DEVICE_ACTION_NONE);

  fake_dev->last_called_function = fpi_device_fake_cancel;
}

static void
fpi_device_fake_init (FpiDeviceFake *self)
{
}

static void
fpi_device_fake_class_init (FpiDeviceFakeClass *klass)
{
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);

  dev_class->id = FP_COMPONENT;
  dev_class->full_name = "Virtual device for debugging";
  dev_class->type = FP_DEVICE_TYPE_VIRTUAL;
  dev_class->id_table = driver_ids;
  dev_class->nr_enroll_stages = 5;
  dev_class->scan_type = FP_SCAN_TYPE_PRESS;

  dev_class->probe = fpi_device_fake_probe;
  dev_class->open = fpi_device_fake_open;
  dev_class->close = fpi_device_fake_close;
  dev_class->enroll = fpi_device_fake_enroll;
  dev_class->verify = fpi_device_fake_verify;
  dev_class->identify = fpi_device_fake_identify;
  dev_class->capture = fpi_device_fake_capture;
  dev_class->list = fpi_device_fake_list;
  dev_class->delete = fpi_device_fake_delete;
  dev_class->cancel = fpi_device_fake_cancel;
}
