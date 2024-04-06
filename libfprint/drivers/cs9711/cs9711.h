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

#define CS9711_FP_WIDTH 34
#define CS9711_FP_HEIGHT 236
#define CS9711_FP_SIZE (CS9711_FP_WIDTH * CS9711_FP_HEIGHT)

/* The default aspect ratio of 34x236 is very thin and high, so there are 2 attempts
 * at adding more colums
 */
/* Simply repeat each pixel for factor times on x - set to 1 for no-op */
#define CS9711_FP_WIDTH_FACTOR 1
/* Add interpolate pixels between each pixel on x - set to 0 for no-op. This seems to increase number of minutias detected */
#define CS9711_FP_WIDTH_INTERPOLATE 20

#if CS9711_FP_WIDTH_FACTOR != 1
#  if CS9711_FP_WIDTH_INTERPOLATE != 0
#    error Can only use one of CS9711_FP_WIDTH_FACTOR or CS9711_FP_WIDTH_INTERPOLATE
#  endif
# define CS9711_OUT_WIDTH ((CS9711_FP_WIDTH) * (CS9711_FP_WIDTH_FACTOR))
#else
# define CS9711_OUT_WIDTH ((CS9711_FP_WIDTH) + ((CS9711_FP_WIDTH) - 1) * CS9711_FP_WIDTH_INTERPOLATE)
#endif
#define CS9711_OUT_HEIGHT CS9711_FP_HEIGHT

struct _FpDeviceCs9711
{
  FpImageDevice parent;

  unsigned char image_buffer[CS9711_FP_SIZE];
};

G_DECLARE_FINAL_TYPE (FpDeviceCs9711, fpi_device_cs9711, FPI, DEVICE_CS9711, FpImageDevice)
