// If we have matches on error conditions, the we likely have a memory
// mangement error.
@ forall @
identifier error;
statement S;
@@
if (<+... g_error_matches(error, ...) ...+>) {
+	_Pragma ("GCC error \"Inserted possibly wrong g_error_free!\"");
+	if (error)
+		g_error_free (error);
	...
} else S

@ forall @
identifier error;
@@
if (<+... g_error_matches(error, ...) ...+>) {
+	_Pragma ("GCC error \"Inserted possibly wrong g_error_free!\"");
+	if (error)
+		g_error_free (error);
	...
}

@@
expression transfer;
identifier r;
statement S;
@@
(
-	r = libusb_cancel_transfer(transfer);
-	if (r < 0) S
+	_Pragma("GCC warning \"Removed libusb_cancel_transfer call!\"");
+	g_warning("USB transfer %p should be cancelled but was not due to a lack of code migration!", transfer);
|
-	libusb_cancel_transfer(transfer);
+	_Pragma("GCC warning \"Removed libusb_cancel_transfer call!\"");
+	g_warning("USB transfer %p should be cancelled but was not due to a lack of code migration!", transfer);
)
