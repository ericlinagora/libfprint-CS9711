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

#ifndef _BMKT_INTERNAL_H_
#define _BMKT_INTERNAL_H_

#include "bmkt.h"
#include "bmkt_message.h"
#include "platform.h"

#define container_of(ptr, type, member) ({						\
		const typeof( ((type *)0)->member ) *__mptr = (ptr);	\
		(type *)( (char *)__mptr - offsetof(type,member) );})

typedef enum
{
	BYTE_ORDER_BIG = 0,
	BYTE_ORDER_LITTLE = 1,
	BYTE_ORDER_SENSOR = BYTE_ORDER_LITTLE,
} bmkt_byte_order_t;

uint32_t extract32(const uint8_t *buf, int *offset, bmkt_byte_order_t byte_order);
uint16_t extract16(const uint8_t *buf, int *offset, bmkt_byte_order_t byte_order);
uint8_t extract8(const uint8_t *buf, int *offset, bmkt_byte_order_t byte_order);

void encode32(uint32_t value, uint8_t *buf, int *offset, bmkt_byte_order_t byte_order);
void encode16(uint16_t value, uint8_t *buf, int *offset, bmkt_byte_order_t byte_order);
void encode8(uint8_t value, uint8_t *buf, int *offset, bmkt_byte_order_t byte_order);

#ifdef FULL_LOGGING
void print_buffer(uint8_t *buf, int len);
#endif

#endif /* _BMKT_INTERNAL_H_ */