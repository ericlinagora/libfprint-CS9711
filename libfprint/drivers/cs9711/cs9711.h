/*
 * Chipsailing CS9711Fingprint driver
 *
 * Modified based on driver vfs301* so keeping original notice:
 *
 * Copyright (c) 2011-2012 Andrej Krutak <dev@andree.sk>
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

#include "fpi-usb-transfer.h"
#include "fpi-image-device.h"

#define CS9711_WIDTH 68
#define CS9711_HEIGHT 118

#define CS9711_FRAME_SIZE (CS9711_WIDTH * CS9711_HEIGHT)

struct _FpDeviceCs9711
{
  FpImageDevice parent;

  unsigned char image_buffer[CS9711_FRAME_SIZE];
};

G_DECLARE_FINAL_TYPE (FpDeviceCs9711, fpi_device_cs9711, FPI, DEVICE_CS9711, FpImageDevice)
