/*
 * Synaptics MiS Fingerprint Sensor Interface
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

#ifndef _BMKT_H_
#define _BMKT_H_

/**< User ID maximum length allowed */
#define BMKT_MAX_USER_ID_LEN                    100
/**< Software Part Number length */
#define BMKT_PART_NUM_LEN                       10
/**< Software supplier identification length */
#define BMKT_SUPPLIER_ID_LEN                    2

/**< Maximum namber of templates for storing in internal flash of the fingerprint sensor */
#define BMKT_MAX_NUM_TEMPLATES_INTERNAL_FLASH   15

#include <stdint.h>
#include "bmkt_response.h"

/*!
*******************************************************************************
**  Type definition for result
*/
/** No error; Operation successfully completed. */
#define BMKT_SUCCESS                            0
/** Fingerprint system not initialized */
#define BMKT_FP_SYSTEM_NOT_INITIALIZED          101
/** Fingerprint system busy performing another operation */
#define BMKT_FP_SYSTEM_BUSY                     102
/** Operation not allowed */
#define BMKT_OPERATION_DENIED                   103
/** System ran out of memory while performing operation */
#define BMKT_OUT_OF_MEMORY                      104
/** Corrupt message, CRC check fail or truncated message */
#define BMKT_CORRUPT_MESSAGE                    110
/** One of the command parameters is outside the range of valid values */
#define BMKT_INVALID_PARAM                      111
/** Unrecognized message or message with invalid message ID */
#define BMKT_UNRECOGNIZED_MESSAGE               112
/** Operation time out */
#define BMKT_OP_TIME_OUT                        113
/** General error – cause of error cannot be determined */
#define BMKT_GENERAL_ERROR                      114

#define BMKT_SET_SECURITY_LEVEL_FAIL            120
#define BMKT_GET_SECURITY_LEVEL_FAIL            121

/** Fingerprint sensor reset while operation was being performed */
#define BMKT_SENSOR_RESET                       201
/** Fingerprint sensor malfunctioned */
#define BMKT_SENSOR_MALFUNCTION                 202
/** Fingerprint sensor cannot be accessed despite repeated attempts */
#define BMKT_SENSOR_TAMPERED                    203
/**
* BMKT_SENSOR_NOT_INIT:
* Fingerprint sensor module not initialized yet – not ready for use
* (different from error code 101 which indicates that the entire system
* has not been initialized)
*/
#define BMKT_SENSOR_NOT_INIT                    204
/** Number of re-pairing operations exceeded limit or re-pairing has been disabled */
#define BMKT_OWNERSHIP_RESET_MAX_EXCEEDED       205
/**
* BMKT_SENSOR_STIMULUS_ERROR:
* There is a finger or debris on the sensor that needs to be removed
* before issuing this command
*/
#define BMKT_SENSOR_STIMULUS_ERROR              213
/**
* BMKT_CORRUPT_TEMPLATE_DATA:
* One of the fingerprint templates stored on flash is corrupt.
* This error code is returned in case of failure in finding a fingerprint match
* during identify or verify operations while also detecting that one or more
* fingerprint templates stored on the flash has become corrupted
*/
#define BMKT_CORRUPT_TEMPLATE_DATA              300
/** Failed to extract features from fingerprint image acquired by sensor */
#define BMKT_FEATURE_EXTRACT_FAIL               301
/** Failed to generate fingerprint template */
#define BMKT_ENROLL_FAIL                        302
/** Specified finger already enrolled for this user */
#define BMKT_ENROLLMENT_EXISTS                  303
/** Invalid fingerprint image */
#define BMKT_INVALID_FP_IMAGE                   304
/** No matching user fingerprint template found in database */
#define BMKT_FP_NO_MATCH                        404
/** Fingerprint database is full */
#define BMKT_FP_DATABASE_FULL                   501
/** Fingerprint database is empty */
#define BMKT_FP_DATABASE_EMPTY                  502
/** Cannot access fingerprint database */
#define BMKT_FP_DATABASE_ACCESS_FAIL            503
/** Fingerprint template record does not exist */
#define BMKT_FP_DATABASE_NO_RECORD_EXISTS       504
/** Failed to read/write system parameters stored on flash */
#define BMKT_FP_PARAM_ACCESS_FAIL               505
/** Fingerprint is a spoof */
#define BMKT_FP_SPOOF_ALERT                     801
/** Anti-spoof module failure */
#define BMKT_ANTI_SPOOF_MODULE_FAIL             802

#define BMKT_CORRUPT_UPDATE_IMAGE               901
#define BMKT_SYSTEM_UPDATE_FAIL                 902

#define BMKT_EVENT_NOT_SET                      1000
#define BMKT_SENSOR_NOT_READY                   1001
#define BMKT_TIMEOUT                            1002
#define BMKT_SENSOR_RESPONSE_PENDING            1003

#define BMKT_SENSOR_FLAGS_POLLING                0x1

#ifdef __cplusplus
extern "C" {
#endif

/**
* bmkt_mode:
* Fingerprint system operational mode values level 1
*/
typedef enum bmkt_mode
{
    BMKT_STATE_UNINIT    = 0xFF,
    BMKT_STATE_IDLE      = 0x00,
    BMKT_STATE_ENROLL    = 0x10,
    BMKT_STATE_IDENTIFY  = 0x20,
    BMKT_STATE_VERIFY    = 0x30,
    BMKT_STATE_DB_OPS    = 0x40,
    BMKT_STATE_SYS_TEST  = 0x50,
    BMKT_STATE_SYS_OPS   = 0x60,
} bmkt_mode_t;

/**
* bmkt_mode_level2:
* Fingerprint system operational mode values level 2
*/
typedef enum bmkt_mode_level2
{
    BMKT_STATE_L2_IDLE                  = 0x00,
    BMKT_STATE_L2_STARTING              = 0x11,
    BMKT_STATE_L2_WAITING_FOR_FINGER    = 0x12,
    BMKT_STATE_L2_CAPTURE_IMAGE         = 0x13,
    BMKT_STATE_L2_CAPTURE_COMPLETE      = 0x14,
    BMKT_STATE_L2_EXTRACT_FEATURE       = 0x15,
    BMKT_STATE_L2_CREATE_TEMPLATE       = 0x16,
    BMKT_STATE_L2_READING_FROM_FLASH    = 0x17,
    BMKT_STATE_L2_WRITING_TO_FLASH      = 0x18,
    BMKT_STATE_L2_FINISHING             = 0x19,
    BMKT_STATE_L2_CANCELING_OP          = 0x20,
    BMKT_STATE_L2_MATCHING              = 0x21,
    BMKT_STATE_L2_TRANSMITTING_RESPONSE = 0x22,
    BMKT_STATE_L2_READY_POWER_DOWN      = 0xF0,
} bmkt_mode_level2_t;

/**
* bmkt_transport_type:
* Fingerprint system transport types
*/
typedef enum bmkt_transport_type
{
    BMKT_TRANSPORT_TYPE_USB = 0,
} bmkt_transport_type_t;

/**
* bmkt_usb_config:
* Structure represcontainingenting USB configuration details
*/
typedef struct bmkt_usb_config
{
    int product_id; /**< USB device product ID */
} bmkt_usb_config_t;

/**
* bmkt_transport_config_t:
* Union containing transport configuration details
*/
typedef union
{
    bmkt_usb_config_t usb_config;
} bmkt_transport_config_t;

/**
* bmkt_sensor_desc_t: 
* Structure containing fingerprint system description
*/
typedef struct bmkt_sensor_desc
{
    bmkt_transport_type_t xport_type;       /**< Transport type */
    bmkt_transport_config_t xport_config;   /**< Transport configuration */
    int flags;
} bmkt_sensor_desc_t;

/**
* bmkt_finger_state_t:
* Finger state representation values.
*/
typedef enum
{
    BMKT_FINGER_STATE_UNKNOWN    = 0,
    BMKT_FINGER_STATE_ON_SENSOR,
    BMKT_FINGER_STATE_NOT_ON_SENSOR,
} bmkt_finger_state_t;

/**
* bmkt_finger_event_t:
* Structure containing finger state
*/
typedef struct bmkt_finger_event
{
    bmkt_finger_state_t finger_state;
} bmkt_finger_event_t;

typedef struct bmkt_user_id
{
    uint8_t      user_id_len;
    uint8_t      user_id[BMKT_MAX_USER_ID_LEN];
} bmkt_user_id_t;

typedef struct bmkt_ctx bmkt_ctx_t;
typedef struct bmkt_sensor bmkt_sensor_t;
typedef struct bmkt_sensor_desc bmkt_sensor_desc_t;
typedef struct bmkt_event bmkt_event_t;

typedef int (*bmkt_resp_cb_t)(bmkt_response_t *resp, void *cb_ctx);
typedef int (*bmkt_event_cb_t)(bmkt_finger_event_t *event, void *cb_ctx);
typedef int (*bmkt_general_error_cb_t)(uint16_t error, void *cb_ctx);

/**
* bmkt_init:
* @brief      Initialize the bmkt library.
*
* @param[out] ctx  A double pointer to return the created library module context pointer.
*
* @return     BMKT_SUCCESS
*             BMKT_INVALID_PARAM
*
* The bmkt_init function must be invoked to intialize the bmkt library before calling other functions. 
* The library module context pointer is returned, which must be passed to all other library interface functions.
*/
int
bmkt_init(
    bmkt_ctx_t **   ctx);

/**
* bmkt_exit:
* @brief      Uninitialize the bmkt library.
*
* @param[in]  ctx Context pointer created by bmkt_init.
*
* @return     none
*
* The bmkt_exit function must be invoked when the module is no longer needed.
*/

void
bmkt_exit(
    bmkt_ctx_t *    ctx);

/**
* bmkt_open:
* @brief      Open the specified sensor module.
*
* @param[in]  ctx           Context pointer created by bmkt_init.
* @param[in]  desc          The specified sensor description.
* @param[out] sensor        A double pointer to return the created sensor module pointer
* @param[in]  err_cb        General Error callback function
* @param[in]  err_cb_ctx    General Error callback user context
*
* @return     VCS_RESULT_OK if success
*
* The bmkt_open function must be called to open a specific sensor module. Returned sensor module pointer
* must be passed to all other interface functions that expect a sensor pointer.
*/

int
bmkt_open(
    bmkt_ctx_t *                ctx, 
    const bmkt_sensor_desc_t *  desc, 
    bmkt_sensor_t **            sensor,
    bmkt_general_error_cb_t     err_cb, 
    void *                      err_cb_ctx);

/**
* bmkt_close:
* @brief      Close the specified sensor module, and release all the resources
*
* @param[in]  sensor  The sensor module pointer
*
* @return     VCS_RESULT_OK if success
*
* The bmkt_close function must be invoked when the sensor module is no longer needed.
*/
int
bmkt_close(
    bmkt_sensor_t *     sensor);

/**
* bmkt_init_fps:
* @brief      Initialize the sensor module.
*
* @param[in]  sensor  The sensor module pointer
*
* @return     VCS_RESULT_OK if success
*
* Initializes the fingerprint sensor module. Must be the first command to be issued to the fingerprint sensor module, before any other commands are issued.
*/
int
bmkt_init_fps(
    bmkt_sensor_t *     sensor);

/**
* bmkt_enroll:
* @brief      Put the fingerprint sensor module into enrollment mode to Enroll a user’s fingerprint into the system.
*
* @param[in]  sensor        The sensor module pointer
* @param[in]  user_id       Enrolled User ID
* @param[in]  user_id_len   Enrolled User ID lenght
* @param[in]  finger_id     Enrolled finger ID
* @param[in]  resp_cb       Responce callback function. Available responses:
*                           - BMKT_RSP_ENROLL_READY
*                           - BMKT_RSP_CAPTURE_COMPLETE
*                           - BMKT_RSP_ENROLL_REPORT
*                           - BMKT_RSP_ENROLL_PAUSED
*                           - BMKT_RSP_ENROLL_RESUMED
*                           - BMKT_RSP_ENROLL_FAIL
*                           - BMKT_RSP_ENROLL_OK

* @param[in]  cb_ctx        Responce callback user context
*
* @return     VCS_RESULT_OK if success
*
* Enrolled users have to touch the fingerprint sensor multiple times based on cues provided by the system.
* After successful enrollment, a template is generated from features of the user’s fingerprint and stored
* in encrypted storage within the fingerprint sensor module.
* When this command is being executed, fingerprint sensor module’s mode is: Enrollment
*/
int
bmkt_enroll(
    bmkt_sensor_t *     sensor, 
    const uint8_t *     user_id, 
    uint32_t            user_id_len,
    uint8_t             finger_id, 
    bmkt_resp_cb_t      resp_cb, 
    void *              cb_ctx);

/**
* bmkt_enroll_pause:
* @brief      Pause enrollment operation.
*
* @param[in]  sensor    The sensor module pointer
*
* @return     VCS_RESULT_OK if success
*
* Enroll pause operation is applicable if fingerprint sensor is in Enrollment mode.
*/
int
bmkt_enroll_pause(
    bmkt_sensor_t *     sensor);

/**
* bmkt_enroll_resume: 
* @brief      Resume enrollment operation.
*
* @param[in]  sensor    The sensor module pointer
*
* @return     VCS_RESULT_OK if success
*
* Enroll resume operation is applicable if fingerprint sensor enrollment is paused.
* Otherwise it has no effect.
*/
int
bmkt_enroll_resume(
    bmkt_sensor_t *     sensor);

/**
* bmkt_identify:
* @brief      Put the fingerprint sensor module into identification mode.
*
* @param[in]  sensor        The sensor module pointer
* @param[in]  resp_cb       Responce callback function.Available responses:
*                           - BMKT_RSP_CAPTURE_COMPLETE
*                           - BMKT_RSP_ID_READY
*                           - BMKT_RSP_ID_FAIL
*                           - BMKT_RSP_ID_OK
* @param[in]  cb_ctx        Responce callback user context
*
* @return     VCS_RESULT_OK if success
*
* The user being identified has to touch the fingerprint sensor once based on a cue provided by the system.
* If a user cannot be identified based on the stored templates of enrolled users or if the fingerprint sensor
* module detects that the fingerprint being presented to the sensor is a spoof, then an error response is generated.
* The order of matching is such that the enrolled template with the most recent successful match is matched against 
* first and then the remaining templates are matched in the order of storage.
* When this command is being executed, fingerprint sensor module’s mode is: Identification
*/
int
bmkt_identify(
    bmkt_sensor_t *     sensor,
    bmkt_resp_cb_t      resp_cb,
    void *              cb_ctx);

/**
* bmkt_verify:
* @brief      Put the fingerprint sensor module into verification mode.
*
* @param[in]  sensor        The sensor module pointer
* @param[in]  user          Enrolled User
* @param[in]  resp_cb       Responce callback function. Available responses:
*                           - BMKT_RSP_CAPTURE_COMPLETE
*                           - BMKT_RSP_VERIFY_READY
*                           - BMKT_RSP_VERIFY_FAIL
*                           - BMKT_RSP_VERIFY_OK
* @param[in]  cb_ctx        Responce callback user context
*
* @return     VCS_RESULT_OK if success
*
* The user being verifyed has to touch the fingerprint sensor once based on a cue provided by the system.
* The Captured fingerprint is matched only against the stored templates corresponding to specifyed user ID,
* If a user’s fingerprint cannot be matched to any of the stored fingerprint templates of the specified user or
* if the fingerprint sensor module detects that the fingerprint being presented to the sensor is a spoof, 
* then an error response is generated.
* When this command is being executed, fingerprint sensor module’s mode is: Verification
*/
int
bmkt_verify(
    bmkt_sensor_t *     sensor,
    bmkt_user_id_t*     user,
    bmkt_resp_cb_t      resp_cb,
    void *              cb_ctx);

/**
* bmkt_get_security_level:
* @brief      Get security level of the fingerprint sensor module.
*
* @param[in]  sensor    The sensor module pointer
* @param[in]  resp_cb   Responce callback function. Available responses:
*                           - BMKT_RSP_GET_SECURITY_LEVEL_REPORT
*                           - BMKT_RSP_GET_SECURITY_LEVEL_FAIL
* @param[in]  cb_ctx    Responce callback user context
*
* @return     VCS_RESULT_OK if success
*
* Security levels affects the False Accept Rate(FAR) and the corresponding False Reject Rate(FRR).
* The higher the security level, the lower the FAR and higher the FRR.
* When this command is being executed, fingerprint sensor module’s mode is: Miscellaneous system operations
*/
int
bmkt_get_security_level(
    bmkt_sensor_t *     sensor,
    bmkt_resp_cb_t      resp_cb,
    void *              cb_ctx);

/**
* bmkt_set_security_level:
* @brief      Set security level of the fingerprint sensor module.
*
* @param[in]  sensor    The sensor module pointer
* @param[in]  level     The security level to be set
* @param[in]  resp_cb   Responce callback function. Available responses:
*                           - BMKT_RSP_SET_SECURITY_LEVEL_REPORT
*                           - BMKT_RSP_SET_SECURITY_LEVEL_FAIL
* @param[in]  cb_ctx    Responce callback user context
*
* @return     VCS_RESULT_OK if success
*
* Security levels affects the False Accept Rate(FAR) and the corresponding False Reject Rate(FRR).
* The higher the security level, the lower the FAR and higher the FRR.
* When this command is being executed, fingerprint sensor module’s mode is: Miscellaneous system operations
*/
int
bmkt_set_security_level(
    bmkt_sensor_t *     sensor,
    bmkt_sec_level_t    level,
    bmkt_resp_cb_t      resp_cb,
    void *              cb_ctx);

/**
* bmkt_get_version:
* @brief      Get system version information from the fingerprint sensor module.
*
* @param[in]  sensor    The sensor module pointer
* @param[in]  resp_cb   Responce callback function. Available responses:
*                           - BMKT_RSP_VERSION_INFO
*                           - BMKT_RSP_GET_VERSION_FAIL
* @param[in]  cb_ctx    Responce callback user context
*
* @return     VCS_RESULT_OK if success
*
* When this command is being executed, fingerprint sensor module’s mode is: Miscellaneous system operations
*/
int
bmkt_get_version(
    bmkt_sensor_t *     sensor,
    bmkt_resp_cb_t      resp_cb,
    void *              cb_ctx);

/**
* bmkt_get_enrolled_users:
* @brief      Get list of enrolled fingerprint template records from the template database. 
*
* @param[in]  sensor    The sensor module pointer
* @param[in]  resp_cb   Responce callback function. Available responses:
*                           - BMKT_RSP_TEMPLATE_RECORDS_REPORT
*                           - BMKT_RSP_QUERY_RESPONSE_COMPLETE
*                           - BMKT_RSP_QUERY_FAIL
* @param[in]  cb_ctx    Responce callback user context
*
* @return     VCS_RESULT_OK if success
*
* The total length of a single response message is limited to 255 bytes hence, depending on how many enrolled fingerprint templates are present in the template database, 
* it may be necessary to split the list of template records of enrolled fingers into multiple messages. 
* The bmkt_get_next_enrolled_users function should be called sequentially until one of conditions are met:
* - Responce is obtained with BMKT_RSP_QUERY_RESPONSE_COMPLETE response id.
* - The response's "query_sequence" field value is equal to "total_query_messages" value.
* When this command is being executed, fingerprint sensor module’s mode is: Database operations
*/
int
bmkt_get_enrolled_users(
    bmkt_sensor_t *     sensor,
    bmkt_resp_cb_t      resp_cb,
    void *              cb_ctx);

/**
* bmkt_get_next_enrolled_users:
* @brief      Continue to get list of enrolled fingerprint template records from the template database.
*
* @param[in]  sensor    The sensor module pointer
* @param[in]  resp_cb   Responce callback function. Available responses:
*                           - BMKT_RSP_TEMPLATE_RECORDS_REPORT
*                           - BMKT_RSP_QUERY_RESPONSE_COMPLETE
*                           - BMKT_RSP_QUERY_FAIL
* @param[in]  cb_ctx    Responce callback user context
*
* @return     VCS_RESULT_OK if success
*
* The total length of a single response message is limited to 255 bytes hence, depending on how many enrolled fingerprint templates are present in the template database,
* it may be necessary to split the list of template records of enrolled fingers into multiple messages.
* The bmkt_get_next_enrolled_users function should be called sequentially until one of conditions are met:
* - Responce is obtained with BMKT_RSP_QUERY_RESPONSE_COMPLETE response id.
* - The response's "query_sequence" field value is equal to "total_query_messages" value.
* When this command is being executed, fingerprint sensor module’s mode is: Database operations
*/
int
bmkt_get_next_enrolled_users(
    bmkt_sensor_t *     sensor,
    bmkt_resp_cb_t      resp_cb,
    void *              cb_ctx);

/**
* bmkt_get_enrolled_fingers:
* @brief      Get list of enrolled fingers for a specific user from the fingerprint template database.
*
* @param[in]  sensor        The sensor module pointer
* @param[in]  user_id       Enrolled User ID
* @param[in]  user_id_len   Enrolled User ID lenght
* @param[in]  resp_cb       Responce callback function. Available responses:
*                           - BMKT_RSP_GET_ENROLLED_FINGERS_REPORT
*                           - BMKT_RSP_QUERY_FAIL
* @param[in]  cb_ctx        Responce callback user context
*
* @return     VCS_RESULT_OK if success
*
* When this command is being executed, fingerprint sensor module’s mode is: Database operations
*/
int
bmkt_get_enrolled_fingers(
    bmkt_sensor_t *     sensor,
    const char *        user_id,
    uint32_t            user_id_len,
    bmkt_resp_cb_t      resp_cb,
    void *              cb_ctx);

/**
* bmkt_delete_all_enrolled_users:
* @brief      Delete entire fingerprint template database.
*
* @param[in]  sensor    The sensor module pointer
* @param[in]  resp_cb   Responce callback function. Available responses:
*                       - BMKT_RSP_DELETE_PROGRESS
*                       - BMKT_RSP_DEL_FULL_DB_FAIL
*                       - BMKT_RSP_DEL_FULL_DB_OK
* @param[in]  cb_ctx    Responce callback user context
*
* @return     VCS_RESULT_OK if success
*
* When this command is being executed, fingerprint sensor module’s mode is: Database operations
*/
int
bmkt_delete_all_enrolled_users(
    bmkt_sensor_t *     sensor,
    bmkt_resp_cb_t      resp_cb,
    void *              cb_ctx);

/**
* bmkt_delete_enrolled_user:
* @brief      Delete a specific fingerprint template of an enrolled user from the database.
*
* @param[in]  sensor        The sensor module pointer
* @param[in]  finger_id     Finger ID to be deleted 
* @param[in]  user_id       User ID to be deleted
* @param[in]  user_id_len   User ID lenght
* @param[in]  resp_cb       Responce callback function. Available responses:
*                           - BMKT_RSP_DEL_USER_FP_FAIL
*                           - BMKT_RSP_DEL_USER_FP_OK
* @param[in]  cb_ctx        Responce callback user context
*
* @return     VCS_RESULT_OK if success
*
* If the value of finger ID is set equal to 0 then all fingerprints of that user will be deleted from the database.
* If the value of user ID is set to an empty string (string with length 0) and the finger ID is set equal to 0 then 
* all templates stored in the fingerprint database which are marked as corrupt will be deleted.
* When this command is being executed, fingerprint sensor module’s mode is: Database operations
*/
int
bmkt_delete_enrolled_user(
    bmkt_sensor_t *     sensor,
    uint8_t             finger_id,
    const char *        user_id,
    uint32_t            user_id_len,
    bmkt_resp_cb_t      resp_cb,
    void *              cb_ctx);

/**
* bmkt_db_capacity:
* @brief      Get information about storage capacity of fingerprint template database.
*
* @param[in]  sensor    The sensor module pointer
* @param[in]  resp_cb   Responce callback function. Available responses:
*                       - BMKT_RSP_QUERY_FAIL
*                       - BMKT_RSP_DATABASE_CAPACITY_REPORT
* @param[in]  cb_ctx    Responce callback user context
*
* @return     VCS_RESULT_OK if success
*
* When this command is executed, the fingerprint sensor module also checks the fingerprint template database for corrupt records
* When this command is being executed, fingerprint sensor module’s mode is: Database operations
*/
int
bmkt_db_capacity(
    bmkt_sensor_t *     sensor,
    bmkt_resp_cb_t      resp_cb,
    void *              cb_ctx);

/**
* bmkt_get_fps_mode:
* @brief      Get current operational mode of the fingerprint sensor module.
*
* @param[in]  sensor    The sensor module pointer
* @param[in]  resp_cb   Responce callback function. Available responses:
*                       - BMKT_RSP_FPS_MODE_FAIL
*                       - BMKT_RSP_FPS_MODE_REPORT
* @param[in]  cb_ctx    Responce callback user context
*
* @return     VCS_RESULT_OK if success
*
* When this command is being executed, it has no effect on the mode on the fingerprint sensor module.
*/
int
bmkt_get_fps_mode(
    bmkt_sensor_t *     sensor,
    bmkt_resp_cb_t      resp_cb,
    void *              cb_ctx);

/**
* bmkt_cancel_op:
* @brief      Cancelling the current operation and returning the fingerprint sensor module to idle mode.
*
* @param[in]  sensor    The sensor module pointer
* @param[in]  resp_cb   Responce callback function. Available responses:
*                       - BMKT_RSP_CANCEL_OP_FAIL
*                       - BMKT_RSP_CANCEL_OP_OK
* @param[in]  cb_ctx    Responce callback user context
*
* @return     VCS_RESULT_OK if success
*
* After initialization, the fingerprint sensor module can return to idle mode in only one of two ways :
* (1) due to completion of current operation;
* (2) due to cancellation of current operation.
* When this command is being executed, fingerprint sensor module’s mode is: Miscellaneous system operations
*/
int
bmkt_cancel_op(
    bmkt_sensor_t *     sensor,
    bmkt_resp_cb_t      resp_cb,
    void *              cb_ctx);

/**
* bmkt_register_finger_event_notification:
* @brief      Register finger presence event callback function
*
* @param[in]  sensor    The sensor module pointer
* @param[in]  cb        Event callback function
* @param[in]  cb_ctx    Event callback user context
*
* @return     VCS_RESULT_OK if success
*
* The registered callback function will be called whenever a finger is detected as being placed on the sensor or removed from the sensor.
*/
int
bmkt_register_finger_event_notification(
    bmkt_sensor_t *     sensor, 
    bmkt_event_cb_t     cb, 
    void *              cb_ctx);

/**
* bmkt_notify_shutdown:
* @brief      Notify the fingerprint sensor module that a power shutdown is imminent.
*
* @param[in]  sensor    The sensor module pointer
* @param[in]  resp_cb   Responce callback function. Available responses: 
*                       - BMKT_RSP_POWER_DOWN_READY 
*                       - BMKT_RSP_POWER_DOWN_FAIL
* @param[in]  cb_ctx    Responce callback user context
*
* @return     VCS_RESULT_OK if success
*
* This will enable the fingerprint sensor module to complete all pending tasks such as writes to the flash memory and respond back to the 
* caller when it is ready to be powered down.
* When this command is being executed, fingerprint sensor module’s mode is: Miscellaneous system operations
*/
int
bmkt_notify_shutdown(
    bmkt_sensor_t *     sensor,
    bmkt_resp_cb_t      resp_cb,
    void *              cb_ctx);

/**
* bmkt_get_status:
* @brief      Get Status of the fingerprint sensor module (is it functioning properly or not).
*
* @param[in]  sensor    The sensor module pointer
* @param[in]  resp_cb   Responce callback function. Available responses:
*                       - BMKT_RSP_SENSOR_STATUS_REPORT
*                       - BMKT_RSP_SENSOR_STATUS_FAIL
* @param[in]  cb_ctx    Responce callback user context
*
* @return     VCS_RESULT_OK if success
*
* When this command is being executed, fingerprint sensor module’s mode is: Miscellaneous system operations
*/
int
bmkt_get_status(
    bmkt_sensor_t *     sensor,
    bmkt_resp_cb_t      resp_cb,
    void *              cb_ctx);

/**
* bmkt_repeat_last_responce:
* @brief      Repeat last responce sent 
*
* @param[in]  sensor    The sensor module pointer
* @param[in]  resp_cb   Responce callback function. Available responses:
*                       - BMKT_RSP_REPEAT_LAST_BMKT_RSP_FAIL
*                       - The last responce transmitted by FPS module
* @param[in]  cb_ctx    Responce callback user context
*
* @return     VCS_RESULT_OK if success
*
* The fingerprint sensor module will respond to this command by simply re - sending its last response without
* making any modifications to the original response message whatsoever.
*/
int
bmkt_repeat_last_responce(
    bmkt_sensor_t *     sensor,
    bmkt_resp_cb_t      resp_cb,
    void *              cb_ctx);

/**
* bmkt_get_final_result:
* @brief      Retrive final result 
*
* @param[in]  sensor    The sensor module pointer
* @param[in]  resp_cb   Responce callback function. Available responses:
*                       - BMKT_RSP_RETRIEVE_FINAL_RESULT_FAIL
*                       - The final result of last opeartion.
* @param[in]  cb_ctx	Responce callback user context
*
* @return     VCS_RESULT_OK if success
*
* The fingerprint sensor module will respond to this command by simply re - sending its last response without
* making any modifications to the original response message whatsoever.
* The fingerprint sensor module will respond to this command by simple re - sending response message 
* containing the final result returned by the last command executed, as long as the command is one of these:
* -Enroll user
* -Identify user
* -Verify user
* -Delete user fingerprint
* -Delete full database
* -Identify user in order
* -Verify user in order
* -Verify finger in order
*/
int
bmkt_get_final_result(
    bmkt_sensor_t *     sensor,
    bmkt_resp_cb_t      resp_cb,
    void *              cb_ctx);

/**
* bmkt_identify_in_order:
* @brief      Put the fingerprint sensor module into identification mode for matching in pre - defined order.
*
* @param[in]  sensor                    The sensor module pointer
* @param[in]  user_num_total            The total number of user IDs in priority list
* @param[in]  user_num_in_order_list    The number of user IDs in this function call
* @param[in]  order_list                Pointer to first user ID record followed by next entries.
* @param[in]  resp_cb                   Responce callback function. Available responses:
*                                       - BMKT_RSP_SEND_NEXT_USER_ID - Notify caller to send the next batch of user IDs in the priority list.
*                                       - BMKT_RSP_CAPTURE_COMPLETE
*                                       - BMKT_RSP_ID_READY
*                                       - BMKT_RSP_ID_FAIL
*                                       - BMKT_RSP_ID_OK
* @param[in]  cb_ctx    responce callback user context
*
* @return     VCS_RESULT_OK if success
*
* Do identification operation with specified priority list of user IDs for matching, starting with the highest priority and ending with the lowest priority.
* The system continues matching against enrolled templates in the order specified till there is a successful match or till the entire template database has 
* been scanned and matched against.
* If the entire priority list of user IDs fits within the payload of this command then the fingerprint sensor module will not respond with a 
* BMKT_RSP_SEND_NEXT_USER_ID and instead will respond directly with BMKT_RSP_ID_READY
* When this command is being executed, fingerprint sensor module’s mode is: Identification
*/
int
bmkt_identify_in_order(
    bmkt_sensor_t *     sensor,
    uint32_t            user_num_total,
    uint32_t            user_num_in_order_list,
    bmkt_user_id_t *    order_list,
    bmkt_resp_cb_t      resp_cb,
    void *              cb_ctx);

/**
* bmkt_identify_in_order_next_users:
* @brief      Send next batch of user IDs to fingerprint sensor module for identify if a BMKT_RSP_SEND_NEXT_USER_ID response is received.
*
* @param[in]  sensor                    The sensor module pointer
* @param[in]  user_num_in_order_list    The number of user IDs in this function call
* @param[in]  order_list                Pointer to first user ID record followed by next entries.
* @param[in]  resp_cb                   Responce callback function. Available responses:
*                                       - BMKT_RSP_SEND_NEXT_USER_ID - Notify caller to send the next batch of user IDs in the priority list.
*                                       - BMKT_RSP_CAPTURE_COMPLETE
*                                       - BMKT_RSP_ID_READY
*                                       - BMKT_RSP_ID_FAIL
*                                       - BMKT_RSP_ID_OK
* @param[in]  cb_ctx                    Responce callback user context
*
* @return     VCS_RESULT_OK if success
*
* If caller receives a BMKT_RSP_SEND_NEXT_USER_ID response from the fingerprint sensor module but there are no more user IDs to be sent,
* it must set the payload length of this command to 0 in order to indicate end of list.
* When this command is being executed, fingerprint sensor module’s mode is: Identification
*/
int
bmkt_identify_in_order_next_users(
    bmkt_sensor_t *     sensor,
    uint32_t            user_num_in_order_list,
    bmkt_user_id_t *    order_list,
    bmkt_resp_cb_t      resp_cb,
    void *              cb_ctx);

/**
* bmkt_verify_in_order:
* @brief      Put the fingerprint sensor module into verifying mode for matching against enrolled templates of multiple users, as specified by the priority list 
*
* @param[in]  sensor                    The sensor module pointer
* @param[in]  user_num_total            The total number of user IDs in priority list
* @param[in]  user_num_in_order_list    The number of user IDs in this function call
* @param[in]  order_list                Pointer to first user ID record followed by next entries.
* @param[in]  resp_cb                   Responce callback function. Available responses:
*                                       - BMKT_RSP_CAPTURE_COMPLETE
*                                       - BMKT_RSP_SEND_NEXT_USER_ID - Notify caller to send the next batch of user IDs in the priority list.
*                                       - BMKT_RSP_VERIFY_READY
*                                       - BMKT_RSP_VERIFY_FAIL
*                                       - BMKT_RSP_VERIFY_OK
* @param[in]  cb_ctx                    Responce callback user context
*
* @return     VCS_RESULT_OK if success
*
* Do verification operation with specified priority list of user IDs for matching, starting with the highest priority and ending with the lowest priority.
* The system continues matching against enrolled templates in the order specified till there is a successful match or till the entire template database has
* been scanned and matched against.
* If the entire priority list of user IDs fits within the payload of this command then the fingerprint sensor module will not respond with a
* BMKT_RSP_SEND_NEXT_USER_ID and instead will respond directly with BMKT_RSP_ID_READY
* When this command is being executed, fingerprint sensor module’s mode is: Verification
*/
int
bmkt_verify_in_order(
    bmkt_sensor_t *     sensor,
    uint32_t            user_num_total,
    uint32_t            user_num_in_order_list,
    bmkt_user_id_t *    order_list,
    bmkt_resp_cb_t      resp_cb,
    void *              cb_ctx);

/**
* bmkt_verify_in_order_next_users:
* @brief      Send next batch of user IDs to fingerprint sensor module for verify if a BMKT_RSP_SEND_NEXT_USER_ID response is received.
*
* @param[in]  sensor                    The sensor module pointer
* @param[in]  user_num_in_order_list    The number of user IDs in this function call
* @param[in]  order_list                Pointer to first user ID record followed by next entries.
* @param[in]  resp_cb                   Responce callback function. Available responses:
*                                       - BMKT_RSP_CAPTURE_COMPLETE
*                                       - BMKT_RSP_SEND_NEXT_USER_ID - Notify caller to send the next batch of user IDs in the priority list.
*                                       - BMKT_RSP_ID_READY
*                                       - BMKT_RSP_ID_FAIL
*                                       - BMKT_RSP_ID_OK
* @param[in]  cb_ctx                    Responce callback user context
*
* @return     VCS_RESULT_OK if success
*
* If caller receives a BMKT_RSP_SEND_NEXT_USER_ID response from the fingerprint sensor module but there are no more user IDs to be sent,
* it must set the payload length of this command to 0 in order to indicate end of list.
* When this command is being executed, fingerprint sensor module’s mode is: Verification
*/
int
bmkt_verify_in_order_next_users(
    bmkt_sensor_t *     sensor,
    uint32_t            user_num_in_order_list,
    bmkt_user_id_t *    order_list,
    bmkt_resp_cb_t      resp_cb,
    void *              cb_ctx);

/**
* bmkt_verify_finger_in_order:
* @brief      Put the fingerprint sensor module into verifying mode for matching against enrolled finger templates of specific user, as specified by the priority list
*
* @param[in]  sensor                    The sensor module pointer
* @param[in]  user_id                   The user ID
* @param[in]  user_id_len               The user ID lenght
* @param[in]  user_num_in_order_list    The number of finger IDs in the order list
* @param[in]  order_list                Pointer to first finger ID followed by next values.
* @param[in]  resp_cb                   Responce callback function. Available responses:
*                                       - BMKT_RSP_VERIFY_READY
*                                       - BMKT_RSP_CAPTURE_COMPLETE
*                                       - BMKT_RSP_VERIFY_FAIL
*                                       - BMKT_RSP_VERIFY_OK
* @param[in]  cb_ctx                    Responce callback user context
*
* @return     VCS_RESULT_OK if success
*
* Verifying the identity of a user by matching against enrolled finger templates in the order specified by the caller till there is a successful match or till the
* entire priority list of fingers has been scanned and matched against.If the priority list of fingers is a proper subset of the set of fingers enrolled for that user,
* then the remaining fingers are matched in the order of storage.In case there is a finger ID specified in the priority list which does not exist for that user ID in the
* fingerprint template database, then the system will terminate the operation and return error code 504.
*/
int
bmkt_verify_finger_in_order(
    bmkt_sensor_t *     sensor,
    const uint8_t *     user_id,
    uint32_t            user_id_len,
    uint32_t            finger_num_in_order_list,
    uint8_t *           order_list,
    bmkt_resp_cb_t      resp_cb,
    void *              cb_ctx);

int
bmkt_process_pending_interrupts(
    bmkt_sensor_t *     sensor);

#ifdef __cplusplus
}
#endif

#endif /* _BMKT_H_ */