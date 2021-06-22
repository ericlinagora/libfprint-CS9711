/*
 * Copyright (C) 2009 Red Hat <mjg@redhat.com>
 * Copyright (C) 2008 Bastien Nocera <hadess@hadess.net>
 * Copyright (C) 2008 Timo Hoenig <thoenig@suse.de>, <thoenig@nouse.net>
 * Coypright (C) 2019 Benjamin Berg <bberg@redhat.com>
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

#include <config.h>

#include "fpi-context.h"
#include "fpi-device.h"

GHashTable *printed = NULL;

static void
print_driver (const FpDeviceClass *cls)
{
  const FpIdEntry *entry;
  gint num_printed = 0;

  if (cls->type != FP_DEVICE_TYPE_UDEV)
    return;

  for (entry = cls->id_table; entry->udev_types != 0; entry++)
    {
      /* We only add rules for spidev right now. */
      if ((entry->udev_types & FPI_DEVICE_UDEV_SUBTYPE_SPIDEV) == 0)
        continue;

      if (g_hash_table_lookup (printed, entry->spi_acpi_id) != NULL)
        continue;

      g_hash_table_insert (printed, g_strdup (entry->spi_acpi_id), GINT_TO_POINTER (1));

      if (num_printed == 0)
        g_print ("# %s\n", cls->full_name);

      g_print ("ACTION==\"add|change\", SUBSYSTEM==\"spi\", ENV{MODALIAS}==\"acpi:%s:\", RUN{builtin}+=\"kmod load spi:spidev\", RUN+=\"/bin/sh -c 'echo spidev > %%S%%p/driver_override && echo %%k > %%S%%p/subsystem/drivers/spidev/bind'\"\n",
               entry->spi_acpi_id);
      num_printed++;
    }

  if (num_printed > 0)
    g_print ("\n");
}

int
main (int argc, char **argv)
{
  g_autoptr(GArray) drivers = fpi_get_driver_types ();
  guint i;

  printed = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  g_print ("# This file is part of libfprint\n");
  g_print ("# Do not modify this file, it will get overwritten on updates.\n");
  g_print ("# To override or extend the rules place a file in /etc/udev/rules.d\n");
  g_print ("\n");

  for (i = 0; i < drivers->len; i++)
    {
      GType driver = g_array_index (drivers, GType, i);
      FpDeviceClass *cls = FP_DEVICE_CLASS (g_type_class_ref (driver));

      if (cls->type != FP_DEVICE_TYPE_UDEV)
        {
          g_type_class_unref (cls);
          continue;
        }

      print_driver (cls);

      g_type_class_unref (cls);
    }

  g_hash_table_destroy (printed);

  return 0;
}
