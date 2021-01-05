/*
 * Socket utilities for "simple" device debugging
 *
 * Copyright (C) 2019 Benjamin Berg <bberg@redhat.com>
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

#define FP_COMPONENT "virtual_device_connection"

#include "fpi-log.h"

#include <glib/gstdio.h>
#include <gio/gunixsocketaddress.h>

#include "virtual-device-private.h"

struct _FpDeviceVirtualListener
{
  GSocketListener                     parent_instance;

  GSocketConnection                  *connection;
  GCancellable                       *cancellable;
  guint                               cancellable_id;

  FpDeviceVirtualListenerConnectionCb ready_cb;
  gpointer                            ready_cb_data;

  gint                                socket_fd;
  gint                                client_fd;
};

G_DEFINE_TYPE (FpDeviceVirtualListener, fp_device_virtual_listener, G_TYPE_SOCKET_LISTENER)

static void start_listen (FpDeviceVirtualListener *self);

FpDeviceVirtualListener *
fp_device_virtual_listener_new (void)
{
  return g_object_new (fp_device_virtual_listener_get_type (), NULL);
}

static void
fp_device_virtual_listener_dispose (GObject *object)
{
  FpDeviceVirtualListener *self = FP_DEVICE_VIRTUAL_LISTENER (object);

  if (self->cancellable_id)
    {
      g_cancellable_disconnect (self->cancellable, self->cancellable_id);
      self->cancellable_id = 0;
    }

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  g_clear_object (&self->connection);

  self->ready_cb = NULL;

  G_OBJECT_CLASS (fp_device_virtual_listener_parent_class)->dispose (object);
}

static void
fp_device_virtual_listener_class_init (FpDeviceVirtualListenerClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);

  object_class->dispose = fp_device_virtual_listener_dispose;
}

static void
fp_device_virtual_listener_init (FpDeviceVirtualListener *self)
{
}

static void
new_connection_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  g_autoptr(GError) error = NULL;
  FpDeviceVirtualListener *self = user_data;
  GSocketConnection *connection;

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

  /* Always allow further connections.
   * If we get a new one, we generally just close the old connection. */
  start_listen (self);
  if (self->connection)
    {
      g_io_stream_close (G_IO_STREAM (self->connection), NULL, NULL);
      g_clear_object (&self->connection);
    }

  self->connection = connection;
  fp_dbg ("Got a new connection!");

  self->ready_cb (self, self->ready_cb_data);
}

static void
start_listen (FpDeviceVirtualListener *self)
{
  g_socket_listener_accept_async (G_SOCKET_LISTENER (self),
                                  self->cancellable,
                                  new_connection_cb,
                                  self);
}

static void
on_cancelled (GCancellable            *cancellable,
              FpDeviceVirtualListener *self)
{
  fp_device_virtual_listener_connection_close (self);
  g_socket_listener_close (G_SOCKET_LISTENER (self));
  g_clear_object (&self->cancellable);
  self->ready_cb = NULL;
}

gboolean
fp_device_virtual_listener_start (FpDeviceVirtualListener            *self,
                                  const char                         *address,
                                  GCancellable                       *cancellable,
                                  FpDeviceVirtualListenerConnectionCb cb,
                                  gpointer                            user_data,
                                  GError                            **error)
{
  g_autoptr(GSocketAddress) addr = NULL;
  G_DEBUG_HERE ();

  g_return_val_if_fail (FP_IS_DEVICE_VIRTUAL_LISTENER (self), FALSE);
  g_return_val_if_fail (cb != NULL, FALSE);
  g_return_val_if_fail (self->ready_cb == NULL, FALSE);

  self->client_fd = -1;

  g_socket_listener_set_backlog (G_SOCKET_LISTENER (self), 1);

  /* Remove any left over socket. */
  g_unlink (address);

  addr = g_unix_socket_address_new (address);

  if (!g_socket_listener_add_address (G_SOCKET_LISTENER (self),
                                      addr,
                                      G_SOCKET_TYPE_STREAM,
                                      G_SOCKET_PROTOCOL_DEFAULT,
                                      NULL,
                                      NULL,
                                      error))
    {
      g_warning ("Could not listen on unix socket: %s", (*error)->message);
      return FALSE;
    }

  self->ready_cb = cb;
  self->ready_cb_data = user_data;
  self->cancellable = cancellable ? g_object_ref (cancellable) : NULL;

  if (self->cancellable)
    self->cancellable_id = g_cancellable_connect (self->cancellable,
                                                  G_CALLBACK (on_cancelled), self, NULL);

  start_listen (self);

  return TRUE;
}

gboolean
fp_device_virtual_listener_connection_close (FpDeviceVirtualListener *self)
{
  g_return_val_if_fail (FP_IS_DEVICE_VIRTUAL_LISTENER (self), FALSE);

  if (!self->connection)
    return FALSE;

  g_io_stream_close (G_IO_STREAM (self->connection), NULL, NULL);
  g_clear_object (&self->connection);

  return TRUE;
}

static void
on_stream_read_cb (GObject      *source_object,
                   GAsyncResult *res,
                   gpointer      user_data)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GTask) task = user_data;
  FpDeviceVirtualListener *self = g_task_get_source_object (task);
  gboolean all;
  gboolean success;
  gsize bytes;

  all = GPOINTER_TO_INT (g_object_get_data (G_OBJECT (task), "all"));

  if (all)
    {
      success = g_input_stream_read_all_finish (G_INPUT_STREAM (source_object), res, &bytes, &error);
    }
  else
    {
      gssize sbytes;

      sbytes = g_input_stream_read_finish (G_INPUT_STREAM (source_object), res, &error);
      bytes = sbytes;
      success = (sbytes >= 0);
    }

  if (g_task_return_error_if_cancelled (task))
    return;

  /* If we are cancelled, just return immediately. */
  if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CLOSED))
    {
      g_task_return_int (task, 0);
      return;
    }

  /* If this error is for an old connection (that should be closed already),
   * then just give up immediately with a CLOSED error.
   */
  if (self->connection &&
      g_io_stream_get_input_stream (G_IO_STREAM (self->connection)) != G_INPUT_STREAM (source_object))
    {
      g_task_return_new_error (task,
                               G_IO_ERROR,
                               G_IO_ERROR_CLOSED,
                               "Error on old connection, ignoring.");
      return;
    }

  if (!success || bytes == 0)
    {
      /* We accept it if someone tries to read twice and just return that error. */
      if (!g_error_matches (error, G_IO_ERROR, G_IO_ERROR_PENDING))
        {
          if (self->connection)
            {
              g_io_stream_close (G_IO_STREAM (self->connection), NULL, NULL);
              g_clear_object (&self->connection);
            }
        }

      if (error)
        {
          g_task_return_error (task, g_steal_pointer (&error));
          return;
        }
      else
        {
          // g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_INVALID_DATA, "Got empty data");
          return;
        }
    }

  g_task_return_int (task, bytes);
}

void
fp_device_virtual_listener_read (FpDeviceVirtualListener *self,
                                 gboolean                 all,
                                 void                    *buffer,
                                 gsize                    count,
                                 GAsyncReadyCallback      callback,
                                 gpointer                 user_data)
{
  g_autoptr(GTask) task = NULL;
  GInputStream *stream;

  g_return_if_fail (FP_IS_DEVICE_VIRTUAL_LISTENER (self));

  task = g_task_new (self, self->cancellable, callback, user_data);
  g_object_set_data (G_OBJECT (task), "all", GINT_TO_POINTER (all));

  if (!self->connection || g_io_stream_is_closed (G_IO_STREAM (self->connection)))
    {
      g_task_return_new_error (task, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED,
                               "Listener not connected to any stream");
      return;
    }

  stream = g_io_stream_get_input_stream (G_IO_STREAM (self->connection));
  if (all)
    {
      g_input_stream_read_all_async (stream, buffer, count,
                                     G_PRIORITY_DEFAULT,
                                     self->cancellable,
                                     on_stream_read_cb,
                                     g_steal_pointer (&task));
    }
  else
    {
      g_input_stream_read_async (stream, buffer, count,
                                 G_PRIORITY_DEFAULT,
                                 self->cancellable,
                                 on_stream_read_cb,
                                 g_steal_pointer (&task));
    }
}

gsize
fp_device_virtual_listener_read_finish (FpDeviceVirtualListener *self,
                                        GAsyncResult            *result,
                                        GError                 **error)
{
  g_return_val_if_fail (g_task_is_valid (result, self), 0);

  return g_task_propagate_int (G_TASK (result), error);
}

gboolean
fp_device_virtual_listener_write_sync (FpDeviceVirtualListener *self,
                                       const char              *buffer,
                                       gsize                    count,
                                       GError                 **error)
{
  if (!self->connection || g_io_stream_is_closed (G_IO_STREAM (self->connection)))
    {
      g_set_error (error, G_IO_ERROR, G_IO_ERROR_CONNECTION_CLOSED,
                   "Listener not connected to any stream");
      return FALSE;
    }

  return g_output_stream_write_all (g_io_stream_get_output_stream (G_IO_STREAM (self->connection)),
                                    buffer,
                                    count,
                                    NULL,
                                    self->cancellable,
                                    error);
}
