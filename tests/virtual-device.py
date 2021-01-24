#!/usr/bin/env python3

import sys
try:
    import gi
    import re
    import os

    from gi.repository import GLib, Gio

    import unittest
    import socket
    import struct
    import subprocess
    import shutil
    import glob
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

ctx = GLib.main_context_default()


class Connection:

    def __init__(self, addr):
        self.addr = addr

    def __enter__(self):
        self.con = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.con.connect(self.addr)
        return self.con

    def __exit__(self, exc_type, exc_val, exc_tb):
        self.con.close()
        del self.con

class VirtualDevice(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        unittest.TestCase.setUpClass()
        cls.tmpdir = tempfile.mkdtemp(prefix='libfprint-')

        driver_name = cls.driver_name if hasattr(cls, 'driver_name') else None
        if not driver_name:
            driver_name = re.compile(r'(?<!^)(?=[A-Z])').sub(
                '_', cls.__name__).lower()

        sock_name = driver_name.replace('_', '-')
        cls.sockaddr = os.path.join(cls.tmpdir, '{}.socket'.format(sock_name))
        os.environ['FP_{}'.format(driver_name.upper())] = cls.sockaddr

        cls.ctx = FPrint.Context()

        cls.dev = None
        for dev in cls.ctx.get_devices():
            # We might have a USB device in the test system that needs skipping
            if dev.get_driver() == driver_name:
                cls.dev = dev
                break

        assert cls.dev is not None, "You need to compile with {} for testing".format(driver_name)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmpdir)
        del cls.dev
        del cls.ctx
        unittest.TestCase.tearDownClass()

    def setUp(self):
        super().setUp()
        self.assertFalse(self.dev.is_open())
        self.dev.open_sync()
        self.assertTrue(self.dev.is_open())

    def tearDown(self):
        self.assertTrue(self.dev.is_open())
        self.dev.close_sync()
        self.assertFalse(self.dev.is_open())
        super().tearDown()

    def send_command(self, command, *args):
        self.assertIn(command, ['INSERT', 'REMOVE', 'SCAN', 'ERROR', 'RETRY',
            'FINGER'])

        with Connection(self.sockaddr) as con:
            params = ' '.join(str(p) for p in args)
            con.sendall('{} {}'.format(command, params).encode('utf-8'))

        while ctx.pending():
            ctx.iteration(False)

    def send_finger_report(self, has_finger, iterate=True):
        self.send_command('FINGER', 1 if has_finger else 0)

        if iterate:
            expected = (FPrint.FingerStatusFlags.PRESENT if has_finger
                else ~FPrint.FingerStatusFlags.PRESENT)

            while not (self.dev.get_finger_status() & expected):
                ctx.iteration(True)

    def enroll_print(self, nick, finger, username='testuser'):
        self._enrolled = None

        def done_cb(dev, res):
            print("Enroll done")
            self._enrolled = dev.enroll_finish(res)

        self.assertEqual(self.dev.get_finger_status(), FPrint.FingerStatusFlags.NONE)

        self.send_command('SCAN', nick)

        template = FPrint.Print.new(self.dev)
        template.set_finger(finger)
        template.set_username(username)

        self.dev.enroll(template, None, None, tuple(), done_cb)
        while self._enrolled is None:
            ctx.iteration(False)

            if not self._enrolled:
                self.assertEqual(self.dev.get_finger_status(),
                    FPrint.FingerStatusFlags.NEEDED)

        self.assertEqual(self.dev.get_finger_status(), FPrint.FingerStatusFlags.NONE)

        self.assertEqual(self._enrolled.get_device_stored(),
            self.dev.has_storage())

        return self._enrolled

    def check_verify(self, p, scan_nick, match):
        self._verify_match = None
        self._verify_fp = None
        self._verify_error = None

        if isinstance(scan_nick, str):
            self.send_command('SCAN', scan_nick)
        elif isinstance(scan_nick, FPrint.DeviceError):
            self.send_command('ERROR', int(scan_nick))
        elif isinstance(scan_nick, FPrint.DeviceRetry):
            self.send_command('RETRY', int(scan_nick))

        def verify_cb(dev, res):
            try:
                self._verify_match, self._verify_fp = dev.verify_finish(res)
            except gi.repository.GLib.Error as e:
                self._verify_error = e

        self.dev.verify(p, callback=verify_cb)
        while self._verify_match is None and self._verify_error is None:
            ctx.iteration(True)

        if match:
            assert self._verify_fp.equal(p)

        if isinstance(scan_nick, str):
            self.assertEqual(self._verify_fp.props.fpi_data.get_string(), scan_nick)

        if self._verify_error is not None:
            raise self._verify_error

    def test_device_properties(self):
        self.assertEqual(self.dev.get_driver(), 'virtual_device')
        self.assertEqual(self.dev.get_device_id(), '0')
        self.assertEqual(self.dev.get_name(), 'Virtual device for debugging')
        self.assertTrue(self.dev.is_open())
        self.assertEqual(self.dev.get_scan_type(), FPrint.ScanType.SWIPE)
        self.assertEqual(self.dev.get_nr_enroll_stages(), 5)
        self.assertFalse(self.dev.supports_identify())
        self.assertFalse(self.dev.supports_capture())
        self.assertFalse(self.dev.has_storage())

    def test_enroll(self):
        matching = self.enroll_print('testprint', FPrint.Finger.LEFT_LITTLE)
        self.assertEqual(matching.get_username(), 'testuser')
        self.assertEqual(matching.get_finger(), FPrint.Finger.LEFT_LITTLE)

    def test_enroll_verify_match(self):
        matching = self.enroll_print('testprint', FPrint.Finger.LEFT_THUMB)

        self.check_verify(matching, 'testprint', match=True)

    def test_enroll_verify_no_match(self):
        matching = self.enroll_print('testprint', FPrint.Finger.LEFT_RING)

        self.check_verify(matching, 'not-testprint', match=False)

    def test_enroll_verify_error(self):
        matching = self.enroll_print('testprint', FPrint.Finger.LEFT_RING)

        with self.assertRaisesRegex(GLib.Error, r"An unspecified error occurred"):
            self.check_verify(matching, FPrint.DeviceError.GENERAL, match=False)

    def test_enroll_verify_retry(self):
        with self.assertRaisesRegex(GLib.GError, 'too short'):
            self.check_verify(FPrint.Print.new(self.dev),
                FPrint.DeviceRetry.TOO_SHORT, match=False)

    def test_finger_status(self):
        cancellable = Gio.Cancellable()
        got_cb = False

        def verify_cb(dev, res):
            nonlocal got_cb
            got_cb = True

        self.dev.verify(FPrint.Print.new(self.dev), callback=verify_cb, cancellable=cancellable)
        while not self.dev.get_finger_status() is FPrint.FingerStatusFlags.NEEDED:
            ctx.iteration(True)

        self.send_finger_report(True)
        self.assertEqual(self.dev.get_finger_status(),
            FPrint.FingerStatusFlags.NEEDED | FPrint.FingerStatusFlags.PRESENT)

        self.send_finger_report(False)
        self.assertEqual(self.dev.get_finger_status(), FPrint.FingerStatusFlags.NEEDED)

        cancellable.cancel()
        while not got_cb:
            ctx.iteration(True)

        self.assertEqual(self.dev.get_finger_status(), FPrint.FingerStatusFlags.NONE)

class VirtualDeviceStorage(VirtualDevice):

    def cleanup_device_storage(self):
        for print in self.dev.list_prints_sync():
            self.assertTrue(self.dev.delete_print_sync(print, None))

    def test_device_properties(self):
        self.assertEqual(self.dev.get_driver(), 'virtual_device_storage')
        self.assertEqual(self.dev.get_device_id(), '0')
        self.assertEqual(self.dev.get_name(),
            'Virtual device with storage and identification for debugging')
        self.assertTrue(self.dev.is_open())
        self.assertEqual(self.dev.get_scan_type(), FPrint.ScanType.SWIPE)
        self.assertEqual(self.dev.get_nr_enroll_stages(), 5)
        self.assertTrue(self.dev.supports_identify())
        self.assertFalse(self.dev.supports_capture())
        self.assertTrue(self.dev.has_storage())

    def test_list_empty(self):
        self.cleanup_device_storage()
        self.assertFalse(self.dev.list_prints_sync())

    def test_list_populated(self):
        self.cleanup_device_storage()
        self.send_command('INSERT', 'p1')
        print2 = self.enroll_print('p2', FPrint.Finger.LEFT_LITTLE)
        self.assertEqual({'p1', 'p2'}, {p.props.fpi_data.get_string() for p in self.dev.list_prints_sync()})

    def test_list_delete(self):
        self.cleanup_device_storage()
        p = self.enroll_print('testprint', FPrint.Finger.RIGHT_THUMB)
        l = self.dev.list_prints_sync()
        print(l[0])
        self.assertEqual(len(l), 1)
        print('blub', p.props.fpi_data, type(l[0].props.fpi_data))
        assert p.equal(l[0])
        self.dev.delete_print_sync(p)
        self.assertFalse(self.dev.list_prints_sync())

    def test_list_delete_missing(self):
        self.cleanup_device_storage()
        p = self.enroll_print('testprint', FPrint.Finger.RIGHT_THUMB)
        self.send_command('REMOVE', 'testprint')

        with self.assertRaisesRegex(GLib.GError, 'Print was not found'):
            self.dev.delete_print_sync(p)


if __name__ == '__main__':
    try:
        gi.require_version('FPrint', '2.0')
        from gi.repository import FPrint
    except Exception as e:
        print("Missing dependencies: %s" % str(e))
        sys.exit(77)

    # avoid writing to stderr
    unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout, verbosity=2))
