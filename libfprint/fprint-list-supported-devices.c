/*
 * Copyright (C) 2009 Red Hat <mjg@redhat.com>
 * Copyright (C) 2008 Bastien Nocera <hadess@hadess.net>
 * Copyright (C) 2008 Timo Hoenig <thoenig@suse.de>, <thoenig@nouse.net>
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

#include <config.h>
#include <stdio.h>
#include <locale.h>

#include "fpi-context.h"
#include "fpi-device.h"

GHashTable *printed = NULL;

static void
insert_drivers (GList **usb_list, GList **spi_list)
{
  g_autoptr(GArray) drivers = fpi_get_driver_types ();
  gint i;

  /* Find the best driver to handle this USB device. */
  for (i = 0; i < drivers->len; i++)
    {
      GType driver = g_array_index (drivers, GType, i);
      g_autoptr(FpDeviceClass) cls = g_type_class_ref (driver);
      const FpIdEntry *entry;

      switch (cls->type)
        {
        case FP_DEVICE_TYPE_USB:

          for (entry = cls->id_table; entry->vid; entry++)
            {
              char *key;

              key = g_strdup_printf ("%04x:%04x", entry->vid, entry->pid);

              if (g_hash_table_lookup (printed, key) != NULL)
                {
                  g_free (key);
                  continue;
                }

              g_hash_table_insert (printed, key, GINT_TO_POINTER (1));

              *usb_list = g_list_prepend (*usb_list,
                                          g_strdup_printf ("%s | %s\n", key, cls->full_name));
            }
          break;

        case FP_DEVICE_TYPE_UDEV:
          for (entry = cls->id_table; entry->udev_types; entry++)
            {
              char *key;

              /* Need SPI device */
              if ((entry->udev_types & FPI_DEVICE_UDEV_SUBTYPE_SPIDEV) == 0)
                continue;

              key = g_strdup_printf ("SPI:%s:%04x:%04x", entry->spi_acpi_id, entry->hid_id.vid, entry->hid_id.pid);

              if (g_hash_table_lookup (printed, key) != NULL)
                {
                  g_free (key);
                  continue;
                }

              g_hash_table_insert (printed, key, GINT_TO_POINTER (1));

              if (entry->udev_types & FPI_DEVICE_UDEV_SUBTYPE_HIDRAW)
                *spi_list = g_list_prepend (*spi_list,
                                            g_strdup_printf ("%s | %04x:%04x | %s\n", entry->spi_acpi_id, entry->hid_id.vid, entry->hid_id.pid, cls->full_name));
              else
                *spi_list = g_list_prepend (*spi_list,
                                            g_strdup_printf ("%s | - | %s\n", entry->spi_acpi_id, cls->full_name));
            }
          break;

        case FP_DEVICE_TYPE_VIRTUAL:
        default:
          break;
        }
    }
}

int
main (int argc, char **argv)
{
  GList *usb_list = NULL;
  GList *spi_list = NULL;
  GList *l;

  setlocale (LC_ALL, "");

  printed = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

  g_print ("%% lifprint — Supported Devices\n");
  g_print ("%% Bastien Nocera, Daniel Drake\n");
  g_print ("%% 2018\n");
  g_print ("\n");

  g_print ("# Supported Devices\n");
  g_print ("\n");
  g_print ("This is a list of supported devices in libfprint's development version. Those drivers might not all be available in the stable, released version. If in doubt, contact your distribution or systems integrator for details.\n");
  g_print ("\n");

  insert_drivers (&usb_list, &spi_list);

  g_print ("## USB devices\n");
  g_print ("\n");
  g_print ("USB ID | Driver\n");
  g_print ("------------ | ------------\n");

  usb_list = g_list_sort (usb_list, (GCompareFunc) g_strcmp0);
  for (l = usb_list; l != NULL; l = l->next)
    g_print ("%s", (char *) l->data);
  g_print ("\n");

  g_list_free_full (g_steal_pointer (&usb_list), g_free);

  g_print ("## SPI devices\n");
  g_print ("\n");
  g_print ("The ACPI ID represents the SPI interface. Some sensors are also connected to a HID device (Human Input Device) for side-channel requests such as resets.\n");
  g_print ("\n");
  g_print ("ACPI ID | HID ID | Driver\n");
  g_print ("------------ | ------------ | ------------\n");

  spi_list = g_list_sort (spi_list, (GCompareFunc) g_strcmp0);
  for (l = spi_list; l != NULL; l = l->next)
    g_print ("%s", (char *) l->data);
  g_print ("\n");

  g_list_free_full (g_steal_pointer (&spi_list), g_free);


  g_hash_table_destroy (printed);

  return 0;
}
