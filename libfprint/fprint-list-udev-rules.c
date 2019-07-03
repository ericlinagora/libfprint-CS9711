/*
 * Copyright (C) 2009 Red Hat <mjg@redhat.com> 
 * Copyright (C) 2008 Bastien Nocera <hadess@hadess.net>
 * Copyright (C) 2008 Timo Hoenig <thoenig@suse.de>, <thoenig@nouse.net>
 * Coypright (C) 2019 Benjamin Berg <bberg@redhat.com>
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

#include "fpi-context.h"
#include "fpi-device.h"

static const FpIdEntry whitelist_id_table[] = {
    /* Unsupported (for now) Validity Sensors finger print readers */
    { .vid = 0x138a, .pid = 0x0090 }, /* Found on e.g. Lenovo T460s */
    { .vid = 0x138a, .pid = 0x0091 },
    { .vid = 0x138a, .pid = 0x0094 },
    { .vid = 0x138a, .pid = 0x0097 }, /* Found on e.g. Lenovo T470s */
    { .vid = 0 },
};

static const FpIdEntry blacklist_id_table[] = {
    { .vid = 0x0483, .pid = 0x2016 },
    /* https://bugs.freedesktop.org/show_bug.cgi?id=66659 */
    { .vid = 0x045e, .pid = 0x00bb },
    { .vid = 0 },
};

static const FpDeviceClass whitelist = {
    .type = FP_DEVICE_TYPE_USB,
    .id_table = whitelist_id_table,
    .full_name = "Hardcoded whitelist"
};

GHashTable *printed = NULL;

static void print_driver (const FpDeviceClass *cls)
{
    const FpIdEntry *entry;
    gint num_printed = 0;

    if (cls->type != FP_DEVICE_TYPE_USB)
        return;

    for (entry = cls->id_table; entry->vid != 0; entry++) {
        const FpIdEntry *bl_entry;
        char *key;

	for (bl_entry = blacklist_id_table; bl_entry->vid != 0; bl_entry++) {
	    if (entry->vid == bl_entry->vid && entry->pid == bl_entry->pid) {
		break;
	    }
	}
	if (bl_entry->vid != 0)
	    continue;

	key = g_strdup_printf ("%04x:%04x", entry->vid, entry->pid);

	if (g_hash_table_lookup (printed, key) != NULL) {
	    g_free (key);
	    continue;
	}

	g_hash_table_insert (printed, key, GINT_TO_POINTER (1));

	if (num_printed == 0)
	    g_print ("# %s\n", cls->full_name);

	g_print ("SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"%04x\", ATTRS{idProduct}==\"%04x\", ATTRS{dev}==\"*\", TEST==\"power/control\", ATTR{power/control}=\"auto\"\n",
                 entry->vid, entry->pid);
	g_print ("SUBSYSTEM==\"usb\", ATTRS{idVendor}==\"%04x\", ATTRS{idProduct}==\"%04x\", ENV{LIBFPRINT_DRIVER}=\"%s\"\n",
                 entry->vid, entry->pid, cls->full_name);
	num_printed++;
    }

    if (num_printed > 0)
        g_print ("\n");
}

int main (int argc, char **argv)
{
    g_autoptr(GArray) drivers = g_array_new (FALSE, FALSE, sizeof(GType));
    guint i;

    g_print ("%p\n", drivers);
    g_print ("%p\n", fpi_get_driver_types);
    fpi_get_driver_types (drivers);

    printed = g_hash_table_new_full (g_str_hash, g_str_equal, g_free, NULL);

    for (i = 0; i < drivers->len; i++) {
	GType driver = g_array_index (drivers, GType, i);
	FpDeviceClass *cls = FP_DEVICE_CLASS (g_type_class_ref (driver));

	if (cls->type != FP_DEVICE_TYPE_USB) {
	  g_type_class_unref (cls);
	  continue;
	}

	print_driver (cls);

	g_type_class_unref (cls);
    }

    print_driver (&whitelist);

    g_hash_table_destroy (printed);

    return 0;
}
