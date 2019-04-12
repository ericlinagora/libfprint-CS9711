/*
 * Copyright (C) 2019 Synaptics Inc
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

#ifndef CRC_H_
#define CRC_H_

#include <stdint.h>

uint32_t compute_crc32(uint8_t *data, uint8_t length, uint32_t prev_crc32);

enum checksum_crc_polynomial
{
    CHECKSUM_CRC_POLY1,  /* polynomial: 0xedb88320 */
    CHECKSUM_CRC_POLY2   /* polynomial: 0x04c11db7 */
};

int crc_checksum(uint32_t initialValue, uint32_t *checksum, uint8_t *msg, uint32_t len, uint32_t poly);

#endif /* CRC_H_ */