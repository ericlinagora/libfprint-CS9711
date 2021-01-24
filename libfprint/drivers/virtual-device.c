/*
 * Virtual driver for "simple" device debugging
 *
 * Copyright (C) 2019 Benjamin Berg <bberg@redhat.com>
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

#define FP_COMPONENT "virtual_device"

#include "virtual-device-private.h"
#include "fpi-log.h"

G_DEFINE_TYPE (FpDeviceVirtualDevice, fpi_device_virtual_device, FP_TYPE_DEVICE)

#define INSERT_CMD_PREFIX "INSERT "
#define REMOVE_CMD_PREFIX "REMOVE "
#define SCAN_CMD_PREFIX "SCAN "
#define ERROR_CMD_PREFIX "ERROR "
#define RETRY_CMD_PREFIX "RETRY "
#define FINGER_CMD_PREFIX "FINGER "
#define SET_ENROLL_STAGES_PREFIX "SET_ENROLL_STAGES "
#define SET_SCAN_TYPE_PREFIX "SET_SCAN_TYPE "

#define LIST_CMD "LIST"

static void
maybe_continue_current_action (FpDeviceVirtualDevice *self)
{
  FpDevice *dev = FP_DEVICE (self);

  switch (fpi_device_get_current_action (dev))
    {
    case FPI_DEVICE_ACTION_ENROLL:
      FP_DEVICE_GET_CLASS (self)->enroll (dev);
      break;

    case FPI_DEVICE_ACTION_VERIFY:
      FP_DEVICE_GET_CLASS (self)->verify (dev);
      break;

    case FPI_DEVICE_ACTION_IDENTIFY:
      FP_DEVICE_GET_CLASS (self)->identify (dev);
      break;

    default:
      break;
    }
}

char *
process_cmds (FpDeviceVirtualDevice * self,
              gboolean scan,
              GError * *error)
{
  while (self->pending_commands->len > 0)
    {
      gchar *cmd = g_ptr_array_index (self->pending_commands, 0);

      g_debug ("Processing command %s", cmd);

      /* These are always processed. */
      if (g_str_has_prefix (cmd, INSERT_CMD_PREFIX))
        {
          g_assert (self->prints_storage);
          g_hash_table_add (self->prints_storage,
                            g_strdup (cmd + strlen (INSERT_CMD_PREFIX)));

          g_ptr_array_remove_index (self->pending_commands, 0);
          continue;
        }
      else if (g_str_has_prefix (cmd, REMOVE_CMD_PREFIX))
        {
          g_assert (self->prints_storage);
          if (!g_hash_table_remove (self->prints_storage,
                                    cmd + strlen (REMOVE_CMD_PREFIX)))
            g_warning ("ID %s was not found in storage", cmd + strlen (REMOVE_CMD_PREFIX));

          g_ptr_array_remove_index (self->pending_commands, 0);
          continue;
        }

      /* If we are not scanning, then we have to stop here. */
      if (!scan)
        break;

      if (g_str_has_prefix (cmd, SCAN_CMD_PREFIX))
        {
          char *res = g_strdup (cmd + strlen (SCAN_CMD_PREFIX));

          g_ptr_array_remove_index (self->pending_commands, 0);
          return res;
        }
      else if (g_str_has_prefix (cmd, ERROR_CMD_PREFIX))
        {
          g_propagate_error (error,
                             fpi_device_error_new (g_ascii_strtoull (cmd + strlen (ERROR_CMD_PREFIX), NULL, 10)));

          g_ptr_array_remove_index (self->pending_commands, 0);
          return NULL;
        }
      else if (g_str_has_prefix (cmd, RETRY_CMD_PREFIX))
        {
          g_propagate_error (error,
                             fpi_device_retry_new (g_ascii_strtoull (cmd + strlen (RETRY_CMD_PREFIX), NULL, 10)));

          g_ptr_array_remove_index (self->pending_commands, 0);
          return NULL;
        }
      else if (g_str_has_prefix (cmd, FINGER_CMD_PREFIX))
        {
          gboolean finger_present;

          finger_present = g_ascii_strtoull (cmd + strlen (FINGER_CMD_PREFIX), NULL, 10) != 0;
          fpi_device_report_finger_status_changes (FP_DEVICE (self),
                                                   finger_present ? FP_FINGER_STATUS_PRESENT : FP_FINGER_STATUS_NONE,
                                                   finger_present ? FP_FINGER_STATUS_NONE : FP_FINGER_STATUS_PRESENT);

          g_ptr_array_remove_index (self->pending_commands, 0);
          continue;
        }
      else
        {
          g_warning ("Could not process command: %s", cmd);
          g_ptr_array_remove_index (self->pending_commands, 0);
        }
    }

  /* No commands left, throw a timeout error. */
  g_set_error_literal (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND, "No commands left that can be run!");
  return NULL;
}

static void
write_key_to_listener (void *key, void *val, void *user_data)
{
  FpDeviceVirtualListener *listener = FP_DEVICE_VIRTUAL_LISTENER (user_data);

  if (!fp_device_virtual_listener_write_sync (listener, key, strlen (key), NULL) ||
      !fp_device_virtual_listener_write_sync (listener, "\n", 1, NULL))
    g_warning ("Error writing reply to LIST command");
}

static void
recv_instruction_cb (GObject      *source_object,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  g_autoptr(GError) error = NULL;
  FpDeviceVirtualListener *listener = FP_DEVICE_VIRTUAL_LISTENER (source_object);
  gsize bytes;

  bytes = fp_device_virtual_listener_read_finish (listener, res, &error);
  fp_dbg ("Got instructions of length %ld", bytes);

  if (error)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      g_warning ("Error receiving instruction data: %s", error->message);
      return;
    }

  if (bytes > 0)
    {
      FpDeviceVirtualDevice *self;
      g_autofree char *cmd = NULL;

      self = FP_DEVICE_VIRTUAL_DEVICE (user_data);

      cmd = g_strndup (self->recv_buf, bytes);
      fp_dbg ("Received command %s", cmd);

      if (g_str_has_prefix (cmd, LIST_CMD))
        {
          if (self->prints_storage)
            g_hash_table_foreach (self->prints_storage, write_key_to_listener, listener);
        }
      else if (g_str_has_prefix (cmd, SET_ENROLL_STAGES_PREFIX))
        {
          guint stages;

          stages = g_ascii_strtoull (cmd + strlen (SET_ENROLL_STAGES_PREFIX), NULL, 10);
          fpi_device_set_nr_enroll_stages (FP_DEVICE (self), stages);
        }
      else if (g_str_has_prefix (cmd, SET_SCAN_TYPE_PREFIX))
        {
          const char *scan_type = cmd + strlen (SET_SCAN_TYPE_PREFIX);
          g_autoptr(GEnumClass) scan_types = g_type_class_ref (fp_scan_type_get_type ());
          GEnumValue *value = g_enum_get_value_by_nick (scan_types, scan_type);

          if (value)
            fpi_device_set_scan_type (FP_DEVICE (self), value->value);
          else
            g_warning ("Scan type '%s' not found", scan_type);
        }
      else
        {
          g_ptr_array_add (self->pending_commands, g_steal_pointer (&cmd));
          g_clear_handle_id (&self->wait_command_id, g_source_remove);

          maybe_continue_current_action (self);
        }
    }

  fp_device_virtual_listener_connection_close (listener);
}

static void
recv_instruction (FpDeviceVirtualDevice *self)
{
  fp_device_virtual_listener_read (self->listener,
                                   FALSE,
                                   self->recv_buf,
                                   sizeof (self->recv_buf),
                                   recv_instruction_cb,
                                   self);
}

static void
on_listener_connected (FpDeviceVirtualListener *listener,
                       gpointer                 user_data)
{
  FpDeviceVirtualDevice *self = FP_DEVICE_VIRTUAL_DEVICE (user_data);

  recv_instruction (self);
}

static void
dev_init (FpDevice *dev)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GCancellable) cancellable = NULL;
  g_autoptr(FpDeviceVirtualListener) listener = NULL;
  FpDeviceVirtualDevice *self = FP_DEVICE_VIRTUAL_DEVICE (dev);

  G_DEBUG_HERE ();

  listener = fp_device_virtual_listener_new ();
  cancellable = g_cancellable_new ();

  if (!fp_device_virtual_listener_start (listener,
                                         fpi_device_get_virtual_env (FP_DEVICE (self)),
                                         cancellable,
                                         on_listener_connected,
                                         self,
                                         &error))
    {
      fpi_device_open_complete (dev, g_steal_pointer (&error));
      return;
    }

  self->listener = g_steal_pointer (&listener);
  self->cancellable = g_steal_pointer (&cancellable);

  fpi_device_open_complete (dev, NULL);
}

static gboolean
wait_for_command_timeout (gpointer data)
{
  FpDeviceVirtualDevice *self = FP_DEVICE_VIRTUAL_DEVICE (data);
  GError *error = NULL;

  self->wait_command_id = 0;
  error = g_error_new (G_IO_ERROR, G_IO_ERROR_TIMED_OUT, "No commands arrived in time to run!");
  fpi_device_action_error (FP_DEVICE (self), error);

  return FALSE;
}

gboolean
should_wait_for_command (FpDeviceVirtualDevice *self,
                         GError                *error)
{
  if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_NOT_FOUND))
    return FALSE;

  if (self->wait_command_id)
    return FALSE;

  self->wait_command_id = g_timeout_add (500, wait_for_command_timeout, self);
  return TRUE;
}

static void
dev_verify (FpDevice *dev)
{
  g_autoptr(GError) error = NULL;
  FpDeviceVirtualDevice *self = FP_DEVICE_VIRTUAL_DEVICE (dev);
  FpPrint *print;
  g_autofree char *scan_id = NULL;

  fpi_device_get_verify_data (dev, &print);
  fpi_device_report_finger_status (dev, FP_FINGER_STATUS_NEEDED);

  scan_id = process_cmds (self, TRUE, &error);
  if (should_wait_for_command (self, error))
    return;

  if (scan_id)
    {
      GVariant *data = NULL;
      FpPrint *new_scan;
      gboolean success;

      g_debug ("Virtual device scanned print %s", scan_id);

      new_scan = fp_print_new (dev);
      fpi_print_set_type (new_scan, FPI_PRINT_RAW);
      if (self->prints_storage)
        fpi_print_set_device_stored (new_scan, TRUE);
      data = g_variant_new_string (scan_id);
      g_object_set (new_scan, "fpi-data", data, NULL);

      success = fp_print_equal (print, new_scan);

      fpi_device_verify_report (dev,
                                success ? FPI_MATCH_SUCCESS : FPI_MATCH_FAIL,
                                new_scan,
                                NULL);
    }
  else
    {
      g_debug ("Virtual device scan failed with error: %s", error->message);
    }

  if (error && error->domain == FP_DEVICE_RETRY)
    fpi_device_verify_report (dev, FPI_MATCH_ERROR, NULL, g_steal_pointer (&error));

  fpi_device_verify_complete (dev, g_steal_pointer (&error));
}

static void
dev_enroll (FpDevice *dev)
{
  g_autoptr(GError) error = NULL;
  FpDeviceVirtualDevice *self = FP_DEVICE_VIRTUAL_DEVICE (dev);
  FpPrint *print = NULL;
  g_autofree char *id = NULL;

  fpi_device_report_finger_status (dev, FP_FINGER_STATUS_NEEDED);
  fpi_device_get_enroll_data (dev, &print);

  id = process_cmds (self, TRUE, &error);
  if (should_wait_for_command (self, error))
    return;

  if (id)
    {
      GVariant *data;

      if (self->enroll_stages_passed == 0)
        {
          fpi_print_set_type (print, FPI_PRINT_RAW);
          data = g_variant_new_string (id);
          g_object_set (print, "fpi-data", data, NULL);
        }
      else
        {
          gboolean changed;

          g_object_get (print, "fpi-data", &data, NULL);
          changed = !g_str_equal (id, g_variant_get_string (data, NULL));
          g_variant_unref (data);

          if (changed)
            {
              g_set_error (&error, FP_DEVICE_RETRY, FP_DEVICE_RETRY_GENERAL, "ID Mismatch");
              fpi_device_enroll_progress (dev, self->enroll_stages_passed, NULL, error);
              return;
            }
        }

      self->enroll_stages_passed++;
      fpi_device_enroll_progress (dev, self->enroll_stages_passed, print, NULL);
      if (self->enroll_stages_passed == fp_device_get_nr_enroll_stages (FP_DEVICE (self)))
        {
          if (self->prints_storage)
            {
              fpi_print_set_device_stored (print, TRUE);
              g_hash_table_add (self->prints_storage, g_strdup (id));
            }

          fpi_device_enroll_complete (dev, g_object_ref (print), NULL);
          self->enroll_stages_passed = 0;
        }
    }
  else
    {
      if (error && error->domain == FP_DEVICE_RETRY)
        {
          fpi_device_enroll_progress (dev, self->enroll_stages_passed, NULL, g_steal_pointer (&error));
        }
      else
        {
          self->enroll_stages_passed = 0;
          fpi_device_enroll_complete (dev, NULL, g_steal_pointer (&error));
        }
    }
}

static void
dev_deinit (FpDevice *dev)
{
  FpDeviceVirtualDevice *self = FP_DEVICE_VIRTUAL_DEVICE (dev);

  g_clear_handle_id (&self->wait_command_id, g_source_remove);
  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  g_clear_object (&self->listener);
  g_clear_object (&self->listener);

  fpi_device_close_complete (dev, NULL);
}

static void
fpi_device_virtual_device_finalize (GObject *object)
{
  FpDeviceVirtualDevice *self = FP_DEVICE_VIRTUAL_DEVICE (object);

  G_DEBUG_HERE ();
  g_clear_pointer (&self->pending_commands, g_ptr_array_unref);
  G_OBJECT_CLASS (fpi_device_virtual_device_parent_class)->finalize (object);
}

static void
fpi_device_virtual_device_init (FpDeviceVirtualDevice *self)
{
  self->pending_commands = g_ptr_array_new_with_free_func (g_free);
}

static const FpIdEntry driver_ids[] = {
  { .virtual_envvar = "FP_VIRTUAL_DEVICE", },
  { .virtual_envvar = NULL }
};

static void
fpi_device_virtual_device_class_init (FpDeviceVirtualDeviceClass *klass)
{
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->finalize = fpi_device_virtual_device_finalize;

  dev_class->id = FP_COMPONENT;
  dev_class->full_name = "Virtual device for debugging";
  dev_class->type = FP_DEVICE_TYPE_VIRTUAL;
  dev_class->id_table = driver_ids;
  dev_class->nr_enroll_stages = 5;

  dev_class->open = dev_init;
  dev_class->close = dev_deinit;
  dev_class->verify = dev_verify;
  dev_class->enroll = dev_enroll;
}
