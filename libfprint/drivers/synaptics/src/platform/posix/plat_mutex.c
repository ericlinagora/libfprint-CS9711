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
#include "mutex.h"

int bmkt_mutex_init(bmkt_mutex_t *mutex)
{
	pthread_mutex_init(&mutex->mutex, NULL);
	return BMKT_SUCCESS;
}

int bmkt_mutex_destroy(bmkt_mutex_t *mutex)
{
	int ret;

	ret = pthread_mutex_destroy(&mutex->mutex);
	if (ret != 0)
	{
		return BMKT_GENERAL_ERROR;
	}

	return BMKT_SUCCESS;
}

int bmkt_mutex_lock(bmkt_mutex_t *mutex)
{
	int ret;

	ret = pthread_mutex_lock(&mutex->mutex);
	if (ret)
	{
		return BMKT_GENERAL_ERROR;
	}

	return BMKT_SUCCESS;
}

int bmkt_mutex_unlock(bmkt_mutex_t *mutex)
{
	int ret;

	ret = pthread_mutex_unlock(&mutex->mutex);
	if (ret)
	{
		return BMKT_GENERAL_ERROR;
	}

	return BMKT_SUCCESS;
}