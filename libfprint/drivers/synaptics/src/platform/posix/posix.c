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

#include <sys/types.h>
#include <errno.h>
#include <time.h>

#include "bmkt_internal.h"
#include "sensor.h"

int bmkt_sleep(int ms)
{
	struct timespec ts;
	struct timespec rem;

	ts.tv_sec = ms / 1000;
	ts.tv_nsec = (ms % 1000) * 1000 * 1000;
	for (;;)
	{
		if (nanosleep(&ts, &rem) == 0)
		{
			break;
		}
		else
		{
			if (errno == EINTR)
			{
				ts = rem;
				continue;
			}
			return BMKT_GENERAL_ERROR;
		}
	}

	return BMKT_SUCCESS;
}

#ifdef THREAD_SUPPORT
void *bmkt_interrupt_thread(void *ctx)
{
	int ret;
	bmkt_sensor_t *sensor = (bmkt_sensor_t *)ctx;

	while (sensor->interrupt_thread.state != BMKT_THREAD_STATE_FINISHED
				&& sensor->sensor_state != BMKT_SENSOR_STATE_EXIT)
	{
		ret = bmkt_process_pending_interrupts(sensor);
		if (ret != BMKT_SUCCESS)
		{
			bmkt_err_log("Failed to run state: %d\n", ret);
            return (void*)BMKT_GENERAL_ERROR;
		}
	}

	return (void*)BMKT_SUCCESS;
}
#endif /* THREAD_SUPPORT */