/*
 * Trivial storage driver for example programs
 *
 * Copyright (C) 2019 Benjamin Berg <bberg@redhat.com>
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

#include <glib.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <libfprint/fprint.h>

#define STORAGE_FILE "test-storage.variant"

static char *
get_print_data_descriptor (struct fp_print_data *data, struct fp_dev *dev, enum fp_finger finger)
{
	gint drv_id;
	gint devtype;

	if (data) {
		drv_id = fp_print_data_get_driver_id (data);
		devtype = fp_print_data_get_devtype (data);
	} else {
		drv_id = fp_driver_get_driver_id(fp_dev_get_driver (dev));
		devtype = fp_dev_get_devtype (dev);
	}

	return g_strdup_printf("%x/%08x/%x",
			       drv_id,
			       devtype,
			       finger);
}

static GVariantDict*
load_data(void)
{
	GVariantDict *res;
	GVariant *var;
	gchar *contents = NULL;
	gssize length = 0;

	if (!g_file_get_contents (STORAGE_FILE, &contents, &length, NULL)) {
		g_warning ("Error loading storage, assuming it is empty");
		return g_variant_dict_new(NULL);
	}

	var = g_variant_new_from_data (G_VARIANT_TYPE_VARDICT, contents, length, FALSE, NULL, NULL);

	res = g_variant_dict_new(var);
	g_variant_unref(var);
	return res;
}

static int
save_data(GVariant *data)
{
	const gchar *contents = NULL;
	gsize length;

	length = g_variant_get_size(data);
	contents = (gchar*) g_variant_get_data (data);

	if (!g_file_set_contents (STORAGE_FILE, contents, length, NULL)) {
		g_warning ("Error saving storage,!");
		return -1;
	}

	g_variant_ref_sink(data);
	g_variant_unref(data);

	return 0;
}

int
print_data_save(struct fp_print_data *fp_data, enum fp_finger finger)
{
	gchar *descr = get_print_data_descriptor (fp_data, NULL, finger);
	GVariantDict *dict;
	GVariant *val;
	guchar *data;
	gsize size;
	int res;

	dict = load_data();

	size = fp_print_data_get_data(fp_data, &data);
	val = g_variant_new_fixed_array (G_VARIANT_TYPE("y"), data, size, 1);
	g_variant_dict_insert_value (dict, descr, val);

	res = save_data(g_variant_dict_end(dict));
	g_variant_dict_unref(dict);

	return res;
}

struct fp_print_data*
print_data_load(struct fp_dev *dev, enum fp_finger finger)
{
	gchar *descr = get_print_data_descriptor (NULL, dev, finger);
	GVariantDict *dict;
	guchar *stored_data;
	gsize stored_len;
	GVariant *val;
	struct fp_print_data *res = NULL;

	dict = load_data();
	val = g_variant_dict_lookup_value (dict, descr, G_VARIANT_TYPE ("ay"));

	if (val) {
		stored_data = (guchar*) g_variant_get_fixed_array (val, &stored_len, 1);
		res = fp_print_data_from_data(stored_data, stored_len);

		g_variant_unref(val);
	}

	g_variant_dict_unref(dict);
	g_free(descr);

	return res;
}
