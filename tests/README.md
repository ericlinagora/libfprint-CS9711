`umockdev` Tests
================
`umockdev` tests use fingerprint devices mocked by [`umockdev`
toolchain][umockdev].

This document describes how to create test cases (for USB devices). Many of
these tests are tests for image devices, where a single image is captured
and stored.

Other kinds of `umockdev` tests can be created in a similar manner. For
match-on-chip devices you would instead create a test specific `custom.py`
script, capture it and store the capture to `custom.pcapng`.

'capture' and 'custom' Test Creation
------------------------------------

For image devices the `capture.py` script will be used to capture one reference
image. If the driver is a non-image driver, then a `custom.py` script should be
created in advance, which will be run instead.

1. Make sure that libfprint is built with support for the device driver
   that you want to create a test case for.

2. From the build directory, run tests/create-driver-test.py as root. Note
   that if you're capturing data for a driver which already has a test case
   but the hardware is slightly different, you might want to pass a variant
   name as a command-line options, for example:
```sh
$ sudo tests/create-driver-test.py driver [variant]
```

3. If the capture is not successful, run the tool again to start another capture.

4. Add driver test name to `drivers_tests` in the `meson.build`, as instructed,
   and change the ownership of the just-created test directory in the source.

5. Check whether `meson test` passes with this new test.

**Note.** To avoid submitting a real fingerprint when creating a 'capture' test,
the side of finger, arm, or anything else producing an image with the device
can be used.


Possible Issues
---------------

Other changes may be needed to get everything working. For example the
`elan` driver relies on a timeout that is not reported correctly. In
this case the driver works around it by interpreting the protocol
error differently in the virtual environment (by means of
`FP_DEVICE_EMULATION` environment variable).


[umockdev]: https://github.com/martinpitt/umockdev
