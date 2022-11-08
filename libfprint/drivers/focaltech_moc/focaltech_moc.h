/*
 * Copyright (C) 2021 Focaltech Microelectronics
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

#include "fpi-device.h"
#include "fpi-ssm.h"
#include <libusb.h>

#include <stdio.h>
#include <stdlib.h>

G_DECLARE_FINAL_TYPE (FpiDeviceFocaltechMoc, fpi_device_focaltech_moc, FPI, DEVICE_FOCALTECH_MOC, FpDevice)

#define FOCALTECH_MOC_DRIVER_FULLNAME "Focaltech MOC Sensors"

#define FOCALTECH_MOC_CMD_TIMEOUT 1000
#define FOCALTECH_MOC_MAX_FINGERS 10
#define FOCALTECH_MOC_UID_PREFIX_LENGTH 8
#define FOCALTECH_MOC_USER_ID_LENGTH 64

typedef void (*FocaltechCmdMsgCallback) (FpiDeviceFocaltechMoc *self,
                                         GError                *error);

struct _FpiDeviceFocaltechMoc
{
  FpDevice        parent;
  FpiSsm         *task_ssm;
  FpiSsm         *cmd_ssm;
  FpiUsbTransfer *cmd_transfer;
  gboolean        cmd_cancelable;
  gsize           cmd_len_in;
  int             num_frames;
  int             delete_slot;
  guint8          bulk_in_ep;
  guint8          bulk_out_ep;
};
