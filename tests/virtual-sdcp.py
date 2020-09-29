#!/usr/bin/env python3

import sys
try:
    import gi
    import os

    from gi.repository import GLib, Gio

    import unittest
    import subprocess
    import shutil
    import tempfile
except Exception as e:
    print("Missing dependencies: %s" % str(e))
    sys.exit(77)

FPrint = None

# Re-run the test with the passed wrapper if set
wrapper = os.getenv('LIBFPRINT_TEST_WRAPPER')
if wrapper:
    wrap_cmd = wrapper.split(' ') + [sys.executable, os.path.abspath(__file__)] + \
        sys.argv[1:]
    os.unsetenv('LIBFPRINT_TEST_WRAPPER')
    sys.exit(subprocess.check_call(wrap_cmd))

# Only permit loading virtual_sdcp driver for tests in this file
os.environ['FP_DRIVERS_WHITELIST'] = 'virtual_sdcp'

if hasattr(os.environ, 'MESON_SOURCE_ROOT'):
    root = os.environ['MESON_SOURCE_ROOT']
else:
    root = os.path.join(os.path.dirname(__file__), '..')

ctx = GLib.main_context_default()

class VirtualSDCP(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        os.environ['FP_VIRTUAL_SDCP'] = os.environ['SDCP_VIRT_BINARY']

        cls.ctx = FPrint.Context()

        cls.dev = None
        for dev in cls.ctx.get_devices():
            cls.dev = dev
            break

        assert cls.dev is not None, "You need to compile with virtual_sdcp for testing"

    @classmethod
    def tearDownClass(cls):
        del cls.dev
        del cls.ctx

    def setUp(self):
        pass

    def tearDown(self):
        pass

    def enroll(self, progress_cb=None):
        # Enroll another print
        template = FPrint.Print.new(self.dev)
        template.props.finger = FPrint.Finger.LEFT_THUMB
        template.props.username = "testuser"
        template.props.description = "test print"
        datetime = GLib.DateTime.new_now_local()
        date = GLib.Date()
        date.set_dmy(*datetime.get_ymd()[::-1])
        template.props.enroll_date = date
        return self.dev.enroll_sync(template, None, progress_cb, None)

    def test_connect(self):
        self.dev.open_sync()
        self.dev.close_sync()

    def test_reconnect(self):
        # Ensure device was opened once before, this may be a reconnect if
        # it is the same process as another test.
        self.dev.open_sync()
        self.dev.close_sync()

        # Check that a reconnect happens on next open. To know about this, we
        # need to parse check log messages for that.
        success = [False]
        def log_func(domain, level, msg):
            print("log: '%s', '%s', '%s'" % (str(domain), str(level), msg))
            if msg == 'Reconnect succeeded':
                success[0] = True

            # Call default handler
            GLib.log_default_handler(domain, level, msg)

        handler_id = GLib.log_set_handler('libfprint-sdcp_device', GLib.LogLevelFlags.LEVEL_DEBUG, log_func)
        self.dev.open_sync()
        self.dev.close_sync()
        GLib.log_remove_handler('libfprint-sdcp_device', handler_id)
        assert success[0]

    def test_enroll(self):
        self.dev.open_sync()

        # Must return a print
        assert isinstance(self.enroll(), FPrint.Print)

        self.dev.close_sync()


    def test_verify(self):
        self.dev.open_sync()

        # Enroll a new print (will be the last), check that it verifies
        p = self.enroll()
        match, dev_print = self.dev.verify_sync(p)
        assert match

        # The returned "device" print is identical
        assert p.equal(dev_print)

        # We can do the same with it
        match, dev_print2 = self.dev.verify_sync(dev_print)
        assert match

        # Now, enroll a new print, causing the old one to not match anymore
        # (the device always claims to see the last enrolled print).
        self.enroll()
        match, dev_print = self.dev.verify_sync(p)
        assert match is False

        self.dev.close_sync()

if __name__ == '__main__':
    try:
        gi.require_version('FPrint', '2.0')
        from gi.repository import FPrint
    except Exception as e:
        print("Missing dependencies: %s" % str(e))
        sys.exit(77)

    # avoid writing to stderr
    unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout, verbosity=2))
