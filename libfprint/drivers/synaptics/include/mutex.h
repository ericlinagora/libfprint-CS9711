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

#ifndef _MUTEX_H_
#define _MUTEX_H_

#include "bmkt_internal.h"

int bmkt_mutex_init(bmkt_mutex_t *mutex);
int bmkt_mutex_destroy(bmkt_mutex_t *mutex);
int bmkt_mutex_lock(bmkt_mutex_t *mutex);
int bmkt_mutex_unlock(bmkt_mutex_t *mutex);

#endif /* _MUTEX_H_ */