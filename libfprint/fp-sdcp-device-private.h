/*
 * FpSdcpDevice - A base class for SDCP enabled devices
 * Copyright (C) 2020 Benjamin Berg <bberg@redhat.com>
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

#include "fpi-sdcp-device.h"

#include <nss.h>
#include <keyhi.h>
#include <keythi.h>
#include <pk11pub.h>

typedef struct
{
  GError *enroll_pre_commit_error;

  /* XXX: Do we want a separate SDCP session object?
   */

  GPtrArray *intermediate_cas;

  /* Host random for the connection */
  guint8            host_random[32];

  NSSInitContext   *nss_init_context;
  PK11SlotInfo     *slot;
  SECKEYPrivateKey *host_key_private;
  SECKEYPublicKey  *host_key_public;

  /* Master secret is required for reconnects.
   * TODO: We probably want to serialize this to disk so it can survive
   *       fprintd idle shutdown. */
  PK11SymKey *master_secret;
  PK11SymKey *mac_secret;

} FpSdcpDevicePrivate;

void fpi_sdcp_device_connect (FpSdcpDevice *self);
void fpi_sdcp_device_reconnect (FpSdcpDevice *self);

void fpi_sdcp_device_enroll (FpSdcpDevice *self);
void fpi_sdcp_device_identify (FpSdcpDevice *self);
