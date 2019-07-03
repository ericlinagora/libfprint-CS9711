@ fp_img_dev @
typedef FpImageDevice;
@@
-struct fp_img_dev
+FpImageDevice

@ FpImageDevice_cast @
typedef FpImageDevice;
expression dev;
@@
-(FpImageDevice*) dev
+FP_IMAGE_DEVICE (dev)

@ fp_dev @
typedef FpDevice;
@@
-struct fp_dev
+FpDevice

@ FpDevice_cast @
typedef FpDevice;
expression dev;
@@
-(FpDevice*) dev
+FP_DEVICE (dev)

@ FP_DEV_cast @
expression dev;
@@
- FP_DEV (dev)
+ FP_DEVICE (dev)

@ FP_IMG_DEV_cast @
expression dev;
@@
- FP_IMG_DEV (dev)
+ FP_IMAGE_DEVICE (dev)

@ fpi_ssm @
typedef fpi_ssm;
typedef FpSsm;
@@
-fpi_ssm
+FpSsm

@ fp_img @
typedef FpImage;
@@
-struct fp_img
+FpImage

@ libusb_transfer @
typedef FpUsbTransfer;
@@
-struct libusb_transfer
+FpUsbTransfer

@ libusb_device_handle @
typedef libusb_device_handle;
typedef GUsbDevice;
@@
-libusb_device_handle
+GUsbDevice

