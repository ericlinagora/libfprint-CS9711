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

#ifndef _THREAD_H_
#define _THREAD_H_

#include "plat_thread.h"

typedef enum 
{
	BMKT_THREAD_STATE_UNINITIALIZED = 0,
	BMKT_THREAD_STATE_RUNNING,
	BMKT_THREAD_STATE_FINISHED,
} bmkt_thread_state_t;

#ifdef WIN32
typedef DWORD(WINAPI *thread_func_t)(LPVOID lpThreadParameter);
#else
typedef void * (*thread_func_t)(void *ctx);
#endif

typedef struct bmkt_thread
{
	bmkt_thread_state_t state;
	plat_thread_t plat_thread;
} bmkt_thread_t;

int bmkt_thread_create(bmkt_thread_t *thread, thread_func_t fn, void *ctx);
int bmkt_thread_destroy(bmkt_thread_t *thread);

#endif /* _THREAD_H_ */