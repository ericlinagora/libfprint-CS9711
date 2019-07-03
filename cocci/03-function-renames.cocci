@@
expression w;
expression h;
expression status;
expression dev;
expression a1, a2, a3;
@@
(
-fpi_img_new(w * h)
+fp_image_new(w, h)
|
-fpi_ssm_new
+fp_ssm_new
|
-fpi_ssm_free
+fp_ssm_free
|
-fpi_ssm_start
+fp_ssm_start
|
-fpi_ssm_start_subsm
+fp_ssm_start_subsm
|
-fpi_ssm_next_state
+fp_ssm_next_state
|
-fpi_ssm_jump_to_state
+fp_ssm_jump_to_state
|
-fpi_ssm_mark_completed
+fp_ssm_mark_completed
|
-fpi_ssm_get_user_data
+fp_ssm_get_user_data
|
-fpi_ssm_get_cur_state
+fp_ssm_get_cur_state
|
-fpi_dev_get_usb_dev
+_fp_device_get_usb_device
|
// HACK: We just insert an error return here!
-fpi_imgdev_close_complete(dev)
+_fp_image_device_close_complete(dev, error)
|
-fpi_imgdev_open_complete(dev, 0)
+_fp_image_device_open_complete(dev, NULL)
|
-fpi_imgdev_activate_complete(dev, 0)
+_fp_image_device_activate_complete(dev, NULL)
|
-fpi_imgdev_deactivate_complete(dev)
+_fp_image_device_deactivate_complete(dev, NULL)
|
-fpi_imgdev_report_finger_status(dev, status)
+_fp_image_device_report_finger_status(dev, status)
|
-fpi_imgdev_image_captured(dev, a1)
+_fp_image_device_image_captured(dev, a1)
|
-fpi_imgdev_abort_scan
+_fp_image_device_retry_scan
|
-fpi_std_sq_dev
+_fp_std_sq_dev
|
-fpi_mean_sq_diff_norm
+_fp_mean_sq_diff_norm
|
-fpi_timeout_add(a1, a2, dev, a3)
+_fp_device_add_timeout(dev, a1, a2, a3)
)

// Some can be nested
@@
@@
(
-fpi_ssm_next_state_timeout_cb
+fp_ssm_next_state_timeout_cb
)
