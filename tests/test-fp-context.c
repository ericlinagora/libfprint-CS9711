/*
 * FpContext Unit tests
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

#include "test-utils.h"

static void
test_context_new (void)
{
  g_autoptr(FpContext) context = fp_context_new ();
  g_assert_true (FP_CONTEXT (context));
}

static void
test_context_has_no_devices (void)
{
  g_autoptr(FpContext) context = NULL;
  GPtrArray *devices;

  context = fp_context_new ();
  devices = fp_context_get_devices (context);

  g_assert_nonnull (devices);
  g_assert_cmpuint (devices->len, ==, 0);
}

static void
test_context_has_virtual_device (void)
{
  g_autoptr(FpContext) context = NULL;
  FpDevice *virtual_device = NULL;
  GPtrArray *devices;
  unsigned int i;

  fpt_setup_virtual_device_environment ();

  context = fp_context_new ();
  devices = fp_context_get_devices (context);

  g_assert_nonnull (devices);
  g_assert_cmpuint (devices->len, ==, 1);

  for (i = 0; i < devices->len; ++i)
    {
      FpDevice *device = devices->pdata[i];

      if (g_strcmp0 (fp_device_get_driver (device), "virtual_image") == 0)
        {
          virtual_device = device;
          break;
        }
    }

  g_assert_true (FP_IS_DEVICE (virtual_device));

  fpt_teardown_virtual_device_environment ();
}

static void
test_context_enumerates_new_devices (void)
{
  g_autoptr(FpContext) context = NULL;
  GPtrArray *devices;

  context = fp_context_new ();

  fpt_setup_virtual_device_environment ();

  fp_context_enumerate (context);
  devices = fp_context_get_devices (context);

  g_assert_nonnull (devices);
  g_assert_cmpuint (devices->len, ==, 1);

  fpt_teardown_virtual_device_environment ();
}

int
main (int argc, char *argv[])
{
  g_test_init (&argc, &argv, NULL);

  g_test_add_func ("/context/new", test_context_new);
  g_test_add_func ("/context/no-devices", test_context_has_no_devices);
  g_test_add_func ("/context/has-virtual-device", test_context_has_virtual_device);
  g_test_add_func ("/context/enumerates-new-devices", test_context_enumerates_new_devices);

  return g_test_run ();
}
