@ usb_transfer_cb @
typedef FpUsbTransfer;
typedef FpSsm;
identifier func;
identifier transfer;
identifier dev;
identifier ssm;
@@
(
-void func(FpUsbTransfer *transfer)
+void func(FpUsbTransfer *transfer,
+          FpDevice      *device,
+          gpointer       user_data,
+          GError        *error)
{
...
}
|
// this is weird, one function in uru4000 didn't get the types
// converted by earlier rules. But, this does not seem to work either.
-void func(\(FpUsbTransfer*\|struct libusb_transfer*\) transfer,
-          \(FpDevice*\|struct fp_dev*\)   dev,
-          \(FpSsm*\|fpi_ssm*\)   ssm,
-          void*   user_data)
+void func(FpUsbTransfer *transfer,
+          FpDevice      *dev,
+          gpointer       user_data,
+          GError        *error)
{
+	FpSsm *ssm = transfer->ssm;
...
}
)


@ errors_generic_1 @
identifier usb_transfer_cb.func;
identifier usb_transfer_cb.transfer;
@@
func(...)
{
	<...
(
-	(transfer->status != LIBUSB_TRANSFER_COMPLETED)
+	error
|
-	(transfer->status == LIBUSB_TRANSFER_COMPLETED)
+	!error
|
-	(transfer->status == LIBUSB_TRANSFER_TIMED_OUT)
+	g_error_matches (error, G_USB_DEVICE_ERROR, G_USB_DEVICE_ERROR_TIMED_OUT)
)
	...>
}


@ errors_1 @
identifier usb_transfer_cb.func;
identifier usb_transfer_cb.transfer;
expression ssm;
statement S;
@@
func(...)
{
	<...
	if (error) {
(
		...
-		fpi_ssm_mark_failed (ssm, ...)
+		fp_ssm_mark_failed (ssm, error)
		...
|
		...
-		fpi_imgdev_session_error (...)
+		_fp_image_device_session_error (FP_IMAGE_DEVICE (device), error)
		...
)
	}
	...>
}

@ errors_1_alt @
identifier usb_transfer_cb.func;
identifier usb_transfer_cb.transfer;
expression ssm;
statement S;
@@
func(...)
{
	<...
	if (!error) { ... }
	else {
(
		...
-		fpi_ssm_mark_failed (ssm, ...)
+		fp_ssm_mark_failed (ssm, error)
		...
|
		...
-		fpi_imgdev_session_error (...)
+		_fp_image_device_session_error (FP_IMAGE_DEVICE (device), error)
		...
)
	}
	...>
}

@ errors_2 @
identifier usb_transfer_cb.func;
identifier usb_transfer_cb.transfer;
expression ssm;
@@
func(...)
{
	<...
	if (transfer->length != transfer->actual_length) {
+		_Pragma("GCC warning \"Driver should probably set short_is_error instead!\"");
		...
(
-		fpi_ssm_mark_failed (ssm, ...);
+		fp_ssm_mark_failed (ssm, g_error_new (G_USB_DEVICE_ERROR,
+		                                      G_USB_DEVICE_ERROR_IO,
+		                                      "Short USB transfer!"));
|
-		fpi_imgdev_session_error (...);
+		_fp_image_device_session_error (FP_IMAGE_DEVICE (device),
+                                               g_error_new (G_USB_DEVICE_ERROR,
+                                                            G_USB_DEVICE_ERROR_IO,
+                                                            "Short USB transfer!"));
)
		...
	}
	...>
}

@ not_useful_error_prints @
identifier usb_transfer_cb.func;
@@
func(...)
{
	<...
-	fp_err (...);
	...
(
	fp_ssm_mark_failed (...);
|
	_fp_image_device_session_error (...);
)
	...>
}

@ error_or_wrong_length @
identifier usb_transfer_cb.func;
identifier usb_transfer_cb.transfer;
expression ssm;
@@
func(...)
{
	<...
-	if (error || (transfer->length != transfer->actual_length))
+	if (error)
	{
+		_Pragma("GCC warning \"Driver needs to set short_is_error for this branch to be taken!\"");
		<...
(
-		fpi_ssm_mark_failed (ssm, ...);
+		fp_ssm_mark_failed (ssm, error);
|
-		fpi_imgdev_session_error (...);
+		_fp_image_device_session_error (FP_IMAGE_DEVICE (device), error);
)
		...>
	}
	...>
}

@ error_or_wrong_length_2 @
identifier usb_transfer_cb.func;
identifier usb_transfer_cb.transfer;
expression ssm;
@@
func(...)
{
	<...
-	if (!error && (transfer->length == transfer->actual_length))
+	if (!error)
	{ ... }
	else {
+		_Pragma("GCC warning \"Driver needs to set short_is_error for this branch to be taken!\"");
		<...
(
-		fpi_ssm_mark_failed (ssm, ...);
+		fp_ssm_mark_failed (ssm, error);
|
-		fpi_imgdev_session_error (...);
+		_fp_image_device_session_error (FP_IMAGE_DEVICE (device), error);
)
		...>
	}
	...>
}


@@
identifier usb_transfer_cb.func;
identifier usb_transfer_cb.transfer;
identifier out;
@@
func(...)
{
<...
-	goto out;
+	return;
...>
-out:
(
-	g_free(transfer->buffer);
|
)
-	libusb_free_transfer (transfer);
}

@@
identifier usb_transfer_cb.func;
identifier usb_transfer_cb.transfer;
@@
func(...)
{
	<...
(
-	g_free(transfer->buffer);
|
)
-	libusb_free_transfer (transfer);
	...
	return;
	...>
}


@@
identifier usb_transfer_cb.func;
identifier usb_transfer_cb.transfer;
@@
func(...)
{
	<...
-	transfer->user_data
+	user_data
	...>
}

@@
typedef gint;
identifier usb_transfer_cb.func;
identifier usb_transfer_cb.transfer;
@@
func(...)
{
<...
(
	fp_dbg
|
	fp_warn
|
	fp_err
)
	(...,
-	transfer->length
+	(gint) transfer->length
	, ...);
...>
}

@@
typedef gint;
identifier usb_transfer_cb.func;
identifier usb_transfer_cb.transfer;
@@
func(...)
{
<...
(
	fp_dbg
|
	fp_warn
|
	fp_err
)
	(...,
-	transfer->actual_length
+	(gint) transfer->actual_length
	, ...);
...>
}

@@
identifier usb_transfer_cb.func;
identifier usb_transfer_cb.transfer;
identifier ssm_var;
gpointer user_data;
@@
func(...)
{
	...
(
-	FpSsm *ssm_var = (FpSsm*) user_data;
|
-	FpSsm *ssm_var = user_data;
)
	<...
-		ssm_var
+		transfer->ssm
	...>
}

// A lot of drivers abuse the SSM user_data for the driver
// Convert FpImageDevice usage to simple cast
@@
identifier usb_transfer_cb.func;
identifier dev;
@@
func(...)
{
-FpImageDevice *dev = ...;
+FpImageDevice *dev = FP_IMAGE_DEVICE (device);
...
}

// A lot of drivers abuse the SSM user_data for the driver
// Remove FpDevice getter and use argument
@@
identifier usb_transfer_cb.func;
identifier arg;
identifier dev;
@@
func(..., FpDevice *arg, ...)
{
-FpDevice *dev = ...;
<...
-dev
+arg
...>
}
