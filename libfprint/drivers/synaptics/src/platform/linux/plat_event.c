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

#include <semaphore.h>
#include <time.h>
#include <errno.h>
#include "event.h"

int bmkt_event_init(bmkt_event_t *event)
{
	sem_init(&event->event_sem, 0, 0);
	return BMKT_SUCCESS;
}

int bmkt_event_set(bmkt_event_t *event)
{
	int ret;

	ret = sem_post(&event->event_sem);
	if (ret) {
		if (errno == EOVERFLOW)
			return BMKT_GENERAL_ERROR;
		else
			return BMKT_INVALID_PARAM;
	}

	return BMKT_SUCCESS;
}

int bmkt_event_wait(bmkt_event_t *event, int timeout)
{
	int ret;

	if (timeout) {
		struct timespec ts;
		long nsec;

		clock_gettime(CLOCK_MONOTONIC, &ts);
		nsec = ts.tv_nsec + (timeout % 1000) * 1000 * 1000;
		ts.tv_nsec = nsec % (1000 * 1000 * 1000);
		ts.tv_sec += (timeout / 1000) + (nsec / (1000 * 1000 * 1000));

		for (;;) {
			ret = sem_timedwait(&event->event_sem, &ts);
			if (ret) {
				bmkt_info_log("%s: sem_timedwait: %d\n", __func__, ret);
				if (errno == EINTR) {
					continue;
				} else if (errno == ETIMEDOUT) {
					ret = BMKT_OP_TIME_OUT;
				} else {
					ret = BMKT_GENERAL_ERROR;
				}
			}
			break;
		}
	} else {
		for (;;) {
			ret = sem_wait(&event->event_sem);
			if (ret) {
				if (errno == EINTR) {
					continue;
				} else if (errno == EINVAL) {
					return BMKT_INVALID_PARAM;
				} else {
					return BMKT_GENERAL_ERROR;
				}
			}
			break;
		}
	}

	return BMKT_SUCCESS;
}

int bmkt_event_try(bmkt_event_t *event)
{
	int ret;

	ret = sem_trywait(&event->event_sem);
	if (ret)
		return BMKT_EVENT_NOT_SET;

	return BMKT_SUCCESS;
}

int bmkt_event_clear(bmkt_event_t *event)
{
	int ret;

	for (;;) {
		ret = sem_trywait(&event->event_sem);
		if (ret) {
			if (errno == EAGAIN) {
				break;
			} else {
				return BMKT_GENERAL_ERROR;
			}
		}
	}

	return BMKT_SUCCESS;
}

int bmkt_event_destroy(bmkt_event_t *event)
{
	sem_destroy(&event->event_sem);
	return BMKT_SUCCESS;
}