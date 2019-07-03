// Remove USB device reset, lets hope that we do not need this.
// If we do, maybe do it elsewhere?
@@
identifier r;
@@
- r = libusb_reset_device(...);
- if (r != 0) { ... }

// Functions that have uneccessary returns (i.e. error cannot happen after refactoring)
// NOTE: Make sure that these function are fine to modify in *all* drivers!
@ prior_int_func @
identifier func =~ "capture_chunk_async|alksdjflkajsfd";
expression res, res2;
@@
-int func
+void func
 (...)
{
	<...
-		return res2;
+		res2;
	...>
-	return res;
+	res;
}

@@
identifier prior_int_func.func;
identifier res;
@@
-res = func
+func
 (...);
(
-if (res < 0) { ... }
|
-if (res != 0) { ... }
)

// Remove useless checks of fpi_timeout_add return values
@@
expression a1, a2, a3, a4;
@@
-if (fpi_timeout_add(a1, a2, a3, a4) == NULL) { ... }
+fpi_timeout_add(a1, a2, a3, a4);

@@
identifier timeout;
expression a1, a2, a3, a4;
@@
timeout = fpi_timeout_add(a1, a2, a3, a4);
-if (timeout == NULL) { ... }


// The VFS5011 driver has some stupid "radiation detected" logic, that should be asserts
@@
expression expr;
@@
-if ((expr)) {
-  ...
-  fp_err("Radiation detected!");
-  ...
-}
+g_assert(!expr);


// A number of drivers call both fpi_imgdev_session_error *and* fpi_ssm_mark_failed.
// While this worked fine, it is plain wrong and considerably complicates memory
// management of the errors.
// Remove this duplication
@@
expression dev;
expression ssm;
expression error;
@@
-	fpi_imgdev_session_error(dev, error);
	fpi_ssm_mark_failed(ssm, error);

