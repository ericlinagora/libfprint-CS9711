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

#include "bmkt_internal.h"

void print_buffer(uint8_t *buf, int len)
{
	int i;

	for (i = 0; i < len; i++)
	{
		bmkt_dbg_log("0x%02x ", buf[i]);
		if ((i % 16) == 15)
		{
			bmkt_dbg_log("\n");
		}
	}
	bmkt_dbg_log("\n");
}

static uint64_t extractN(const uint8_t *buf, int len, int *offset, bmkt_byte_order_t byte_order)
{
	int i;
	int shift = 0;
	int ret = 0;
	int off = 0;

	if (offset)
	{
		off = *offset;
	}

	if (len > 8)
	{
		len = 8;
	}

	if (byte_order == BYTE_ORDER_LITTLE) {
		for (i = 0; i < len; i++, shift += 8)
		{
			ret |= (buf[off + i] << shift);
		}
	} else {
		for (i = len - 1; i >= 0; i--, shift += 8)
		{
			ret |= (buf[off + i] << shift);
		}
	}

	if (offset)
	{
		*offset += len;
	}

	return ret;
}

uint32_t extract32(const uint8_t *buf, int *offset, bmkt_byte_order_t byte_order)
{
	return extractN(buf, 4, offset, byte_order);
}

uint16_t extract16(const uint8_t *buf, int *offset, bmkt_byte_order_t byte_order)
{
	return extractN(buf, 2, offset, byte_order);
}

uint8_t extract8(const uint8_t *buf, int *offset, bmkt_byte_order_t byte_order)
{
	return extractN(buf, 1, offset, byte_order);
}

static void encodeN(uint64_t value, int len, uint8_t *buf, int *offset, bmkt_byte_order_t byte_order)
{
	int i;
	int shift = 0;
	int off = 0;

	if (offset)
	{
		off = *offset;
	}

	if (len > 8)
	{
		len = 8;
	}

	if (byte_order == BYTE_ORDER_LITTLE) 
	{
		for (i = 0; i < len; i++, shift += 8)
		{
			buf[off + i] = (value >> shift) & 0xFF;
		}
	} else {
		for (i = len - 1; i >= 0; i--, shift += 8)
		{
			buf[off + i] = (value >> shift) & 0xFF;
		}
	}

	if (offset)
	{
		*offset += len;
	}
}

void encode32(uint32_t value, uint8_t *buf, int *offset, bmkt_byte_order_t byte_order)
{
	encodeN(value, 4, buf, offset, byte_order);
}

void encode16(uint16_t value, uint8_t *buf, int *offset, bmkt_byte_order_t byte_order)
{
	encodeN(value, 2, buf, offset, byte_order);
}

void encode8(uint8_t value, uint8_t *buf, int *offset, bmkt_byte_order_t byte_order)
{
	encodeN(value, 1, buf, offset, byte_order);
}