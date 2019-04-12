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

#ifndef __synaptics_h__
#define __synaptics_h__

#define SYNAPTICS_VENDOR_ID          0x06cb
#define SYNAPTICS_PRODUCT_ID_A9	    0x00a9

/* Number of enroll stages */
#define ENROLL_SAMPLES		8

#define SYNAPTICS_DRIVER_FULLNAME    "Synaptics Sensors"
#include "bmkt.h"
#include "bmkt_response.h"


struct syna_enroll_resp_data
{
	int progress;
};
typedef enum syna_state
{
	SYNA_STATE_UNINIT 							= 0,
	SYNA_STATE_IDLE								,
	SYNA_STATE_ENROLL							,
	SYNA_STATE_IDENTIFY							,
	SYNA_STATE_IDENTIFY_DELAY_RESULT			,
	SYNA_STATE_VERIFY							,
	SYNA_STATE_VERIFY_DELAY_RESULT				,
} syna_state_t;

typedef struct synaptics_dev_s 
{
	void *hImage;
	void *pEnrollData;
	void *pCtx;
	bmkt_ctx_t *ctx;
	bmkt_sensor_desc_t sensor_desc;
	bmkt_sensor_t *sensor;
	bmkt_usb_config_t *usb_config;
	struct syna_enroll_resp_data enroll_resp_data;
	gboolean isFingerOnSensor;
	syna_state_t state;
	pthread_mutex_t op_mutex;
	pthread_cond_t op_cond;
	int op_finished;
	GSList* file_gslist;
	GSList* sensor_gslist;
}synaptics_dev;

#endif //__synaptics_h__
