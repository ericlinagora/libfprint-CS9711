/*
 * Virtual driver for "simple" device debugging with storage
 *
 * Copyright (C) 2020 Bastien Nocera <hadess@hadess.net>
 * Copyright (C) 2020 Marco Trevisan <marco.trevisan@canonical.com>
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

/*
 * This is a virtual driver to debug the non-image based drivers. A small
 * python script is provided to connect to it via a socket, allowing
 * prints to registered programmatically.
 * Using this, it is possible to test libfprint and fprintd.
 */

#define FP_COMPONENT "virtual_device_storage"

#include "virtual-device-private.h"
#include "fpi-log.h"

G_DEFINE_TYPE (FpDeviceVirtualDeviceStorage, fpi_device_virtual_device_storage, fpi_device_virtual_device_get_type ())

static void
dev_identify (FpDevice *dev)
{
  g_autoptr(GError) error = NULL;
  FpDeviceVirtualDevice *self = FP_DEVICE_VIRTUAL_DEVICE (dev);
  GPtrArray *prints;
  g_autofree char *scan_id = NULL;

  fpi_device_get_identify_data (dev, &prints);

  scan_id = process_cmds (self, TRUE, &error);
  if (should_wait_for_command (self, error))
    return;

  if (scan_id)
    {
      GVariant *data = NULL;
      FpPrint *new_scan;
      FpPrint *match = NULL;
      guint idx;

      new_scan = fp_print_new (dev);
      fpi_print_set_type (new_scan, FPI_PRINT_RAW);
      if (self->prints_storage)
        fpi_print_set_device_stored (new_scan, TRUE);
      data = g_variant_new_string (scan_id);
      g_object_set (new_scan, "fpi-data", data, NULL);

      if (g_ptr_array_find_with_equal_func (prints,
                                            new_scan,
                                            (GEqualFunc) fp_print_equal,
                                            &idx))
        match = g_ptr_array_index (prints, idx);

      fpi_device_identify_report (dev,
                                  match,
                                  new_scan,
                                  NULL);
    }

  fpi_device_identify_complete (dev, g_steal_pointer (&error));
}

struct ListData
{
  FpDevice  *dev;
  GPtrArray *res;
};

static void
dev_list_insert_print (gpointer key,
                       gpointer value,
                       gpointer user_data)
{
  struct ListData *data = user_data;
  FpPrint *print = fp_print_new (data->dev);
  GVariant *var = NULL;

  fpi_print_fill_from_user_id (print, key);
  fpi_print_set_type (print, FPI_PRINT_RAW);
  var = g_variant_new_string (key);
  g_object_set (print, "fpi-data", var, NULL);
  g_object_ref_sink (print);

  g_ptr_array_add (data->res, print);
}

static void
dev_list (FpDevice *dev)
{
  g_autoptr(GPtrArray) prints_list = NULL;
  FpDeviceVirtualDevice *vdev = FP_DEVICE_VIRTUAL_DEVICE (dev);
  struct ListData data;

  process_cmds (vdev, FALSE, NULL);

  prints_list = g_ptr_array_new_full (g_hash_table_size (vdev->prints_storage), NULL);
  data.dev = dev;
  data.res = prints_list;

  g_hash_table_foreach (vdev->prints_storage, dev_list_insert_print, &data);

  fpi_device_list_complete (dev, g_steal_pointer (&prints_list), NULL);
}

static void
dev_delete (FpDevice *dev)
{
  g_autoptr(GVariant) data = NULL;
  FpDeviceVirtualDevice *vdev = FP_DEVICE_VIRTUAL_DEVICE (dev);
  FpPrint *print = NULL;
  const char *id = NULL;

  process_cmds (vdev, FALSE, NULL);

  fpi_device_get_delete_data (dev, &print);

  g_object_get (print, "fpi-data", &data, NULL);
  if (data == NULL)
    {
      fpi_device_delete_complete (dev,
                                  fpi_device_error_new (FP_DEVICE_ERROR_DATA_INVALID));
      return;
    }

  id = g_variant_get_string (data, NULL);

  fp_dbg ("Deleting print %s for user %s",
          id,
          fp_print_get_username (print));

  if (g_hash_table_remove (vdev->prints_storage, id))
    fpi_device_delete_complete (dev, NULL);
  else
    fpi_device_delete_complete (dev,
                                fpi_device_error_new (FP_DEVICE_ERROR_DATA_NOT_FOUND));
}

static void
fpi_device_virtual_device_storage_init (FpDeviceVirtualDeviceStorage *self)
{
  FpDeviceVirtualDevice *vdev = FP_DEVICE_VIRTUAL_DEVICE (self);

  vdev->prints_storage = g_hash_table_new_full (g_str_hash,
                                                g_str_equal,
                                                g_free,
                                                NULL);
}

static const FpIdEntry driver_ids[] = {
  { .virtual_envvar = "FP_VIRTUAL_DEVICE_STORAGE" },
  { .virtual_envvar = "FP_VIRTUAL_DEVICE_IDENT" },
  { .virtual_envvar = NULL }
};

static void
fpi_device_virtual_device_storage_class_init (FpDeviceVirtualDeviceStorageClass *klass)
{
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);

  dev_class->id = FP_COMPONENT;
  dev_class->full_name = "Virtual device with storage and identification for debugging";
  dev_class->id_table = driver_ids;

  dev_class->identify = dev_identify;
  dev_class->list = dev_list;
  dev_class->delete = dev_delete;
}
