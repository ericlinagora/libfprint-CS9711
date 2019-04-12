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


#include "bmkt.h"
#include "platform.h"

#include <stdarg.h>

FILE *g_log_file;

int bmkt_log_open(const char *log_file)
{
	if (log_file == NULL)
	{
		g_log_file = stdout;
		return BMKT_SUCCESS;
	}

	g_log_file = fopen(log_file, "w");
	if (g_log_file == NULL)
	{
		g_log_file = stdout;
		return BMKT_GENERAL_ERROR;
	}

	return BMKT_SUCCESS;
}

int bmkt_log_close(void)
{
	if (g_log_file != stdout)
	{
		fclose(g_log_file);
	}

    return BMKT_SUCCESS;
}

int bmkt_log(const char *format, ...)
{
	va_list args;

	if (g_log_file == NULL)
	{
		return BMKT_GENERAL_ERROR;
	}

  	va_start(args, format);
  	vfprintf(g_log_file, format, args);
  	va_end(args);

  	fflush(g_log_file);

  	return BMKT_SUCCESS;
}
