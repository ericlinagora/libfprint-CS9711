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
#include "sensor.h"

void print_buffer(uint8_t *buf, int len)
{
	int i;

	for (i = 0; i < len; i++)
	{
		bmkt_dbg_log("0x%02x ", buf[i]);
		if ((i % 16) == 15)
		{
			bmkt_dbg_log("");
		}
	}
	bmkt_dbg_log("");
}

uint32_t extract32(const uint8_t *buf, int *offset)
{
	uint32_t ret = 0;
	int off = 0;
	if (offset)
	{
		off = *offset;
	}
	ret = GUINT32_FROM_LE(*(uint32_t*)(buf + off));
	if (offset)
	{
		*offset += 4;
	}
	return ret;
}

uint16_t extract16(const uint8_t *buf, int *offset)
{
	uint16_t ret = 0;
	int off = 0;
	if (offset)
	{
		off = *offset;
	}
	ret = GUINT16_FROM_LE(*(uint16_t*)(buf + off));
	if (offset)
	{
		*offset += 2;
	}
	return ret;
}

uint8_t extract8(const uint8_t *buf, int *offset)
{
	uint8_t ret = 0;
	int off = 0;
	if (offset)
	{
		off = *offset;
	}
	ret = *(buf + off);
	if (offset)
	{
		*offset += 1;
	}
	return ret;
}

