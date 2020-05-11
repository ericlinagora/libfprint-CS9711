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

#include <glib-2.0/glib.h>
#include "fpi-device.h"
#include "fp-sdcp-device.h"

/**
 * FpiSdcpClaim:
 * @cert_m: The per-model ECDSA certificate (x509 ASN.1 DER encoded)
 * @pk_d: The device public key (65 bytes)
 * @pk_f: The firmware public key (65 bytes)
 * @h_f: The firmware hash
 * @s_m: Signature over @pk_d using the per-model private key (64 bytes)
 * @s_d: Signature over h_f and pk_f using the device private key (64 bytes)
 *
 * Structure to hold the claim as produced by the device during a secure
 * connect. See the SDCP specification for more details.
 *
 * Note all of these may simply be memory views into a larger #GBytes created
 * using g_bytes_new_from_bytes().
 */
struct _FpiSdcpClaim
{
  /*< public >*/
  GBytes *cert_m;
  GBytes *pk_d;
  GBytes *pk_f;
  GBytes *h_f;
  GBytes *s_m;
  GBytes *s_d;
};
typedef struct _FpiSdcpClaim FpiSdcpClaim;

GType          fpi_sdcp_claim_get_type (void) G_GNUC_CONST;
FpiSdcpClaim  *fpi_sdcp_claim_new (void);
FpiSdcpClaim  *fpi_sdcp_claim_copy (FpiSdcpClaim *other);
void           fpi_sdcp_claim_free (FpiSdcpClaim *claim);

G_DEFINE_AUTOPTR_CLEANUP_FUNC (FpiSdcpClaim, fpi_sdcp_claim_free)


/**
 * FpSdcpDeviceClass:
 * @connect: Establish SDCP connection. Similar to open in #FpDeviceClass
 *   but called connect to mirror the SDCP specification.
 * @reconnect: Perform a faster reconnect. Drivers do not need to provide this
 *   function. If reconnect fails, then a normal connect will be tried.
 * @enroll_begin: Start the enrollment procedure. In the absence of an error,
 *   the driver must call fpi_sdcp_device_enroll_set_nonce() at any point. It
 *   also must report enrollment progress using fpi_device_enroll_progress().
 *   It should also store available metadata about the print in device memory.
 *   The operation is completed with fpi_sdcp_device_enroll_ready().
 * @enroll_commit: Complete the enrollment procedure. This commits the newly
 *   enrolled print to the device memory. Will only be called if enroll_begin
 *   succeeded. The passed id may be %NULL, in that case the driver must
 *   abort the enrollment process. id is owned by the base class and remains
 *   valid throughout the operation.
 * @identify: Start identification process. On completion, the driver must call
 *   fpi_sdcp_device_identify_complete(). To request the user to retry the
 *   fpi_sdcp_device_identify_retry() function is used.
 *
 *
 * These are the main entry points for drivers implementing SDCP.
 *
 * Drivers *must* eventually call the corresponding function to finish the
 * operation.
 *
 * XXX: Is the use of fpi_device_action_error() acceptable?
 *
 * Drivers *must* also handle cancellation properly for any long running
 * operation (i.e. any operation that requires capturing). It is entirely fine
 * to ignore cancellation requests for short operations (e.g. open/close).
 *
 * This API is solely intended for drivers. It is purely internal and neither
 * API nor ABI stable.
 */
struct _FpSdcpDeviceClass
{
  FpDeviceClass parent_class;

  void          (*connect)       (FpSdcpDevice *dev);
  void          (*reconnect)     (FpSdcpDevice *dev);
  void          (*close)         (FpSdcpDevice *dev);
  void          (*enroll_begin)  (FpSdcpDevice *dev);
  void          (*enroll_commit) (FpSdcpDevice *dev,
                                  GBytes       *id);
  void          (*identify)      (FpSdcpDevice *dev);
};

void fpi_sdcp_device_set_intermediat_cas (FpSdcpDevice *self,
                                          GBytes       *ca_1,
                                          GBytes       *ca_2);

void fpi_sdcp_device_get_connect_data (FpSdcpDevice *self,
                                       GBytes      **r_h,
                                       GBytes      **pk_h);
void fpi_sdcp_device_connect_complete (FpSdcpDevice *self,
                                       GBytes       *r_d,
                                       FpiSdcpClaim *claim,
                                       GBytes       *mac,
                                       GError       *error);

void fpi_sdcp_device_get_reconnect_data (FpSdcpDevice *self,
                                         GBytes      **r_h);
void fpi_sdcp_device_reconnect_complete (FpSdcpDevice *self,
                                         GBytes       *mac,
                                         GError       *error);

void fpi_sdcp_device_enroll_set_nonce (FpSdcpDevice *self,
                                       GBytes       *nonce);
void fpi_sdcp_device_enroll_ready (FpSdcpDevice *self,
                                   GError       *error);
void fpi_sdcp_device_enroll_commit_complete (FpSdcpDevice *self,
                                             GError       *error);

void fpi_sdcp_device_get_identify_data (FpSdcpDevice *self,
                                        GBytes      **nonce);
void fpi_sdcp_device_identify_retry (FpSdcpDevice *self,
                                     GError       *error);
void fpi_sdcp_device_identify_complete (FpSdcpDevice *self,
                                        GBytes       *id,
                                        GBytes       *mac,
                                        GError       *error);
