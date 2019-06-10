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

#define FP_COMPONENT "synaptics"

#include "drivers_api.h"
#include "fpi-async.h"
#include "fp_internal.h"

#include "synaptics.h"


static const struct usb_id id_table[] = {
	{ .vendor = SYNAPTICS_VENDOR_ID, .product = SYNAPTICS_PRODUCT_ID_A9,  },

    { 0, 0, 0, }, /* terminating entry */
};


static int general_error_callback(uint16_t error, void *ctx)
{
	fp_err("Received General Error %d from the sensor", error);
	return 0;
}

static int finger_event_callback(bmkt_finger_event_t *event, void *ctx)
{
	struct fp_dev *dev=(struct fp_dev *)ctx;
	synaptics_dev *sdev = FP_INSTANCE_DATA(dev);

	switch (event->finger_state)
	{
		case BMKT_FINGER_STATE_UNKNOWN:
			fp_info("Finger state is not known");
			break;
		case BMKT_FINGER_STATE_ON_SENSOR:
			sdev->isFingerOnSensor = TRUE;
			fp_info("Finger in on the sensor");
			break;
		case BMKT_FINGER_STATE_NOT_ON_SENSOR:
			sdev->isFingerOnSensor = FALSE;
			fp_info("Finger is not on the sensor");
			if(sdev->state == SYNA_STATE_VERIFY_DELAY_RESULT)
			{
				fp_info("verify no match");
				bmkt_op_set_state(sdev->sensor, BMKT_OP_STATE_COMPLETE);
				fpi_drvcb_report_verify_result(dev, FP_VERIFY_NO_MATCH, NULL);
			}
			break;
	}

	return BMKT_SUCCESS;
}

static int cancel_resp(bmkt_response_t *resp, void *ctx)
{

	switch (resp->response_id)
	{
		case BMKT_RSP_CANCEL_OP_OK:
			fp_info("Successfully canceled operation");
			break;
		case BMKT_RSP_CANCEL_OP_FAIL:
			fp_info("Failed to cancel current operation: %d", resp->result);
			break;
	}

	return 0;
}


struct syna_mis_print_data
{
	uint8_t finger_id;
	uint8_t user_id[BMKT_MAX_USER_ID_LEN];
};

static int enroll_response(bmkt_response_t *resp, void *ctx)
{
	bmkt_enroll_resp_t *enroll_resp = &resp->response.enroll_resp;
	struct fp_dev *dev=(struct fp_dev *)ctx;
	synaptics_dev *sdev = FP_INSTANCE_DATA(dev);

	switch (resp->response_id)
	{
		case BMKT_RSP_ENROLL_READY:
		{
			fpi_drvcb_enroll_started(dev, 0);
			sdev->enroll_resp_data.progress = 0;
			fp_info("Place Finger on the Sensor!");
			break;
		}
		case BMKT_RSP_CAPTURE_COMPLETE:
		{
			fp_info("Fingerprint image capture complete!");
			break;
		}
		case BMKT_RSP_ENROLL_REPORT:
		{
			fp_info("Enrollment is %d %% ", enroll_resp->progress);
			if(enroll_resp->progress < 100)
			{
				if(sdev->enroll_resp_data.progress == enroll_resp->progress)
					fpi_drvcb_enroll_stage_completed(dev, FP_ENROLL_RETRY, NULL, NULL);
				else
					fpi_drvcb_enroll_stage_completed(dev, FP_ENROLL_PASS, NULL, NULL);
			}
			sdev->enroll_resp_data.progress = enroll_resp->progress;
			break;
		}
		case BMKT_RSP_ENROLL_PAUSED:
		{
			fp_info("Enrollment has been paused!");
			break;
		}
		case BMKT_RSP_ENROLL_RESUMED:
		{
			fp_info("Enrollment has been resumed!");
			break;
		}
		case BMKT_RSP_ENROLL_FAIL:
		{
			fp_info("Enrollment has failed!: %d", resp->result);
			break;
		}
		case BMKT_RSP_ENROLL_OK:
		{
			struct syna_mis_print_data mis_data;
			struct fp_print_data *fdata = NULL;
			struct fp_print_data_item *item = NULL;
			fdata = fpi_print_data_new(dev);
			item = fpi_print_data_item_new(sizeof(mis_data));
			fp_info("Enrollment was successful!");
			mis_data.finger_id = enroll_resp->finger_id;
			memcpy(mis_data.user_id, enroll_resp->user_id,
				BMKT_MAX_USER_ID_LEN);
			memcpy(item->data, &mis_data,
				sizeof(struct syna_mis_print_data));
			fdata->prints = g_slist_prepend(fdata->prints, item);
			bmkt_op_set_state(sdev->sensor, BMKT_OP_STATE_COMPLETE);
			fpi_drvcb_enroll_stage_completed(dev, 1, fdata, NULL);
			break;
		}
	}
	return 0;
}

static int dev_init(struct fp_dev *dev, unsigned long driver_data)
{
	synaptics_dev *sdev = NULL;
	int result = 0, ret = 0;
	libusb_device *udev = libusb_get_device(fpi_dev_get_usb_dev(dev));

	fp_info("%s ", __func__);

	    /* Set enroll stage number */
	fpi_dev_set_nr_enroll_stages(dev, ENROLL_SAMPLES);

    /* Initialize private structure */
	sdev = g_malloc0(sizeof(synaptics_dev));

	result = bmkt_init(&(sdev->ctx));
	if (result != BMKT_SUCCESS)
	{
		fp_err("Failed to initialize bmkt context: %d", result);
		return -1;
	}
	fp_info("bmkt_init successfully.");

	result = bmkt_open(sdev->ctx, &sdev->sensor, general_error_callback, NULL, fpi_dev_get_usb_dev(dev));
	if (result != BMKT_SUCCESS)
	{
		fp_err("Failed to open bmkt sensor: %d", result);
		goto bmkt_cleanup;
	}

	result = bmkt_register_finger_event_notification(sdev->sensor, finger_event_callback, dev);
	if (result != BMKT_SUCCESS)
	{
		fp_err("Failed to register finger event notification: %d", result);
		goto bmkt_cleanup;
	}
	result = bmkt_init_fps(sdev->sensor);
	if (result == BMKT_SUCCESS)
	{
		fp_info("Successfully initialized the FPS");
	}
	else if (result == BMKT_OPERATION_DENIED)
	{
		/* sensor already intialized...allow operations to continue */
		fp_info("FPS already initialized");
		result = BMKT_SUCCESS;
	}
	else
	{
		fp_err("Failed to initialize the FPS: %d", result);
		goto bmkt_cleanup;
	}

	fp_dev_set_instance_data(dev, sdev);
	/* Notify open complete */
	fpi_drvcb_open_complete(dev, 0);
	return result;

bmkt_cleanup:
	ret = bmkt_close(sdev->sensor);
	if (ret != BMKT_SUCCESS)
	{
		fp_err("Failed to close bmkt sensor: %d", ret);
		goto cleanup;
	}
	bmkt_exit(sdev->ctx);
	g_free(sdev);

cleanup:
	fpi_drvcb_open_complete(dev, 1);
	return result;
}
static void dev_exit(struct fp_dev *dev)
{
	int ret = 0;
	synaptics_dev *sdev = FP_INSTANCE_DATA(dev);
	ret = bmkt_close(sdev->sensor);
	if (ret != BMKT_SUCCESS)
	{
		fp_err("Failed to close bmkt sensor: %d", ret);
		return;
	}

	bmkt_exit(sdev->ctx);

	g_free(sdev);
	fpi_drvcb_close_complete(dev);
}

static gboolean rand_string(char *str, size_t size)
{
    const char charset[] = "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
	srand(time(NULL));
    if (size) {
        --size;
        for (size_t n = 0; n < size; n++) {
            int key = rand() % (int) (sizeof charset - 1);
            str[n] = charset[key];
        }
        str[size] = '\0';
    }
	else
		return FALSE;
    return TRUE;
}

#define TEMPLATE_ID_SIZE 20

static int del_enrolled_user_resp(bmkt_response_t *resp, void *ctx)
{
	bmkt_del_user_resp_t *del_user_resp = &resp->response.del_user_resp;
	struct fp_dev *dev=(struct fp_dev *)ctx;
	synaptics_dev *sdev = FP_INSTANCE_DATA(dev);

	switch (resp->response_id)
	{
		case BMKT_RSP_DELETE_PROGRESS:
			fp_info("Deleting Enrolled Users is %d%% complete",
				del_user_resp->progress);
			break;
		case BMKT_RSP_DEL_USER_FP_FAIL:
			fp_info("Failed to delete enrolled user: %d", resp->result);
			bmkt_op_set_state(sdev->sensor, BMKT_OP_STATE_COMPLETE);
			if(sdev->state == SYNA_STATE_DELETE)
			{
				/* Return result complete when record doesn't exist, otherwise host data
				won't be deleted. */
				if(resp->result == BMKT_FP_DATABASE_NO_RECORD_EXISTS)
					fpi_drvcb_delete_complete(dev, FP_DELETE_COMPLETE);
				else
					fpi_drvcb_delete_complete(dev, FP_DELETE_FAIL);
			}
			break;
		case BMKT_RSP_DEL_USER_FP_OK:
			fp_info("Successfully deleted enrolled user");
			bmkt_op_set_state(sdev->sensor, BMKT_OP_STATE_COMPLETE);
			if(sdev->state == SYNA_STATE_DELETE)
			{
				fpi_drvcb_delete_complete(dev, FP_DELETE_COMPLETE);
			}
			break;
	}
	return 0;
}


static int enroll_start(struct fp_dev *dev)
{
	synaptics_dev *sdev = FP_INSTANCE_DATA(dev);
	int result = 0;
	char userid[TEMPLATE_ID_SIZE + 1];
	
	fp_info("enroll_start");


	rand_string(userid, TEMPLATE_ID_SIZE);

	int useridlength =0;
	int finger_id;

	finger_id = 1;
	useridlength = strlen(userid);

	sdev->state = SYNA_STATE_ENROLL;

	result = bmkt_enroll(sdev->sensor, userid, useridlength,
				finger_id, enroll_response, dev);
	if (result)
	{
		fp_err("Failed to enroll finger: %d", result);
	}

	return 0;
}


static int enroll_stop(struct fp_dev *dev)
{
	fp_info("syna enroll stop");
	int ret;
	synaptics_dev *sdev = FP_INSTANCE_DATA(dev);
	sdev->state = SYNA_STATE_IDLE;
	fpi_drvcb_enroll_stopped(dev);
	return 1;
}

static int verify_response(bmkt_response_t *resp, void *ctx)
{
	bmkt_verify_resp_t *verify_resp = &resp->response.verify_resp;
	struct fp_dev *dev=(struct fp_dev *)ctx;
	synaptics_dev *sdev = FP_INSTANCE_DATA(dev);

	switch (resp->response_id)
	{
		case BMKT_RSP_VERIFY_READY:
		{
			fp_info("Place Finger on the Sensor!");
			fpi_drvcb_verify_started(dev, 0);
			break;
		}
		case BMKT_RSP_CAPTURE_COMPLETE:
		{
			fp_info("Fingerprint image capture complete!");
			break;
		}
		case BMKT_RSP_VERIFY_FAIL:
		{
			fp_err("Verify has failed!: %d", resp->result);
			if(resp->result == BMKT_SENSOR_STIMULUS_ERROR || resp->result == BMKT_FP_NO_MATCH)
			{
				sdev->state = SYNA_STATE_VERIFY_DELAY_RESULT;
			}
			else
			{
				bmkt_op_set_state(sdev->sensor, BMKT_OP_STATE_COMPLETE);
				fpi_drvcb_report_verify_result(dev, FP_VERIFY_NO_MATCH, NULL);
			}
			break;
		}
		case BMKT_RSP_VERIFY_OK:
		{
			fp_info("Verify was successful! for user: %s finger: %d score: %f",
					verify_resp->user_id, verify_resp->finger_id, verify_resp->match_result);
			bmkt_op_set_state(sdev->sensor, BMKT_OP_STATE_COMPLETE);
			fpi_drvcb_report_verify_result(dev, FP_VERIFY_MATCH, NULL);
			break;
		}
	}

	return 0;
}
static int delete_finger(struct fp_dev *dev)
{
	synaptics_dev *sdev = FP_INSTANCE_DATA(dev);
	int result = 0;
	struct fp_print_data *print = fpi_dev_get_delete_data(dev);;
	struct fp_print_data_item *item = print->prints->data;
	struct syna_mis_print_data *print_data;
	bmkt_user_id_t user;

	if(item->length != sizeof(struct syna_mis_print_data))
	{
		fp_err("print data is incorrect !");
		goto cleanup;
	}

	print_data = (struct syna_mis_print_data *)item->data;

	memset(&user, 0, sizeof(bmkt_user_id_t));
	memcpy(user.user_id, print_data->user_id, sizeof(print_data->user_id));

	fp_info("delete finger !");

	user.user_id_len = strlen(user.user_id);
	if (user.user_id_len <= 0 || user.user_id[0] == ' ')
	{
		fp_err("Invalid user name.");
		goto cleanup;
	}

	sdev->state = SYNA_STATE_DELETE;
	result = bmkt_delete_enrolled_user(sdev->sensor, 1, print_data->user_id, 
	user.user_id_len, del_enrolled_user_resp, dev);
	if (result != BMKT_SUCCESS)
	{
		fp_err("Failed to delete enrolled user: %d", result);
		goto cleanup;
	}

	return 0;

cleanup:
	return -1;
}
static int verify_start(struct fp_dev *dev)
{
	synaptics_dev *sdev = FP_INSTANCE_DATA(dev);
	int result = 0;
	struct fp_print_data *print = fpi_dev_get_verify_data(dev);;
	struct fp_print_data_item *item = print->prints->data;
	struct syna_mis_print_data *print_data;
	bmkt_user_id_t user;

	if(item->length != sizeof(struct syna_mis_print_data))
	{
		fp_err("print data is incorrect !");
		goto cleanup;
	}

	print_data = (struct syna_mis_print_data *)item->data;

	memset(&user, 0, sizeof(bmkt_user_id_t));
	memcpy(user.user_id, print_data->user_id, sizeof(print_data->user_id));

	fp_info("syna verify_start !");

	user.user_id_len = strlen(user.user_id);
	if (user.user_id_len <= 0 || user.user_id[0] == ' ')
	{
		fp_err("Invalid user name.");
		goto cleanup;
	}

	sdev->state = SYNA_STATE_VERIFY;
	result = bmkt_verify(sdev->sensor, &user, verify_response, dev);
	if (result != BMKT_SUCCESS)
	{
		fp_err("Failed to verify finger: %d", result);
	}

	return 0;

cleanup:
	fpi_drvcb_verify_started(dev, 1);
	return -1;
}

static int verify_stop(struct fp_dev *dev, gboolean iterating)
{
	fp_info("syna verify_stop");
	int ret;
	synaptics_dev *sdev = FP_INSTANCE_DATA(dev);
	sdev->state = SYNA_STATE_IDLE;
	fpi_drvcb_verify_stopped(dev);
	return 0;
}

struct fp_driver synaptics_driver = {
	.id = SYNAPTICS_ID,
	.name = FP_COMPONENT,
	.full_name = SYNAPTICS_DRIVER_FULLNAME,
	.id_table = id_table,
	.scan_type = FP_SCAN_TYPE_PRESS,
	.open = dev_init,
	.close = dev_exit,
	.enroll_start = enroll_start,
	.enroll_stop = enroll_stop,
	.verify_start = verify_start,
	.verify_stop = verify_stop,
	.delete_finger = delete_finger,
};




