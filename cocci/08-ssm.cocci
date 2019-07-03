@ ssm_callbacks @
identifier ssm;
identifier cb;
@@
fp_ssm_start(ssm, cb)

@@
identifier ssm_callbacks.cb;
identifier ssm;
identifier dev;
identifier user_data;
@@
void cb(FpSsm *ssm, FpDevice *dev, void* user_data
+       , GError *error
 )
{
+	_Pragma("GCC warning \"Check that error is returned/free'ed properly!\"");
	...
}

