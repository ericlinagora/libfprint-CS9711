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

#ifndef _PLATFORM_H_
#define _PLATFORM_H_

#include <stdio.h>
#include <string.h>
#include <semaphore.h>
#include <linux/limits.h>

#include "posix.h"
#include "event.h"
#include "plat_thread.h"
#include "thread.h"

int bmkt_log(const char *format, ...);
int bmkt_log_open(const char *log_file);
int bmkt_log_close(void);

#ifdef FULL_LOGGING
#define bmkt_dbg_log(format, ...)		bmkt_log(format, ##__VA_ARGS__)
#define bmkt_info_log(format, ...)		bmkt_log(format, ##__VA_ARGS__)
#define bmkt_warn_log(format, ...)		bmkt_log(format, ##__VA_ARGS__)
#define bmkt_err_log(format, ...) 		bmkt_log(format, ##__VA_ARGS__)
#else
#define bmkt_dbg_log(format, ...)
#define bmkt_info_log(format, ...)
#define bmkt_warn_log(format, ...)		bmkt_log(format, ##__VA_ARGS__)
#define bmkt_err_log(format, ...)		bmkt_log(format, ##__VA_ARGS__)
#endif

typedef struct bmkt_event
{
	sem_t event_sem;
} bmkt_event_t;

#endif /* _PLATFORM_H_ */