/*
 * FPrint spidev transfer handling
 * Copyright (C) 2019-2020 Benjamin Berg <bberg@redhat.com>
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

#pragma once

#include "fpi-compat.h"
#include "fpi-device.h"

G_BEGIN_DECLS

#define FPI_TYPE_SPI_TRANSFER (fpi_spi_transfer_get_type ())

typedef struct _FpiSpiTransfer FpiSpiTransfer;
typedef struct _FpiSsm         FpiSsm;

typedef void (*FpiSpiTransferCallback)(FpiSpiTransfer *transfer,
                                       FpDevice       *dev,
                                       gpointer        user_data,
                                       GError         *error);

/**
 * FpiSpiTransfer:
 * @device: The #FpDevice that the transfer belongs to.
 * @ssm: Storage slot to associate the transfer with a state machine.
 *   Used by fpi_ssm_spi_transfer_cb() to modify the given state machine.
 * @length_wr: The length of the write buffer
 * @length_rd: The length of the read buffer
 * @buffer_wr: The write buffer.
 * @buffer_rd: The read buffer.
 *
 * Helper for handling SPI transfers. Currently transfers can either be pure
 * write/read transfers or a write followed by a read (full duplex support
 * can easily be added if desired).
 */
struct _FpiSpiTransfer
{
  /*< public >*/
  FpDevice *device;

  FpiSsm   *ssm;

  gssize    length_wr;
  gssize    length_rd;

  guchar   *buffer_wr;
  guchar   *buffer_rd;

  /*< private >*/
  guint ref_count;

  int   spidev_fd;

  /* Callbacks */
  gpointer               user_data;
  FpiSpiTransferCallback callback;

  /* Data free function */
  GDestroyNotify free_buffer_wr;
  GDestroyNotify free_buffer_rd;
};

GType              fpi_spi_transfer_get_type (void) G_GNUC_CONST;
FpiSpiTransfer     *fpi_spi_transfer_new (FpDevice *device,
                                          int       spidev_fd);
FpiSpiTransfer     *fpi_spi_transfer_ref (FpiSpiTransfer *self);
void               fpi_spi_transfer_unref (FpiSpiTransfer *self);

void               fpi_spi_transfer_write (FpiSpiTransfer *transfer,
                                           gsize           length);

FP_GNUC_ACCESS (read_only, 2, 3)
void               fpi_spi_transfer_write_full (FpiSpiTransfer *transfer,
                                                guint8         *buffer,
                                                gsize           length,
                                                GDestroyNotify  free_func);

void               fpi_spi_transfer_read (FpiSpiTransfer *transfer,
                                          gsize           length);

FP_GNUC_ACCESS (write_only, 2, 3)
void               fpi_spi_transfer_read_full (FpiSpiTransfer *transfer,
                                               guint8         *buffer,
                                               gsize           length,
                                               GDestroyNotify  free_func);

void               fpi_spi_transfer_submit (FpiSpiTransfer        *transfer,
                                            GCancellable          *cancellable,
                                            FpiSpiTransferCallback callback,
                                            gpointer               user_data);

gboolean           fpi_spi_transfer_submit_sync (FpiSpiTransfer *transfer,
                                                 GError        **error);


G_DEFINE_AUTOPTR_CLEANUP_FUNC (FpiSpiTransfer, fpi_spi_transfer_unref)

G_END_DECLS
