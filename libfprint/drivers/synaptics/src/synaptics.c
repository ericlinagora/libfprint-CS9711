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

#define SYNA_ASSERT(_state, _message, _err) ({ \
		if (__builtin_expect(!(_state), 0)) \
		{ \
			fp_err("%s",(_message)); \
			result = (_err); \
			goto cleanup; \
		} \
})

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
	struct libusb_device_descriptor dsc;
	libusb_device *udev = libusb_get_device(fpi_dev_get_usb_dev(dev));

	fp_info("%s ", __func__);

	    /* Set enroll stage number */
	fpi_dev_set_nr_enroll_stages(dev, ENROLL_SAMPLES);

    /* Initialize private structure */
	sdev = g_malloc0(sizeof(synaptics_dev));
	sdev->sensor_desc.xport_type = BMKT_TRANSPORT_TYPE_USB;
	sdev->usb_config = &sdev->sensor_desc.xport_config.usb_config;

	result = libusb_get_device_descriptor(udev, &dsc);
	if(result)
	{
		fp_err("Failed to get device descriptor");
		return -1;
	}
	sdev->usb_config->product_id = dsc.idProduct;
    
	pthread_mutex_init(&sdev->op_mutex, NULL);
	pthread_cond_init(&sdev->op_cond, NULL);

    result = bmkt_init(&(sdev->ctx));
	if (result != BMKT_SUCCESS)
	{
		fp_err("Failed to initialize bmkt context: %d", result);
		return -1;
	}
	fp_info("bmkt_init successfully.");

    result = bmkt_open(sdev->ctx, &sdev->sensor_desc, &sdev->sensor, general_error_callback, NULL);
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
    
	ret = pthread_mutex_destroy(&sdev->op_mutex);
	if (ret)
	{
		fp_err("failed to destroy mutex");
	}

	ret = pthread_cond_destroy(&sdev->op_cond);
	if (ret)
	{
		fp_err("failed to destroy cond ");
	}

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

void get_file_data(struct fp_dev *dev, const char *basepath)
{
	char path[PATH_MAX];
	struct dirent *dp;
	DIR *dir = opendir(basepath);
	synaptics_dev *sdev = FP_INSTANCE_DATA(dev);
	if(!dir)
		return;
	while( (dp = readdir(dir)) != NULL)
	{
		if(strcmp(dp->d_name, ".")!=0 && strcmp(dp->d_name,"..")!=0)
		{
			strcpy(path,basepath);
			strcat(path,"/");
			strcat(path,dp->d_name);
			fp_info("%s type is %d ",dp->d_name,dp->d_type);
			if(dp->d_type == DT_REG)
			{
				gsize length;
				gchar *contents;
				GError *err = NULL;
				struct fp_print_data *fdata;
				fp_info(" load file %s ", path);
				g_file_get_contents(path, &contents, &length, &err);
				if (err) {
					fp_err("load file failed ");
				}
				else
				{
					fdata = fp_print_data_from_data(contents, length);
					struct fp_print_data_item *item = fdata->prints->data;
					struct syna_mis_print_data *print_data =(struct syna_mis_print_data *) item -> data;

					char *data_node=(char *)g_malloc0(strlen(print_data->user_id) + 1);
					memcpy(data_node, print_data->user_id, strlen(print_data->user_id) + 1);
					sdev->file_gslist = g_slist_append(sdev->file_gslist, data_node);
					g_free(contents);
				}
			}
			else
				get_file_data(dev, path);
		}
	}
}

static int get_enrolled_users_resp(bmkt_response_t *resp, void *ctx)
{
	
	bmkt_enroll_templates_resp_t *get_enroll_templates_resp = &resp->response.enroll_templates_resp;
	struct fp_dev *dev=(struct fp_dev *)ctx;
	synaptics_dev *sdev = FP_INSTANCE_DATA(dev);

	switch (resp->response_id)
	{
		case BMKT_RSP_QUERY_FAIL:
			fp_info("Failed to query enrolled users: %d", resp->result);
			pthread_mutex_lock(&sdev->op_mutex);
			sdev->op_finished = 1;
			pthread_cond_signal(&sdev->op_cond);
			pthread_mutex_unlock(&sdev->op_mutex);
			break;
		case BMKT_RSP_QUERY_RESPONSE_COMPLETE:
			pthread_mutex_lock(&sdev->op_mutex);
			sdev->op_finished = 1;
			pthread_cond_signal(&sdev->op_cond);
			pthread_mutex_unlock(&sdev->op_mutex);
			fp_info("Query complete!");
			break;
		case BMKT_RSP_TEMPLATE_RECORDS_REPORT:  

			for (int n = 0; n < BMKT_MAX_NUM_TEMPLATES_INTERNAL_FLASH; n++)
			{
				if (get_enroll_templates_resp->templates[n].user_id_len == 0)
					continue;

				fp_info("![query %d of %d] template %d: status=0x%x, userId=%s, fingerId=%d",
					get_enroll_templates_resp->query_sequence,
					get_enroll_templates_resp->total_query_messages,
					n,
					get_enroll_templates_resp->templates[n].template_status,
					get_enroll_templates_resp->templates[n].user_id,
					get_enroll_templates_resp->templates[n].finger_id);
				char *data_node = (char *)g_malloc0(strlen(get_enroll_templates_resp->templates[n].user_id) + 1);
				memcpy(data_node, get_enroll_templates_resp->templates[n].user_id, 
				strlen(get_enroll_templates_resp->templates[n].user_id) + 1 );
				sdev->sensor_gslist = g_slist_prepend(sdev->sensor_gslist, data_node);
			}
			break;
	}
	
	return 0;
}

void get_sensor_data(struct fp_dev *dev)
{
	synaptics_dev *sdev = FP_INSTANCE_DATA(dev);
	int result;
	sdev->op_finished = 0;
	result = bmkt_get_enrolled_users(sdev->sensor, get_enrolled_users_resp, dev);
	if (result != BMKT_SUCCESS)
	{
		fp_err("Failed to get enrolled users: %d", result);
	}
	else
	{
		fp_info("get enrolled data started.");
	}
	pthread_mutex_lock(&sdev->op_mutex);
	if(sdev->op_finished == 0)
	{
		pthread_cond_wait(&sdev->op_cond, &sdev->op_mutex);
	}
	pthread_mutex_unlock(&sdev->op_mutex);
}

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
			pthread_mutex_lock(&sdev->op_mutex);
			sdev->op_finished = 1;
			pthread_cond_signal(&sdev->op_cond);
			pthread_mutex_unlock(&sdev->op_mutex);
			break;
		case BMKT_RSP_DEL_USER_FP_OK:
			fp_info("Successfully deleted enrolled user");
			pthread_mutex_lock(&sdev->op_mutex);
			sdev->op_finished = 1;
			pthread_cond_signal(&sdev->op_cond);
			pthread_mutex_unlock(&sdev->op_mutex);
			break;
	}
	return 0;
}


const char FPRINTD_DATAPATH[]="/usr/local/var/lib/fprint";
/* 
 * Delete the data which doesn't exist in fprintd folder from sensor database, 
 * otherwise, new finger may have problem to be recognized if it 
 * already exists in sensor.
*/
void sync_database(struct fp_dev *dev)
{
	synaptics_dev *sdev = FP_INSTANCE_DATA(dev);
	int result = 0;
	int sindex, findex;
	GSList* snode;
	GSList* fnode;

	get_file_data(dev, FPRINTD_DATAPATH);
	get_sensor_data(dev);
	
	for(sindex = 0; (snode = g_slist_nth(sdev->sensor_gslist, sindex)); sindex++)
	{
		for(findex = 0; (fnode = g_slist_nth(sdev->file_gslist, findex)); findex++)
		{
			if(strlen(snode->data) == strlen(fnode->data) &&
				strncmp(snode->data, fnode->data, strlen(snode->data)) == 0)
			{
				break;
			}
		}
		if(!fnode)
		{
			sdev->op_finished = 0;
			result = bmkt_delete_enrolled_user(sdev->sensor, 1, snode->data, 
			strlen(snode->data), del_enrolled_user_resp, dev);
			if (result != BMKT_SUCCESS)
			{
				fp_err("Failed to delete enrolled user: %d", result);
			}
			else
			{
				
				pthread_mutex_lock(&sdev->op_mutex);
				if(sdev->op_finished == 0)
				{
					pthread_cond_wait(&sdev->op_cond, &sdev->op_mutex);
				}
				pthread_mutex_unlock(&sdev->op_mutex);

			}
			
		}
	}

	g_slist_free(sdev->file_gslist);
	g_slist_free(sdev->sensor_gslist);
}
static int enroll_start(struct fp_dev *dev)
{
	synaptics_dev *sdev = FP_INSTANCE_DATA(dev);
	int result = 0;
	char userid[TEMPLATE_ID_SIZE + 1];

	sync_database(dev);
	
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
	ret = bmkt_cancel_op(sdev->sensor, cancel_resp, dev);
	if (ret != BMKT_SUCCESS)
	{
		fp_err("Failed to cancel operation: %d", ret);
	}
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
				fpi_drvcb_report_verify_result(dev, FP_VERIFY_NO_MATCH, NULL);
			break;
		}
		case BMKT_RSP_VERIFY_OK:
		{
			fp_info("Verify was successful! for user: %s finger: %d score: %f",
					verify_resp->user_id, verify_resp->finger_id, verify_resp->match_result);
			fpi_drvcb_report_verify_result(dev, FP_VERIFY_MATCH, NULL);
			break;
		}
	}

	return 0;
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
	ret = bmkt_cancel_op(sdev->sensor, cancel_resp, dev);
	if (ret != BMKT_SUCCESS)
	{
		fp_err("Failed to cancel operation: %d", ret);
	}
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
};




