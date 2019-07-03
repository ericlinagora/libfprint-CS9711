
@ dev_func @
identifier dev;
identifier func;
@@
func(..., FpDevice *dev, ...)
{
   ...
}

@ imgdev_func @
identifier dev;
identifier func;
@@
func(..., FpImageDevice *dev, ...)
{
   ...
}

@ self_func extends driver @
identifier self;
identifier func;
@@
func(..., driver_cls *self, ...)
{
   ...
}

@ transfer_func_imgdev @
typedef FpUsbTransfer;
identifier imgdev_func.func;
identifier imgdev_func.dev;
identifier transfer;
@@
func (...)
{
<...
(
-transfer = fpi_usb_alloc();
+transfer = fp_usb_transfer_new(FP_DEVICE (dev));
|
-FpUsbTransfer *transfer = fpi_usb_alloc();
+FpUsbTransfer *transfer = fp_usb_transfer_new(FP_DEVICE (dev));
)
...>
}


@ transfer_func_dev @
typedef FpUsbTransfer;
identifier dev_func.func;
identifier dev_func.dev;
identifier transfer;
@@
func (...)
{
<...
(
-transfer = fpi_usb_alloc();
+transfer = fp_usb_transfer_new(dev);
|
-FpUsbTransfer *transfer = fpi_usb_alloc();
+FpUsbTransfer *transfer = fp_usb_transfer_new(dev);
)
...>
}

@ transfer_func_self @
typedef FpUsbTransfer;
identifier self_func.func;
identifier self_func.self;
expression transfer;
@@
func (...)
{
<...
-transfer = fpi_usb_alloc();
+transfer = fp_usb_transfer_new(FP_DEVICE (self));
...>
}

// None of the release interface calls had error handling ...
@ extends driver @
expression usb_dev;
expression interface;
@@
dev_close(...)
{
+  GError *error = NULL;
...
-libusb_release_interface(usb_dev, interface);
+g_usb_device_release_interface(usb_dev, interface, 0, &error);
...
}

@ extends driver @
expression usb_dev;
expression interface;
identifier imgdev;
identifier r;
@@
dev_open (..., FpImageDevice *imgdev, ...)
{
+  GError *error = NULL;
...
-r = libusb_claim_interface(usb_dev, interface);
+if (!g_usb_device_claim_interface(usb_dev, interface, 0, &error)) {
+   _fp_image_device_open_complete (imgdev, error);
+   return;
+ }
(
-if (r != 0) { ... }
|
-if (r < 0) { ... }
)
...
}

