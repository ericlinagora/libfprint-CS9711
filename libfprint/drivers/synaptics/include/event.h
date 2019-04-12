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
 
#ifndef _EVENT_H_
#define _EVENT_H_

#include "bmkt_internal.h"

int bmkt_event_init(bmkt_event_t *event);
int bmkt_event_set(bmkt_event_t *event);
int bmkt_event_wait(bmkt_event_t *event, int timeout);
int bmkt_event_try(bmkt_event_t *event);
int bmkt_event_clear(bmkt_event_t *event);
int bmkt_event_destroy(bmkt_event_t *event);

#endif /* _EVENT_H_ */