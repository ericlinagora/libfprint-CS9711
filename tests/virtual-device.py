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
            'FINGER', 'SET_ENROLL_STAGES', 'SET_SCAN_TYPE'])

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

    def send_error(self, error):
        self.assertIsInstance(error, FPrint.DeviceError)
        self.send_command('ERROR', int(error))

    def send_retry(self, retry):
        self.assertIsInstance(retry, FPrint.DeviceRetry)
        self.send_command('RETRY', int(retry))

    def send_auto(self, obj):
        if isinstance(obj, FPrint.DeviceError):
            self.send_error(obj)
        elif isinstance(obj, FPrint.DeviceRetry):
            self.send_retry(obj)
        elif isinstance(obj, FPrint.FingerStatusFlags):
            self.send_finger_report(obj & FPrint.FingerStatusFlags.PRESENT, iterate=False)
        elif isinstance(obj, FPrint.ScanType):
            self.send_command('SET_SCAN_TYPE', obj.value_nick)
        else:
            raise Exception('No known type found for {}'.format(obj))

    def enroll_print(self, nick, finger, username='testuser'):
        self._enrolled = None

        def done_cb(dev, res):
            print("Enroll done")
            self._enrolled = dev.enroll_finish(res)

        self._enroll_stage = -1
        def progress_cb(dev, stage, pnt, data, error):
            self._enroll_stage = stage
            self._enroll_progress_error = error

        stage = 1
        def enroll_in_progress():
            if self._enroll_stage < 0 and not self._enrolled:
                return True

            nonlocal stage
            self.assertLessEqual(self._enroll_stage, self.dev.get_nr_enroll_stages())
            self.assertEqual(self._enroll_stage, stage)

            if self._enroll_stage < self.dev.get_nr_enroll_stages():
                self._enroll_stage = -1
                self.assertIsNone(self._enrolled)
                self.assertEqual(self.dev.get_finger_status(),
                    FPrint.FingerStatusFlags.NEEDED)
                GLib.idle_add(self.send_command, 'SCAN', nick)
                stage += 1

            return not self._enrolled

        self.assertEqual(self.dev.get_finger_status(), FPrint.FingerStatusFlags.NONE)

        self.send_command('SCAN', nick)

        template = FPrint.Print.new(self.dev)
        template.set_finger(finger)
        template.set_username(username)

        self.dev.enroll(template, callback=done_cb, progress_cb=progress_cb)
        while enroll_in_progress():
            ctx.iteration(False)

        self.assertEqual(self._enroll_stage, stage)
        self.assertEqual(self._enroll_stage, self.dev.get_nr_enroll_stages())
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
        else:
            self.send_auto(scan_nick)

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

    def test_change_enroll_stages(self):
        notified_spec = None
        def on_stage_changed(dev, spec):
            nonlocal notified_spec
            notified_spec = spec

        self.dev.connect('notify::nr-enroll-stages', on_stage_changed)

        notified_spec = None
        self.send_command('SET_ENROLL_STAGES', 20)
        self.assertEqual(self.dev.get_nr_enroll_stages(), 20)
        self.assertEqual(notified_spec.name, 'nr-enroll-stages')

        notified_spec = None
        self.send_command('SET_ENROLL_STAGES', 1)
        self.assertEqual(self.dev.get_nr_enroll_stages(), 1)
        self.assertEqual(notified_spec.name, 'nr-enroll-stages')

        GLib.test_expect_message('libfprint-device',
            GLib.LogLevelFlags.LEVEL_CRITICAL, '*enroll_stages > 0*')
        notified_spec = None
        self.send_command('SET_ENROLL_STAGES', 0)
        self.assertEqual(self.dev.get_nr_enroll_stages(), 1)
        self.assertIsNone(notified_spec)
        GLib.test_assert_expected_messages_internal('libfprint-device',
            __file__, 0, 'test_change_enroll_stages')

    def test_quick_enroll(self):
        self.send_command('SET_ENROLL_STAGES', 1)
        self.assertEqual(self.dev.get_nr_enroll_stages(), 1)
        matching = self.enroll_print('testprint', FPrint.Finger.LEFT_LITTLE)
        self.assertEqual(matching.get_username(), 'testuser')
        self.assertEqual(matching.get_finger(), FPrint.Finger.LEFT_LITTLE)

    def test_change_scan_type(self):
        notified_spec = None
        def on_scan_type_changed(dev, spec):
            nonlocal notified_spec
            notified_spec = spec

        self.dev.connect('notify::scan-type', on_scan_type_changed)

        for scan_type in [FPrint.ScanType.PRESS, FPrint.ScanType.SWIPE]:
            notified_spec = None
            self.send_auto(scan_type)
            self.assertEqual(self.dev.get_scan_type(), scan_type)
            self.assertEqual(notified_spec.name, 'scan-type')

        GLib.test_expect_message('libfprint-virtual_device',
            GLib.LogLevelFlags.LEVEL_WARNING, '*Scan type*not found')
        notified_spec = None
        self.send_command('SET_SCAN_TYPE', 'eye-contact')
        self.assertEqual(self.dev.get_scan_type(), FPrint.ScanType.SWIPE)
        self.assertIsNone(notified_spec)
        GLib.test_assert_expected_messages_internal('libfprint-device',
            __file__, 0, 'test_change_scan_type')

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
