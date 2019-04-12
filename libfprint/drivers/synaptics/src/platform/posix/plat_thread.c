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

int bmkt_thread_create(bmkt_thread_t *thread, thread_func_t fn, void *ctx)
{
	int ret;

	ret = pthread_create(&thread->plat_thread.thread, NULL, fn, ctx);
	if (ret)
	{
		return BMKT_GENERAL_ERROR;
	}

	thread->state = BMKT_THREAD_STATE_RUNNING;
	return BMKT_SUCCESS;
}

int bmkt_thread_destroy(bmkt_thread_t *thread)
{
	int ret;
	void *thread_ret_val;
	thread->state = BMKT_THREAD_STATE_FINISHED;

	ret = pthread_cancel(thread->plat_thread.thread);
	if (ret)
	{
		return BMKT_GENERAL_ERROR;
	}

	ret = pthread_join(thread->plat_thread.thread, &thread_ret_val);
	if (ret)
	{
		return BMKT_GENERAL_ERROR;
	}

	return BMKT_SUCCESS;
}