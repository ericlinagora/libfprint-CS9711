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
#include <time.h>
#include <errno.h>
#include <stdarg.h>
#include <string.h>
#include <stdio.h>
#include "fp_internal.h"

uint32_t extract32(const uint8_t *buf, int *offset);
uint16_t extract16(const uint8_t *buf, int *offset);
uint8_t extract8(const uint8_t *buf, int *offset);
void print_buffer(uint8_t *buf, int len);


#define bmkt_dbg_log	fp_dbg
#define bmkt_info_log	fp_info
#define bmkt_warn_log	fp_warn
#define bmkt_err_log	fp_err

void bmkt_op_next_state(bmkt_sensor_t *sensor);

#endif /* _BMKT_INTERNAL_H_ */