/*
 * Fingerprint data handling and storage
 * Copyright (C) 2007 Daniel Drake <dsd@gentoo.org>
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

#include <config.h>
#include <errno.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <unistd.h>

#include <glib.h>
#include <glib/gstdio.h>

#include "fp_internal.h"

#define DIR_PERMS 0700

struct fpi_print_data_fp2 {
	char prefix[3];
	uint16_t driver_id;
	uint32_t devtype;
	unsigned char data_type;
	unsigned char data[0];
} __attribute__((__packed__));

struct fpi_print_data_item_fp2 {
	uint32_t length;
	unsigned char data[0];
} __attribute__((__packed__));

/**
 * SECTION: print_data
 * @title: Stored prints
 * @short_description: Stored prints functions
 *
 * Stored prints are represented by a structure named #fp_print_data.
 * Stored prints are originally obtained from an enrollment function such as
 * fp_enroll_finger().
 *
 * This page documents the various operations you can do with a stored print.
 * Note that by default, "stored prints" are not actually stored anywhere
 * except in RAM. Storage needs to be handled by the API user by using the
 * fp_print_data_get_data() and fp_print_data_from_data(). This API allows
 * to convert print data into byte strings, and to reconstruct stored prints
 * from such data at a later point. You are welcome to store these byte strings
 * in any fashion that suits you.
 */

/*
 * SECTION: fpi-data
 * @title: Stored prints creation
 * @short_description: Stored prints creation functions
 *
 * Stored print can be loaded and created by certain drivers which do their own
 * print matching in hardware. Most drivers will not be using those functions.
 * See #fp_print_data for the public API counterpart.
 */

#define FP_FINGER_IS_VALID(finger) \
	((finger) >= LEFT_THUMB && (finger) <= RIGHT_LITTLE)

static struct fp_print_data *print_data_new(uint16_t driver_id,
	uint32_t devtype, enum fp_print_data_type type)
{
	struct fp_print_data *data = g_malloc0(sizeof(*data));
	fp_dbg("driver=%02x devtype=%04x", driver_id, devtype);
	data->driver_id = driver_id;
	data->devtype = devtype;
	data->type = type;
	return data;
}

static void fpi_print_data_item_free(struct fp_print_data_item *item)
{
	g_free(item);
}

struct fp_print_data_item *fpi_print_data_item_new(size_t length)
{
	struct fp_print_data_item *item = g_malloc0(sizeof(*item) + length);
	item->length = length;

	return item;
}

struct fp_print_data *fpi_print_data_new(struct fp_dev *dev)
{
	return print_data_new(dev->drv->id, dev->devtype,
		fpi_driver_get_data_type(dev->drv));
}

struct fp_print_data_item *
fpi_print_data_get_item(struct fp_print_data *data)
{
	return data->prints->data;
}

void
fpi_print_data_add_item(struct fp_print_data      *data,
			struct fp_print_data_item *item)
{
	data->prints = g_slist_prepend(data->prints, item);
}

/**
 * fp_print_data_get_data:
 * @data: the stored print
 * @ret: output location for the data buffer. Must be freed with free()
 * after use.

 * Convert a stored print into a unified representation inside a data buffer.
 * You can then store this data buffer in any way that suits you, and load
 * it back at some later time using fp_print_data_from_data().
 *
 * Returns: the size of the freshly allocated buffer, or 0 on error.
 */
API_EXPORTED size_t fp_print_data_get_data(struct fp_print_data *data,
	unsigned char **ret)
{
	struct fpi_print_data_fp2 *out_data;
	struct fpi_print_data_item_fp2 *out_item;
	struct fp_print_data_item *item;
	size_t buflen = 0;
	GSList *list_item;
	unsigned char *buf;

	G_DEBUG_HERE();

	list_item = data->prints;
	while (list_item) {
		item = list_item->data;
		buflen += sizeof(*out_item);
		buflen += item->length;
		list_item = g_slist_next(list_item);
	}

	buflen += sizeof(*out_data);
	out_data = g_malloc(buflen);

	*ret = (unsigned char *) out_data;
	buf = out_data->data;
	out_data->prefix[0] = 'F';
	out_data->prefix[1] = 'P';
	out_data->prefix[2] = '2';
	out_data->driver_id = GUINT16_TO_LE(data->driver_id);
	out_data->devtype = GUINT32_TO_LE(data->devtype);
	out_data->data_type = data->type;

	list_item = data->prints;
	while (list_item) {
		item = list_item->data;
		out_item = (struct fpi_print_data_item_fp2 *)buf;
		out_item->length = GUINT32_TO_LE(item->length);
		/* FIXME: fp_print_data_item->data content is not endianness agnostic */
		memcpy(out_item->data, item->data, item->length);
		buf += sizeof(*out_item);
		buf += item->length;
		list_item = g_slist_next(list_item);
	}

	return buflen;
}

static struct fp_print_data *fpi_print_data_from_fp1_data(unsigned char *buf,
	size_t buflen)
{
	size_t print_data_len;
	struct fp_print_data *data;
	struct fp_print_data_item *item;
	struct fpi_print_data_fp2 *raw = (struct fpi_print_data_fp2 *) buf;

	print_data_len = buflen - sizeof(*raw);
	data = print_data_new(GUINT16_FROM_LE(raw->driver_id),
		GUINT32_FROM_LE(raw->devtype), raw->data_type);
	item = fpi_print_data_item_new(print_data_len);
	/* FIXME: fp_print_data->data content is not endianness agnostic */
	memcpy(item->data, raw->data, print_data_len);
	data->prints = g_slist_prepend(data->prints, item);

	return data;
}

static struct fp_print_data *fpi_print_data_from_fp2_data(unsigned char *buf,
	size_t buflen)
{
	size_t total_data_len, item_len;
	struct fp_print_data *data;
	struct fp_print_data_item *item;
	struct fpi_print_data_fp2 *raw = (struct fpi_print_data_fp2 *) buf;
	unsigned char *raw_buf;
	struct fpi_print_data_item_fp2 *raw_item;

	total_data_len = buflen - sizeof(*raw);
	data = print_data_new(GUINT16_FROM_LE(raw->driver_id),
		GUINT32_FROM_LE(raw->devtype), raw->data_type);
	raw_buf = raw->data;
	while (total_data_len) {
		if (total_data_len < sizeof(*raw_item))
			break;
		total_data_len -= sizeof(*raw_item);

		raw_item = (struct fpi_print_data_item_fp2 *)raw_buf;
		item_len = GUINT32_FROM_LE(raw_item->length);
		fp_dbg("item len %d, total_data_len %d", (int) item_len, (int) total_data_len);
		if (total_data_len < item_len) {
			fp_err("corrupted fingerprint data");
			break;
		}
		total_data_len -= item_len;

		item = fpi_print_data_item_new(item_len);
		/* FIXME: fp_print_data->data content is not endianness agnostic */
		memcpy(item->data, raw_item->data, item_len);
		data->prints = g_slist_prepend(data->prints, item);

		raw_buf += sizeof(*raw_item);
		raw_buf += item_len;
	}

	if (g_slist_length(data->prints) == 0) {
		fp_print_data_free(data);
		data = NULL;
	}

	return data;

}

/**
 * fp_print_data_from_data:
 * @buf: the data buffer
 * @buflen: the length of the buffer

 * Load a stored print from a data buffer. The contents of said buffer must
 * be the untouched contents of a buffer previously supplied to you by the
 * fp_print_data_get_data() function.
 *
 * Returns: the stored print represented by the data, or %NULL on error. Must
 * be freed with fp_print_data_free() after use.
 */
API_EXPORTED struct fp_print_data *fp_print_data_from_data(unsigned char *buf,
	size_t buflen)
{
	struct fpi_print_data_fp2 *raw = (struct fpi_print_data_fp2 *) buf;

	fp_dbg("buffer size %zd", buflen);
	if (buflen < sizeof(*raw))
		return NULL;

	if (strncmp(raw->prefix, "FP1", 3) == 0) {
		return fpi_print_data_from_fp1_data(buf, buflen);
	} else if (strncmp(raw->prefix, "FP2", 3) == 0) {
		return fpi_print_data_from_fp2_data(buf, buflen);
	} else {
		fp_dbg("bad header prefix");
	}

	return NULL;
}

gboolean fpi_print_data_compatible(uint16_t driver_id1, uint32_t devtype1,
	enum fp_print_data_type type1, uint16_t driver_id2, uint32_t devtype2,
	enum fp_print_data_type type2)
{
	if (driver_id1 != driver_id2) {
		fp_dbg("driver ID mismatch: %02x vs %02x", driver_id1, driver_id2);
		return FALSE;
	}

	if (devtype1 != devtype2) {
		fp_dbg("devtype mismatch: %04x vs %04x", devtype1, devtype2);
		return FALSE;
	}

	if (type1 != type2) {
		fp_dbg("type mismatch: %d vs %d", type1, type2);
		return FALSE;
	}

	return TRUE;
}

/**
 * fp_print_data_free:
 * @data: the stored print to destroy. If NULL, function simply returns.
 *
 * Frees a stored print. Must be called when you are finished using the print.
 */
API_EXPORTED void fp_print_data_free(struct fp_print_data *data)
{
	if (data)
		g_slist_free_full(data->prints, (GDestroyNotify)fpi_print_data_item_free);
	g_free(data);
}

/**
 * fp_print_data_get_driver_id:
 * @data: the stored print

 * Gets the [driver ID](advanced-topics.html#driver_id) for a stored print. The driver ID
 * indicates which driver the print originally came from. The print is
 * only usable with a device controlled by that driver.
 *
 * Returns: the driver ID of the driver compatible with the print
 */
API_EXPORTED uint16_t fp_print_data_get_driver_id(struct fp_print_data *data)
{
	return data->driver_id;
}

/**
 * fp_print_data_get_devtype:
 * @data: the stored print

 * Gets the [devtype](advanced-topics.html#device-types) for a stored print. The devtype represents
 * which type of device under the parent driver is compatible with the print.
 *
 * Returns: the devtype of the device range compatible with the print
 */
API_EXPORTED uint32_t fp_print_data_get_devtype(struct fp_print_data *data)
{
	return data->devtype;
}
