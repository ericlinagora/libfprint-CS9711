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

#define FP_COMPONENT "sdcp_device"
#include "fpi-log.h"

#include "fp-sdcp-device-private.h"

/**
 * SECTION: fp-sdcp-device
 * @title: FpSdcpDevice
 * @short_description: SDCP device subclass
 *
 * This is a base class for devices implementing the SDCP security protocol.
 */

G_DEFINE_ABSTRACT_TYPE_WITH_PRIVATE (FpSdcpDevice, fp_sdcp_device, FP_TYPE_DEVICE)

#if 0
/* XXX: We'll very likely want/need some properties on this class. */
enum {
  PROP_0,
  N_PROPS
};

static GParamSpec *properties[N_PROPS];
#endif

/*******************************************************/

/* Callbacks/vfuncs */
static void
fp_sdcp_device_open (FpDevice *device)
{
  FpSdcpDevice *self = FP_SDCP_DEVICE (device);
  FpSdcpDevicePrivate *priv = fp_sdcp_device_get_instance_private (self);

  /* Try a reconnect if we still have the mac secret. */
  if (priv->mac_secret)
    fpi_sdcp_device_reconnect (self);
  else
    fpi_sdcp_device_connect (self);
}

static void
fp_sdcp_device_enroll (FpDevice *device)
{
  FpSdcpDevice *self = FP_SDCP_DEVICE (device);

  fpi_sdcp_device_enroll (self);
}

static void
fp_sdcp_device_identify (FpDevice *device)
{
  FpSdcpDevice *self = FP_SDCP_DEVICE (device);

  fpi_sdcp_device_identify (self);
}

/*********************************************************/

static void
fp_sdcp_device_finalize (GObject *object)
{
  FpSdcpDevice *self = (FpSdcpDevice *) object;
  FpSdcpDevicePrivate *priv = fp_sdcp_device_get_instance_private (self);

  g_clear_pointer (&priv->intermediate_cas, g_ptr_array_unref);
  g_clear_pointer (&priv->slot, PK11_FreeSlot);
  g_clear_pointer (&priv->host_key_private, SECKEY_DestroyPrivateKey);
  g_clear_pointer (&priv->host_key_public, SECKEY_DestroyPublicKey);
  g_clear_pointer (&priv->master_secret, PK11_FreeSymKey);
  g_clear_pointer (&priv->mac_secret, PK11_FreeSymKey);
  g_clear_pointer (&priv->nss_init_context, NSS_ShutdownContext);

  G_OBJECT_CLASS (fp_sdcp_device_parent_class)->finalize (object);
}

static void
fp_sdcp_device_get_property (GObject    *object,
                             guint       prop_id,
                             GValue     *value,
                             GParamSpec *pspec)
{
  switch (prop_id)
    {
    default:
      G_OBJECT_WARN_INVALID_PROPERTY_ID (object, prop_id, pspec);
    }
}

static void
fp_sdcp_device_constructed (GObject *obj)
{
  G_OBJECT_CLASS (fp_sdcp_device_parent_class)->constructed (obj);
}

static void
fp_sdcp_device_class_init (FpSdcpDeviceClass *klass)
{
  GObjectClass *object_class = G_OBJECT_CLASS (klass);
  FpDeviceClass *fp_device_class = FP_DEVICE_CLASS (klass);

  object_class->finalize = fp_sdcp_device_finalize;
  object_class->get_property = fp_sdcp_device_get_property;
  object_class->constructed = fp_sdcp_device_constructed;

  fp_device_class->open = fp_sdcp_device_open;
  fp_device_class->enroll = fp_sdcp_device_enroll;
  fp_device_class->verify = fp_sdcp_device_identify;
  fp_device_class->identify = fp_sdcp_device_identify;

#if 0
  g_object_class_install_properties (object_class, N_PROPS, properties);
#endif
}

static void
fp_sdcp_device_init (FpSdcpDevice *self)
{
  FpSdcpDevicePrivate *priv = fp_sdcp_device_get_instance_private (self);

  priv->intermediate_cas = g_ptr_array_new_with_free_func ((GDestroyNotify) g_bytes_unref);
}
