/*
 * Virtual driver for image device debugging
 *
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

/*
 * This is a virtual driver to debug the image based drivers. A small
 * python script is provided to connect to it via a socket, allowing
 * prints to be sent to this device programatically.
 * Using this it is possible to test libfprint and fprintd.
 */

#define FP_COMPONENT "virtual_image"

#include "fpi-log.h"

#include "../fpi-image.h"
#include "../fpi-image-device.h"

#include <glib/gstdio.h>
#include <gio/gio.h>
#include <gio/gunixsocketaddress.h>

struct _FpDeviceVirtualImage
{
  FpImageDevice      parent;

  GSocketListener   *listener;
  GSocketConnection *connection;
  GCancellable      *cancellable;

  gint               socket_fd;
  gint               client_fd;

  gboolean           automatic_finger;
  FpImage           *recv_img;
  gint               recv_img_hdr[2];
};

G_DECLARE_FINAL_TYPE (FpDeviceVirtualImage, fpi_device_virtual_image, FPI, DEVICE_VIRTUAL_IMAGE, FpImageDevice)
G_DEFINE_TYPE (FpDeviceVirtualImage, fpi_device_virtual_image, FP_TYPE_IMAGE_DEVICE)

static void start_listen (FpDeviceVirtualImage *dev);
static void recv_image (FpDeviceVirtualImage *dev,
                        GInputStream         *stream);

static void
recv_image_img_recv_cb (GObject      *source_object,
                        GAsyncResult *res,
                        gpointer      user_data)
{
  g_autoptr(GError) error = NULL;
  FpDeviceVirtualImage *self;
  FpImageDevice *device;
  gboolean success;
  gsize bytes = 0;

  success = g_input_stream_read_all_finish (G_INPUT_STREAM (source_object), res, &bytes, &error);

  if (!success || bytes == 0)
    {
      if (!success)
        {
          if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
            return;
          g_warning ("Error receiving header for image data: %s", error->message);
        }

      self = FPI_DEVICE_VIRTUAL_IMAGE (user_data);
      g_io_stream_close (G_IO_STREAM (self->connection), NULL, NULL);
      g_clear_object (&self->connection);
      return;
    }

  self = FPI_DEVICE_VIRTUAL_IMAGE (user_data);
  device = FP_IMAGE_DEVICE (self);

  if (self->automatic_finger)
    fpi_image_device_report_finger_status (device, TRUE);
  fpi_image_device_image_captured (device, g_steal_pointer (&self->recv_img));
  if (self->automatic_finger)
    fpi_image_device_report_finger_status (device, FALSE);

  /* And, listen for more images from the same client. */
  recv_image (self, G_INPUT_STREAM (source_object));
}

static void
recv_image_hdr_recv_cb (GObject      *source_object,
                        GAsyncResult *res,
                        gpointer      user_data)
{
  g_autoptr(GError) error = NULL;
  FpDeviceVirtualImage *self;
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
          g_warning ("Error receiving header for image data: %s", error->message);
        }

      self = FPI_DEVICE_VIRTUAL_IMAGE (user_data);
      g_io_stream_close (G_IO_STREAM (self->connection), NULL, NULL);
      g_clear_object (&self->connection);
      return;
    }

  self = FPI_DEVICE_VIRTUAL_IMAGE (user_data);
  if (self->recv_img_hdr[0] > 5000 || self->recv_img_hdr[1] > 5000)
    {
      g_warning ("Image header suggests an unrealistically large image, disconnecting client.");
      g_io_stream_close (G_IO_STREAM (self->connection), NULL, NULL);
      g_clear_object (&self->connection);
    }

  if (self->recv_img_hdr[0] < 0 || self->recv_img_hdr[1] < 0)
    {
      switch (self->recv_img_hdr[0])
        {
        case -1:
          /* -1 is a retry error, just pass it through */
          fpi_image_device_retry_scan (FP_IMAGE_DEVICE (self), self->recv_img_hdr[1]);
          break;

        case -2:
          /* -2 is a fatal error, just pass it through*/
          fpi_image_device_session_error (FP_IMAGE_DEVICE (self),
                                          fpi_device_error_new (self->recv_img_hdr[1]));
          break;

        case -3:
          /* -3 sets/clears automatic finger detection for images */
          self->automatic_finger = !!self->recv_img_hdr[1];
          break;

        case -4:
          /* -4 submits a finger detection report */
          fpi_image_device_report_finger_status (FP_IMAGE_DEVICE (self),
                                                 !!self->recv_img_hdr[1]);
          break;

        default:
          /* disconnect client, it didn't play fair */
          g_io_stream_close (G_IO_STREAM (self->connection), NULL, NULL);
          g_clear_object (&self->connection);
        }

      /* And, listen for more images from the same client. */
      recv_image (self, G_INPUT_STREAM (source_object));
      return;
    }

  self->recv_img = fp_image_new (self->recv_img_hdr[0], self->recv_img_hdr[1]);
  g_debug ("image data: %p", self->recv_img->data);
  g_input_stream_read_all_async (G_INPUT_STREAM (source_object),
                                 (guint8 *) self->recv_img->data,
                                 self->recv_img->width * self->recv_img->height,
                                 G_PRIORITY_DEFAULT,
                                 self->cancellable,
                                 recv_image_img_recv_cb,
                                 self);
}

static void
recv_image (FpDeviceVirtualImage *dev, GInputStream *stream)
{
  g_input_stream_read_all_async (stream,
                                 dev->recv_img_hdr,
                                 sizeof (dev->recv_img_hdr),
                                 G_PRIORITY_DEFAULT,
                                 dev->cancellable,
                                 recv_image_hdr_recv_cb,
                                 dev);
}

static void
new_connection_cb (GObject *source_object, GAsyncResult *res, gpointer user_data)
{
  g_autoptr(GError) error = NULL;
  GSocketConnection *connection;
  GInputStream *stream;
  FpDeviceVirtualImage *dev = user_data;

  connection = g_socket_listener_accept_finish (G_SOCKET_LISTENER (source_object),
                                                res,
                                                NULL,
                                                &error);
  if (!connection)
    {
      if (g_error_matches (error, G_IO_ERROR, G_IO_ERROR_CANCELLED))
        return;

      g_warning ("Error accepting a new connection: %s", error->message);
      start_listen (dev);
    }

  /* Always further connections (but we disconnect them immediately
   * if we already have a connection). */
  start_listen (dev);
  if (dev->connection)
    {
      g_io_stream_close (G_IO_STREAM (connection), NULL, NULL);
      g_object_unref (connection);
      return;
    }

  dev->connection = connection;
  dev->automatic_finger = TRUE;
  stream = g_io_stream_get_input_stream (G_IO_STREAM (connection));

  recv_image (dev, stream);

  fp_dbg ("Got a new connection!");
}

static void
start_listen (FpDeviceVirtualImage *dev)
{
  g_socket_listener_accept_async (dev->listener,
                                  dev->cancellable,
                                  new_connection_cb,
                                  dev);
}

static void
dev_init (FpImageDevice *dev)
{
  g_autoptr(GError) error = NULL;
  g_autoptr(GSocketListener) listener = NULL;
  FpDeviceVirtualImage *self = FPI_DEVICE_VIRTUAL_IMAGE (dev);
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

      fpi_image_device_open_complete (FP_IMAGE_DEVICE (dev), g_steal_pointer (&error));

      return;
    }

  self->listener = g_steal_pointer (&listener);
  self->cancellable = g_cancellable_new ();

  start_listen (self);

  fpi_image_device_open_complete (dev, NULL);
}

static void
dev_deinit (FpImageDevice *dev)
{
  FpDeviceVirtualImage *self = FPI_DEVICE_VIRTUAL_IMAGE (dev);

  G_DEBUG_HERE ();

  g_cancellable_cancel (self->cancellable);
  g_clear_object (&self->cancellable);
  g_clear_object (&self->listener);
  g_clear_object (&self->connection);

  fpi_image_device_close_complete (dev, NULL);
}

static void
fpi_device_virtual_image_init (FpDeviceVirtualImage *self)
{
}

static const FpIdEntry driver_ids[] = {
  { .virtual_envvar = "FP_VIRTUAL_IMAGE" },
  { .virtual_envvar = NULL }
};

static void
fpi_device_virtual_image_class_init (FpDeviceVirtualImageClass *klass)
{
  FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);
  FpImageDeviceClass *img_class = FP_IMAGE_DEVICE_CLASS (klass);

  dev_class->id = FP_COMPONENT;
  dev_class->full_name = "Virtual image device for debugging";
  dev_class->type = FP_DEVICE_TYPE_VIRTUAL;
  dev_class->id_table = driver_ids;

  img_class->img_open = dev_init;
  img_class->img_close = dev_deinit;
}
