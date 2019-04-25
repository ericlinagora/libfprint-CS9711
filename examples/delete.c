/*
 * Example fingerprint delete finger program, which delete the right index
 * finger which has been previously enrolled to disk.
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

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include <libfprint/fprint.h>

struct fp_dscv_dev *discover_device(struct fp_dscv_dev **discovered_devs)
{
	struct fp_dscv_dev *ddev = discovered_devs[0];
	struct fp_driver *drv;
	if (!ddev)
		return NULL;
	
	drv = fp_dscv_dev_get_driver(ddev);
	printf("Found device claimed by %s driver\n", fp_driver_get_full_name(drv));
	return ddev;
}


int main(void)
{
	int r = 1;
	struct fp_dscv_dev *ddev;
	struct fp_dscv_dev **discovered_devs;
	struct fp_dev *dev;
	struct fp_print_data *data;

	setenv ("G_MESSAGES_DEBUG", "all", 0);
	setenv ("LIBUSB_DEBUG", "3", 0);

	r = fp_init();
	if (r < 0) {
		fprintf(stderr, "Failed to initialize libfprint\n");
		exit(1);
	}

	discovered_devs = fp_discover_devs();
	if (!discovered_devs) {
		fprintf(stderr, "Could not discover devices\n");
		goto out;
	}

	ddev = discover_device(discovered_devs);
	if (!ddev) {
		fprintf(stderr, "No devices detected.\n");
		goto out;
	}

	dev = fp_dev_open(ddev);
	fp_dscv_devs_free(discovered_devs);
	if (!dev) {
		fprintf(stderr, "Could not open device.\n");
		goto out;
	}

	printf("Opened device. Loading previously enrolled right index finger "
		"data...\n");

	r = fp_print_data_load(dev, RIGHT_INDEX, &data);
	if (r != 0) {
		fprintf(stderr, "Failed to load fingerprint, error %d\n", r);
		fprintf(stderr, "Did you remember to enroll your right index finger "
			"first?\n");
		goto out_close;
	}

	printf("Print loaded. delete data in sensor.\n");
	if(!fp_dev_supports_data_in_sensor(dev))
	{
		printf("This driver doesn't support to store data in sensor.\n");
		goto out_close;
	}
		
	r = fp_delete_finger(dev, data);
	fp_print_data_free(data);
	if (r) {
		printf("delete finger failed with error %d :(\n", r);
	}
	else
	{
		printf("sensor data deleted. now delete host data");
		r = fp_print_data_delete(dev, RIGHT_INDEX);
		if (r < 0) {
			printf("Delete sensor data successfully but delete host data failed. %d :(\n", r);
		}
	}
	
out_close:
	fp_dev_close(dev);
out:
	fp_exit();
	return r;
}


