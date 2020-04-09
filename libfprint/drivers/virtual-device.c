/*
 * Virtual driver for "simple" device debugging
 *
 * Copyright (C) 2019 Benjamin Berg <bberg@redhat.com>
 * Copyright (C) 2020 Bastien Nocera <hadess@hadess.net>
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

#include "fpi-log.h"

#include "../fpi-device.h"

#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>

#define MAX_LINE_LEN 1024

enum {
  VIRTUAL_DEVICE,
  VIRTUAL_DEVICE_IDENT
};

struct _FpDeviceVirtualDevice
{
  FpDevice           parent;

  GSocketListener   *listener;
  GSocketConnection *connection;
  GCancellable      *cancellable;

  gint               socket_fd;
  gint               client_fd;
  guint              line[MAX_LINE_LEN];

  GHashTable        *pending_prints; /* key: finger+username value: gboolean */
};

G_DECLARE_FINAL_TYPE (FpDeviceVirtualDevice, fpi_device_virtual_device, FP, DEVICE_VIRTUAL_DEVICE, FpDevice)
G_DEFINE_TYPE (FpDeviceVirtualDevice, fpi_device_virtual_device, FP_TYPE_DEVICE)

static void start_listen (FpDeviceVirtualDevice *self);

#define ADD_CMD_PREFIX "ADD "

static FpFinger
str_to_finger (const char *str)
{
  g_autoptr(GEnumClass) eclass;
  GEnumValue *value;

  eclass = g_type_class_ref (FP_TYPE_FINGER);
  value = g_enum_get_value_by_nick (eclass, str);

  if (value == NULL)
    return FP_FINGER_UNKNOWN;

  return value->value;
}

static const char *
finger_to_str (FpFinger finger)
{
  GEnumClass *eclass;
  GEnumValue *value;

  eclass = g_type_class_ref (FP_TYPE_FINGER);
  value = g_enum_get_value (eclass, finger);
  g_type_class_unref (eclass);

  if (value == NULL)
    return NULL;

  return value->value_nick;
}

static gboolean
parse_code (const char *str)
{
  if (g_strcmp0 (str, "1") == 0 ||
      g_strcmp0 (str, "success") == 0 ||
      g_strcmp0 (str, "SUCCESS") == 0 ||
      g_strcmp0 (str, "FPI_MATCH_SUCCESS") == 0)
    return FPI_MATCH_SUCCESS;

  return FPI_MATCH_FAIL;
}

static void
handle_command_line (FpDeviceVirtualDevice *self,
                     const char            *line)
{
  if (g_str_has_prefix (line, ADD_CMD_PREFIX))
    {
      g_auto(GStrv) elems;
      FpPrint *print;
      FpFinger finger;
      gboolean success;
      g_autofree char *description = NULL;
      char *key;

      /* Syntax: ADD <finger> <username> <error when used> */
      elems = g_strsplit (line + strlen (ADD_CMD_PREFIX), " ", 3);
      if (g_strv_length (elems) != 3)
        {
          g_warning ("Malformed command: %s", line);
          return;
        }
      finger = str_to_finger (elems[0]);
      if (finger == FP_FINGER_UNKNOWN)
        {
          g_warning ("Unknown finger '%s'", elems[0]);
          return;
        }
      print = fp_print_new (FP_DEVICE (self));
      fp_print_set_finger (print, finger);
      fp_print_set_username (print, elems[1]);
      description = g_strdup_printf ("Fingerprint finger '%s' for user '%s'",
                                     elems[0], elems[1]);
      fp_print_set_description (print, description);
      success = parse_code (elems[2]);

      key = g_strdup_printf ("%s-%s", elems[0], elems[1]);
      g_hash_table_insert (self->pending_prints,
                           key, GINT_TO_POINTER (success));

      fp_dbg ("Added pending print %s for user %s (code: %s)",
              elems[0], elems[1], success ? "FPI_MATCH_SUCCESS" : "FPI_MATCH_FAIL");
    }
  else
    {
      g_warning ("Unhandled command sent: '%s'", line);
    }
}

static void
recv_instruction_cb (GObject      *source_object,
                     GAsyncResult *res,
                     gpointer      user_data)
{
  g_autoptr(GError) error = NULL;
  FpDeviceVirtualDevice *self;
  gboolean success;
  gsize bytes;

  success = g_input_stream_read_all_finish (G_INPUT_STREAM (source_object), res, &bytes, &error);

  if (!success || bytes == 0)
    {
      if (!success)
        {
          if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED) ||
              g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CLOSED))
            return;
          g_warning ("Error receiving instruction data: %s", error->message);
        }

      self = FP_DEVICE_VIRTUAL_DEVICE (user_data);
      goto out;
    }

  self = FP_DEVICE_VIRTUAL_DEVICE (user_data);
  handle_command_line (self, (const char *) self->line);

out:
  g_io_stream_close (G_IO_STREAM (self->connection), NULL, NULL);
  g_clear_object (&self->connection);

  start_listen (self);
}

static void
recv_instruction (FpDeviceVirtualDevice *self,
                  GInputStream          *stream)
{
  memset (&self->line, 0, sizeof (self->line));
  g_input_stream_read_all_async (stream,
                                 self->line,
                                 sizeof (self->line),
                                 G_PRIORITY_DEFAULT,
                                 self->cancellable,
                                 recv_instruction_cb,
                                 self);
}

static void
new_connection_cb (GObject      *source_object,
                   GAsyncResult *res,
                   gpointer      user_data)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GSocketConnection) connection = NULL;
  GInputStream *stream;
  FpDeviceVirtualDevice *self = user_data;

  connection = g_socket_listener_accept_finish (G_SOCKET_LISTENER (source_object),
                                                res,
                                                NULL,
                                                &error);
  if (!connection)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      g_warning ("Error accepting a new connection: %s", error->message);
      start_listen (self);
      return;
    }

  /* Always further connections (but we disconnect them immediately
   * if we already have a connection). */
  if (self->connection)
    {
      g_io_stream_close (G_IO_STREAM (connection), NULL, NULL);
      start_listen (self);
      return;
    }

  self->connection = g_steal_pointer (&connection);
  stream = g_io_stream_get_input_stream (G_IO_STREAM (connection));

  recv_instruction (self, stream);

  fp_dbg ("Got a new connection!");
}

static void
start_listen (FpDeviceVirtualDevice *self)
{
  fp_dbg ("Starting a new listener");
  g_socket_listener_accept_async (self->listener,
                                  self->cancellable,
                                  new_connection_cb,
                                  self);
}

static void
dev_init (FpDevice *dev)
{
  fpi_device_open_complete (dev, NULL);
}

static void
dev_verify (FpDevice *dev)
{
  FpPrint *print;

  g_autoptr(GVariant) data = NULL;
  gboolean success;

  fpi_device_get_verify_data (dev, &print);
  g_object_get (print, "fpi-data", &data, NULL);
  success = g_variant_get_boolean (data);

  fpi_device_verify_report (dev,
                            success ? FPI_MATCH_SUCCESS : FPI_MATCH_FAIL,
                            NULL, NULL);
  fpi_device_verify_complete (dev, NULL);
}

static void
dev_enroll (FpDevice *dev)
{
  FpDeviceVirtualDevice *self = FP_DEVICE_VIRTUAL_DEVICE (dev);
  gpointer success_ptr;
  FpPrint *print = NULL;
  g_autofree char *key = NULL;

  fpi_device_get_enroll_data (dev, &print);
  key = g_strdup_printf ("%s-%s",
                         finger_to_str (fp_print_get_finger (print)),
                         fp_print_get_username (print));

  if (g_hash_table_lookup_extended (self->pending_prints, key, NULL, &success_ptr))
    {
      gboolean success = GPOINTER_TO_INT (success_ptr);
      GVariant *fp_data;

      fp_data = g_variant_new_boolean (success);
      fpi_print_set_type (print, FPI_PRINT_RAW);
      if (fpi_device_get_driver_data (dev) == VIRTUAL_DEVICE_IDENT)
        fpi_print_set_device_stored (print, TRUE);
      g_object_set (print, "fpi-data", fp_data, NULL);
      fpi_device_enroll_complete (dev, g_object_ref (print), NULL);
    }
  else
    {
      fpi_device_enroll_complete (dev, NULL,
                                  fpi_device_error_new_msg (FP_DEVICE_ERROR_PROTO,
                                                            "No pending result for this username/finger combination"));
    }
}

static void
dev_deinit (FpDevice *dev)
{
  fpi_device_close_complete (dev, NULL);
}

static gboolean
dev_supports_identify (FpDevice *dev)
{
  return fpi_device_get_driver_data (dev) == VIRTUAL_DEVICE_IDENT;
}

static void
dev_identify (FpDevice *dev)
{
  GPtrArray *templates;
  FpPrint *result = NULL;
  guint i;

  g_assert (fpi_device_get_driver_data (dev) == VIRTUAL_DEVICE_IDENT);

  fpi_device_get_identify_data (dev, &templates);

  for (i = 0; i < templates->len; i++)
    {
      FpPrint *template = g_ptr_array_index (templates, i);
      g_autoptr(GVariant) data = NULL;
      gboolean success;

      g_object_get (dev, "fpi-data", &template, NULL);
      success = g_variant_get_boolean (data);
      if (success)
        {
          result = template;
          break;
        }
    }

  if (result)
    fpi_device_identify_report (dev, result, NULL, NULL);
  fpi_device_identify_complete (dev, NULL);
}

static void
fpi_device_virtual_device_finalize (GObject *object)
{
  FpDeviceVirtualDevice *self = FP_DEVICE_VIRTUAL_DEVICE (object);

  G_DEBUG_HERE ();

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  g_clear_object (&self->listener);
  g_clear_object (&self->connection);
  g_hash_table_destroy (self->pending_prints);
}

static void
fpi_device_virtual_device_constructed (GObject *object)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GSocketListener) listener = NULL;
  FpDeviceVirtualDevice *self = FP_DEVICE_VIRTUAL_DEVICE (object);
  const char *env;
  g_autoptr(GSocketAddress) addr = NULL;
  G_DEBUG_HERE ();

  self->client_fd = -1;

  env = fpi_device_get_virtual_env (FP_DEVICE (self));

  listener = g_socket_listener_new ();
  g_socket_listener_set_backlog (listener, 1);

  /* Remove any left over socket. */
  g_unlink (env);

  addr = g_unix_socket_address_new (env);

  if (!g_socket_listener_add_address (listener,
                                      addr,
                                      G_SOCKET_TYPE_STREAM,
                                      G_SOCKET_PROTOCOL_DEFAULT,
                                      NULL,
                                      NULL,
                                      &error))
    {
      g_warning ("Could not listen on unix socket: %s", error->message);

      fpi_device_open_complete (FP_DEVICE (self), g_steal_pointer (&error));

      return;
    }

  self->listener = g_steal_pointer (&listener);
  self->cancellable = g_cancellable_new ();
  self->pending_prints = g_hash_table_new_full (g_str_hash,
                                                g_str_equal,
                                                g_free,
                                                NULL);

  if (G_OBJECT_CLASS (fpi_device_virtual_device_parent_class)->constructed)
    G_OBJECT_CLASS (fpi_device_virtual_device_parent_class)->constructed (object);

  start_listen (self);
}

static void
fpi_device_virtual_device_init (FpDeviceVirtualDevice *self)
{
}

static const FpIdEntry driver_ids[] = {
  { .virtual_envvar = "FP_VIRTUAL_DEVICE", .driver_data = VIRTUAL_DEVICE },
  { .virtual_envvar = "FP_VIRTUAL_DEVICE_IDENT", .driver_data = VIRTUAL_DEVICE_IDENT },
  { .virtual_envvar = NULL }
};

static void
fpi_device_virtual_device_class_init (FpDeviceVirtualDeviceClass *klass)
{
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->constructed = fpi_device_virtual_device_constructed;
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

  dev_class->identify = dev_identify;
  dev_class->supports_identify = dev_supports_identify;
}
