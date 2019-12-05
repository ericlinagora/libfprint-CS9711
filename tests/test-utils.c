/*
 * Unit tests for libfprint
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
#include <glib/gstdio.h>

#include "test-utils.h"

void
fpt_teardown_virtual_device_environment (void)
{
  const char *path = g_getenv ("FP_VIRTUAL_IMAGE");

  if (path)
    {
      g_autofree char *temp_dir = g_path_get_dirname (path);

      g_unsetenv ("FP_VIRTUAL_IMAGE");
      g_unlink (path);
      g_rmdir (temp_dir);
    }
}

static void
on_signal_event (int sig)
{
  fpt_teardown_virtual_device_environment ();
}

void
fpt_setup_virtual_device_environment (void)
{
  g_autoptr(GError) error = NULL;
  g_autofree char *temp_dir = NULL;
  g_autofree char *temp_path = NULL;

  g_assert_null (g_getenv ("FP_VIRTUAL_IMAGE"));

  temp_dir = g_dir_make_tmp ("libfprint-XXXXXX", &error);
  g_assert_no_error (error);

  temp_path = g_build_filename (temp_dir, "virtual-image.socket", NULL);
  g_setenv ("FP_VIRTUAL_IMAGE", temp_path, TRUE);

  signal (SIGKILL, on_signal_event);
  signal (SIGABRT, on_signal_event);
  signal (SIGSEGV, on_signal_event);
  signal (SIGTERM, on_signal_event);
  signal (SIGQUIT, on_signal_event);
  signal (SIGPIPE, on_signal_event);
}

FptContext *
fpt_context_new (void)
{
  FptContext *tctx;

  tctx = g_new0 (FptContext, 1);
  tctx->fp_context = fp_context_new ();

  return tctx;
}

FptContext *
fpt_context_new_with_virtual_imgdev (void)
{
  FptContext *tctx;
  GPtrArray *devices;
  unsigned int i;

  fpt_setup_virtual_device_environment ();

  tctx = fpt_context_new ();
  devices = fp_context_get_devices (tctx->fp_context);

  g_assert_nonnull (devices);
  g_assert_cmpuint (devices->len, ==, 1);

  for (i = 0; i < devices->len; ++i)
    {
      FpDevice *device = devices->pdata[i];

      if (g_strcmp0 (fp_device_get_driver (device), "virtual_image") == 0)
        {
          tctx->device = device;
          break;
        }
    }

  g_assert_true (FP_IS_DEVICE (tctx->device));
  g_object_add_weak_pointer (G_OBJECT (tctx->device), (gpointer) & tctx->device);

  return tctx;
}

void
fpt_context_free (FptContext *tctx)
{
  if (tctx->device && fp_device_is_open (tctx->device))
    {
      g_autoptr(GError) error = NULL;

      fp_device_close_sync (tctx->device, NULL, &error);
      g_assert_no_error (error);
    }

  g_clear_object (&tctx->fp_context);
  g_free (tctx);

  fpt_teardown_virtual_device_environment ();
}
