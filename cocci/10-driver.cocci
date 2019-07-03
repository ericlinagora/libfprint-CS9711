@ orig_driver_struct @
identifier driver_struct;
@@
struct fp_img_driver driver_struct = {
	...,
};

// Grab the type of the main device struct using a rather blind fashion,
// and remove it from the init function.
// This assume that only device init calls fp_dev_set_instance_data, which
// is a fair assumption after all
@ orig_device_struct @
type device_struct;
expression dev;
identifier dev_init;
identifier data;
@@
dev_init(...)
{
	// Note: We redefine it to an instance access for now, which will be
	// made to work correctly with a later transform/cast.
...
	device_struct *data;
...
(
-	data = g_malloc0(sizeof(*data));
+	data = FP_INSTANCE_DATA(dev);
|
-	data = (device_struct*) g_malloc0(sizeof(*data));
+	data = FP_INSTANCE_DATA(dev);
|
-	data = g_malloc0(sizeof(device_struct));
+	data = FP_INSTANCE_DATA(dev);
)
...
-	fp_dev_set_instance_data(dev, data);
...
}

@ driver_ids @
typedef FpIdEntry;
identifier driver_id_table;
@@
-const struct usb_id driver_id_table[] = {
+const FpIdEntry driver_id_table[] = {
	...
};


@ @
identifier driver_ids.driver_id_table;
expression entry_vid;
expression entry_pid;
@@
const FpIdEntry driver_id_table[] = {
	..., {
-	.vendor = entry_vid, .product = entry_pid,
+	.vid = entry_vid, .pid = entry_pid,
	}, ...
};

@ @
identifier driver_ids.driver_id_table;
expression entry_vid;
expression entry_pid;
expression entry_data;
@@
const FpIdEntry driver_id_table[] = {
	..., {
-	.vendor = entry_vid, .product = entry_pid, .device_data = entry_data
+	.vid = entry_vid, .pid = entry_pid, .driver_data = entry_data
	}, ...
};

@ @
identifier driver_ids.driver_id_table;
expression entry_vid;
expression entry_pid;
expression entry_data;
@@
const FpIdEntry driver_id_table[] = {
	..., {
-	entry_vid, entry_pid, entry_data
+	.vid = entry_vid, .pid = entry_pid, .driver_data = entry_data
	}, ...
};



@ driver_info extends orig_driver_struct @
expression driver_full_name;
identifier driver_id_table;
identifier dev_open;
identifier dev_close;
identifier dev_scan_type;
@@
struct fp_img_driver driver_struct = {
	.driver = {
		.full_name = driver_full_name,
		.id_table = driver_id_table,
		.scan_type = dev_scan_type,
	},

	.open = dev_open,
	.close = dev_close,
};

@ script:python driver_gobj @
driver_struct << orig_driver_struct.driver_struct;

driver_id;
driver_init;
driver_cls;
_driver_cls;
driver_klass;
driver_class_init;
driver_ns;
driver_cast;
driver_cast_no_prefix;
@@

import os

driver_id = driver_struct.split('_')[:-1]

driver_cls = "FpDevice" + "".join(d[0].upper() + d[1:] for d in driver_id)
driver_id = '_'.join(driver_id)

driver_ns = "fp_device_" + driver_id
driver_cast = driver_ns.upper()
driver_cast_no_prefix = driver_ns.upper()[3:]

coccinelle.driver_id = cocci.make_expr('"%s"' % driver_id)
coccinelle.driver_cls = cocci.make_type(driver_cls)
coccinelle._driver_cls = cocci.make_type('struct _' + driver_cls)
coccinelle.driver_klass = cocci.make_type(driver_cls + 'Class')
coccinelle.driver_ns = cocci.make_ident(driver_ns)
coccinelle.driver_init = cocci.make_ident(driver_ns + '_init')
coccinelle.driver_class_init = cocci.make_ident(driver_ns + '_class_init')
coccinelle.driver_cast = cocci.make_ident(driver_cast)
coccinelle.driver_cast_no_prefix = cocci.make_ident(driver_cast_no_prefix)

#############################################################################

@ driver @
typedef FpDeviceClass;
typedef FpImageDeviceClass;

type orig_device_struct.device_struct;

identifier orig_driver_struct.driver_struct;

expression driver_gobj.driver_id;
identifier driver_gobj.driver_ns;
identifier driver_gobj.driver_init;
identifier driver_gobj.driver_class_init;
identifier driver_gobj.driver_cast;
identifier driver_gobj.driver_cast_no_prefix;
type driver_gobj.driver_cls;
type driver_gobj._driver_cls;
type driver_gobj.driver_klass;

expression driver_info.driver_full_name;
identifier driver_info.driver_id_table;
identifier driver_info.dev_open;
identifier driver_info.dev_close;
identifier driver_info.dev_scan_type;
@@
struct fp_img_driver driver_struct = {
	...
};

+static void
+driver_init(driver_cls *self)
+{
+}
+
+static void
+driver_class_init(driver_klass *klass)
+{
+	FpDeviceClass *dev_class = FP_DEVICE_CLASS (klass);
+	FpImageDeviceClass *img_class = FP_IMAGE_DEVICE_CLASS (klass);
+
+	dev_class->id = driver_id;
+	dev_class->full_name = driver_full_name;
+	dev_class->type = FP_DEVICE_TYPE_USB;
+	dev_class->id_table = driver_id_table;
+	dev_class->scan_type = dev_scan_type;
+
+	img_class->img_open = dev_open;
+	img_class->img_close = dev_close;
+	IMG_CLASS_FUNCS;
+}

/////////////////////////////////////////////////////////////////////////
@ optional_activate extends driver @
identifier dev_activate;
@@
struct fp_img_driver driver_struct = {
-	.activate = dev_activate,
};
@@
identifier optional_activate.dev_activate;
@@
+	img_class->activate = dev_activate;
	IMG_CLASS_FUNCS;

/////////////////////////////////////////////////////////////////////////
@ optional_deactivate extends driver @
identifier dev_deactivate;
@@
struct fp_img_driver driver_struct = {
-	.deactivate = dev_deactivate,
};
@@
identifier optional_deactivate.dev_deactivate;
@@
+	img_class->deactivate = dev_deactivate;
	IMG_CLASS_FUNCS;

/////////////////////////////////////////////////////////////////////////
@ optional_change_state extends driver @
identifier dev_change_state;
@@
struct fp_img_driver driver_struct = {
-	.change_state = dev_change_state,
};
@@
identifier optional_change_state.dev_change_state;
@@
+	img_class->change_state = dev_change_state;
	IMG_CLASS_FUNCS;

/////////////////////////////////////////////////////////////////////////
@ optional_bz3 extends driver @
expression dev_bz3_threshold;
@@
struct fp_img_driver driver_struct = {
-	.bz3_threshold = dev_bz3_threshold,
};
@@
expression optional_bz3.dev_bz3_threshold;
@@
+
+	img_class->bz3_threshold = dev_bz3_threshold;
	IMG_CLASS_FUNCS;

/////////////////////////////////////////////////////////////////////////
@ optional_img_size extends driver @
expression dev_img_width;
expression dev_img_height;
@@
struct fp_img_driver driver_struct = {
-	.img_width = dev_img_width,
-	.img_height = dev_img_height,
};
@@
expression optional_img_size.dev_img_width;
expression optional_img_size.dev_img_height;
@@
+
+	img_class->img_width = dev_img_width;
+	img_class->img_height = dev_img_height;
	IMG_CLASS_FUNCS;

@ remove_placeholder extends driver @
@@
-	IMG_CLASS_FUNCS;

@ remove_orig extends driver @
@@
-struct fp_img_driver driver_struct = {
-	...
-};

/////////////////////////////////////////////////////////////////////////
/////////////////////////////////////////////////////////////////////////
@ type_declaration extends driver @
@@
-device_struct {
+_driver_cls {
+	FpImageDevice parent;
+
	...
};
+#include "TYPE_DECLARATION"

@ extends driver @
@@
-#include "TYPE_DECLARATION"

+G_DECLARE_FINAL_TYPE (driver_cls, driver_ns, FP, driver_cast_no_prefix, FpImageDevice);
+G_DEFINE_TYPE (driver_cls, driver_ns, FP_TYPE_IMAGE_DEVICE);


///////////////
// Change some function declarations
@ extends driver @
identifier a_driver_data;
@@
-int
+void
dev_open(...
-  ,unsigned long a_driver_data
 ) { ... }

@ extends driver @
@@
-int
+void
dev_activate(...) { ... }


/////////////////////////////////////////////////////////////////////////
// Replace all old data with a cast to the new class
/////////////////////////////////////////////////////////////////////////
@ rewrite_dev_struct extends driver @
identifier func;
identifier data;
identifier dev;
@@
func(...)
{
	...
(
-	device_struct *data;
+	driver_cls *self;
	...
(
-	data = FP_INSTANCE_DATA(FP_DEVICE(dev));
+	data = driver_cast(dev);
|
-	data = FP_INSTANCE_DATA(dev);
+	data = driver_cast(dev);
)
|
-	device_struct *data = FP_INSTANCE_DATA(FP_DEVICE(dev));
+	driver_cls *self = driver_cast(dev);
|
-	device_struct *data = FP_INSTANCE_DATA(dev);
+	driver_cls *self = driver_cast(dev);
)
	...
}

@@
identifier rewrite_dev_struct.func;
identifier rewrite_dev_struct.data;
@@
func(...)
{
	<...
-	data
+	self
	...>
}

@ extends driver @
identifier func;
identifier data;
@@
func(...,
- device_struct *data,
+ driver_cls *self,
...)
{
	<...
-	data
+	self
	...>
}

// Remove unneccessary self check
@@
@@
-if (self != NULL) {
	...
-}

// Remove g_free(self)
@@
@@
-g_free(self);



