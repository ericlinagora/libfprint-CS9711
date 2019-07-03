///////////////////////////////////////////////////////////////////////////
// bulk transfers
@@
typedef FpUsbTransfer;
expression transfer;
expression usb_dev;
expression endpoint;
expression data;
expression size;
expression cb;
expression user_data;
expression timeout;
identifier ret;
@@
-	libusb_fill_bulk_transfer(transfer, usb_dev, endpoint, data, size, cb, user_data, timeout);
+	fp_usb_transfer_fill_bulk_full(transfer, endpoint, data, size, NULL);
+	fp_usb_transfer_submit(transfer, timeout, NULL, cb, user_data);
+	fp_usb_transfer_unref(transfer);
(
-	ret = libusb_submit_transfer(transfer);
	...
(
-	if (ret < 0) { ... }
|
-	if (ret != 0) { ... }
|
)
|
-	if (libusb_submit_transfer(transfer)) { ... }
|
-	if (libusb_submit_transfer(transfer) < 0) { ... }
|
-	libusb_submit_transfer(transfer);
)

///////////////////////////////////////////////////////////////////////////
// bulk transfers
@@
typedef FpUsbTransfer;
expression transfer;
expression dev;
expression transfer_ssm;
expression endpoint;
expression data;
expression size;
expression cb;
expression user_data;
expression timeout;
expression storage;
identifier ret;
@@
-	transfer = fpi_usb_fill_bulk_transfer(dev, transfer_ssm, endpoint, data, size, cb, user_data, timeout);
+	transfer = fp_usb_transfer_new (dev);
+	transfer->ssm = transfer_ssm;
+	fp_usb_transfer_fill_bulk_full(transfer, endpoint, data, size, NULL);
+	fp_usb_transfer_submit(transfer, timeout, NULL, cb, user_data);
+	fp_usb_transfer_unref(transfer);
(
	storage = transfer;
|
)
(
-	ret = fpi_usb_submit_transfer(transfer);
	...
(
-	if (ret < 0) { ... }
|
-	if (ret != 0) { ... }
|
)
|
-	if (fpi_usb_submit_transfer(transfer)) { ... }
|
-	if (fpi_usb_submit_transfer(transfer) < 0) { ... }
|
-	fpi_usb_submit_transfer(transfer);
)

// The following only happens due to some prior simplifications we did
@@
typedef FpUsbTransfer;
expression transfer;
expression usb_dev;
expression endpoint;
expression data;
expression size;
expression cb;
expression user_data;
expression timeout;
identifier ret;
@@
-	libusb_fill_bulk_transfer(transfer, usb_dev, endpoint, data, size, cb, user_data, timeout);
-	libusb_submit_transfer(transfer);
+	fp_usb_transfer_fill_bulk_full(transfer, endpoint, data, size, NULL);
+	fp_usb_transfer_submit(transfer, timeout, NULL, cb, user_data);
+	fp_usb_transfer_unref(transfer);


///////////////////////////////////////////////////////////////////////////
// control transfers
@@
typedef FpUsbTransfer;
expression timeout;
expression direction;
expression request_type;
expression recipient;
expression request;
expression value;
expression index;
expression length;
expression callback;
expression user_data;
expression usb_dev;
identifier ret;
@@
(
-	data = g_malloc(LIBUSB_CONTROL_SETUP_SIZE)
|
-	data = g_malloc(LIBUSB_CONTROL_SETUP_SIZE + ...)
|
-	data = g_malloc0(LIBUSB_CONTROL_SETUP_SIZE)
|
-	data = g_malloc0(LIBUSB_CONTROL_SETUP_SIZE + ...)
)
-	;
+	_Pragma("GCC warning \"control transfer filling is a mess due to automatic translation\"");
+	fp_usb_transfer_fill_control(transfer, !((request_type) & 0x80), ((request_type) >> 5) & 0x3, (request_type) & 0x1f, request, value, index, length);
+	data = transfer->buffer;
+	fp_usb_transfer_submit(transfer, timeout, NULL, callback, user_data);
+	fp_usb_transfer_unref(transfer);
	...
-	libusb_fill_control_setup(data, request_type, request, value, index, length);
-	libusb_fill_control_transfer(transfer, usb_dev, data, callback, user_data, timeout);
	<...
-	LIBUSB_CONTROL_SETUP_SIZE
	...>
-	ret = libusb_submit_transfer(transfer);
(
-	if (ret < 0) { ... }
|
)


///////////////////////////////////////////////////////////////////////////
// We have a field in the transfer just for a state machine, use that
// We also later modify all similar code on the callback side to use that field
// instead.
@@
expression transfer;
expression timeout;
expression cb;
@@
+	transfer->ssm = ssm;
-	fp_usb_transfer_submit(transfer, timeout, NULL, cb, ssm);
+	fp_usb_transfer_submit(transfer, timeout, NULL, cb, NULL);
@@
expression transfer;
expression timeout;
expression cb;
@@
-	fp_usb_transfer_submit(transfer, timeout, NULL, cb, dev);
+	fp_usb_transfer_submit(transfer, timeout, NULL, cb, NULL);

