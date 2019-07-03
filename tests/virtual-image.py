#!/usr/bin/env python3


import gi
gi.require_version('FPrint', '2.0')
from gi.repository import FPrint, GLib, Gio

import os
import sys
import unittest
import socket
import struct
import shutil
import glob
import cairo
import tempfile

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

def load_image(img):
    png = cairo.ImageSurface.create_from_png(img)

    # Cairo wants 4 byte aligned rows, so just add a few pixel if necessary
    w = png.get_width()
    h = png.get_height()
    w = (w + 3) // 4 * 4
    h = (h + 3) // 4 * 4
    img = cairo.ImageSurface(cairo.Format.A8, w, h)
    cr = cairo.Context(img)

    cr.set_source_rgba(1, 1, 1, 1)
    cr.paint()

    cr.set_source_rgba(0, 0, 0, 0)
    cr.set_operator(cairo.OPERATOR_SOURCE)

    cr.set_source_surface(png)
    cr.paint()

    return img

if hasattr(os.environ, 'MESON_SOURCE_ROOT'):
    root = os.environ['MESON_SOURCE_ROOT']
else:
    root = os.path.join(os.path.dirname(__file__), '..')

imgdir = os.path.join(root, 'examples', 'prints')

ctx = GLib.main_context_default()

class VirtualImage(unittest.TestCase):

    @classmethod
    def setUpClass(cls):
        cls.tmpdir = tempfile.mkdtemp(prefix='libfprint-')

        cls.sockaddr = os.path.join(cls.tmpdir, 'virtual-image.socket')
        os.environ['FP_VIRTUAL_IMAGE'] = cls.sockaddr

        cls.ctx = FPrint.Context()

        cls.dev = None
        for dev in cls.ctx.get_devices():
            # We might have a USB device in the test system that needs skipping
            if dev.get_driver() == 'virtual_image':
                cls.dev = dev
                break

        assert cls.dev is not None, "You need to compile with virtual_image for testing"

        cls.prints = {}
        for f in glob.glob(os.path.join(imgdir, '*.png')):
            n = os.path.basename(f)[:-4]
            cls.prints[n] = load_image(f)

    @classmethod
    def tearDownClass(cls):
        shutil.rmtree(cls.tmpdir)

    def setUp(self):
        self.dev.open_sync()

    def tearDown(self):
        self.dev.close_sync()

    def report_finger(self, state):
        with Connection(self.sockaddr) as con:
            con.write(struct.pack('ii', -1, 1 if state else 0))

    def send_image(self, image):
        img = self.prints[image]
        with Connection(self.sockaddr) as con:
            mem = img.get_data()
            mem = mem.tobytes()
            assert len(mem) == img.get_width() * img.get_height()

            encoded_img = struct.pack('ii', img.get_width(), img.get_height())
            encoded_img += mem

            con.sendall(encoded_img)

    def test_capture_prevents_close(self):
        cancel = Gio.Cancellable()
        def cancelled_cb(dev, res, obj):
            print("Capture operation finished")
            with self.assertRaises(GLib.GError) as cm:
                dev.capture_finish(res)
            assert cm.exception.matches(Gio.io_error_quark(), Gio.IOErrorEnum.CANCELLED)
            print("Capture cancelled as expected")
            obj._cancelled = True

        self._cancelled = False
        self.dev.capture(True, cancel, cancelled_cb, self)

        with self.assertRaises(GLib.GError) as cm:
            self.dev.close_sync()
        assert cm.exception.matches(FPrint.device_error_quark(), FPrint.DeviceError.BUSY)

        cancel.cancel()
        while not self._cancelled:
            ctx.iteration(True)

    def enroll_print(self, image):
        self._step = 0
        self._enrolled = None

        def progress_cb(dev, step, fp, user_data):
            print('Print was processed, continuing')
            self._step = step

        def done_cb(dev, res):
            print("Enroll done")
            fp = dev.enroll_finish(res)
            self._enrolled = fp

        template = FPrint.Print.new(self.dev)
        template.props.finger = FPrint.Finger.LEFT_THUMB
        template.props.username = "testuser"
        template.props.description = "test print"
        datetime = GLib.DateTime.new_now_local()
        date = GLib.Date()
        date.set_dmy(*datetime.get_ymd()[::-1])
        template.props.enroll_date = date
        self.dev.enroll(template, None, progress_cb, tuple(), done_cb)

        # Note: Assumes 5 enroll steps for this device!
        self.send_image(image)
        while self._step < 1:
            ctx.iteration(True)

        self.send_image(image)
        while self._step < 2:
            ctx.iteration(True)

        self.send_image(image)
        while self._step < 3:
            ctx.iteration(True)

        self.send_image(image)
        while self._step < 4:
            ctx.iteration(True)

        self.send_image(image)
        while self._enrolled is None:
            ctx.iteration(True)

        return self._enrolled

    def test_enroll_verify(self):
        done = False

        def verify_cb(dev, res):
            match, fp = dev.verify_finish(res)
            self._verify_match = match
            self._verify_fp = fp

        fp_whorl = self.enroll_print('whorl')

        self._verify_match = None
        self._verify_fp = None
        self.dev.verify(fp_whorl, None, verify_cb)
        self.send_image('whorl')
        while self._verify_match is None:
            ctx.iteration(True)
        assert(self._verify_match)

        self._verify_match = None
        self._verify_fp = None
        self.dev.verify(fp_whorl, None, verify_cb)
        self.send_image('tented_arch')
        while self._verify_match is None:
            ctx.iteration(True)
        assert(not self._verify_match)

    def test_identify(self):
        done = False

        def verify_cb(dev, res):
            r, fp = dev.verify_finish(res)
            self._verify_match = r
            self._verify_fp = fp

        fp_whorl = self.enroll_print('whorl')
        fp_tented_arch = self.enroll_print('tented_arch')

        def identify_cb(dev, res):
            print('Identify finished')
            self._identify_match, self._identify_fp = self.dev.identify_finish(res)

        self._identify_fp = None
        self.dev.identify([fp_whorl, fp_tented_arch], None, identify_cb)
        self.send_image('tented_arch')
        while self._identify_fp is None:
            ctx.iteration(True)
        assert(self._identify_match is fp_tented_arch)

        self._identify_fp = None
        self.dev.identify([fp_whorl, fp_tented_arch], None, identify_cb)
        self.send_image('whorl')
        while self._identify_fp is None:
            ctx.iteration(True)
        assert(self._identify_match is fp_whorl)

    def test_verify_serialized(self):
        done = False

        def verify_cb(dev, res):
            r, fp = dev.verify_finish(res)
            self._verify_match = r
            self._verify_fp = fp

        fp_whorl = self.enroll_print('whorl')

        fp_data = fp_whorl.serialize()
        fp_whorl_new = FPrint.Print.deserialize(fp_data)

        # The serialized/deserialized prints need to be equal
        assert fp_whorl.equal(fp_whorl_new)

        datetime = GLib.DateTime.new_now_local()
        date = GLib.Date()
        date.set_dmy(*datetime.get_ymd()[::-1])

        assert fp_whorl_new.props.username == "testuser"
        assert fp_whorl_new.props.description == "test print"
        assert fp_whorl_new.props.finger == FPrint.Finger.LEFT_THUMB
        assert date.compare(fp_whorl_new.props.enroll_date) == 0

        self._verify_match = None
        self._verify_fp = None
        self.dev.verify(fp_whorl_new, None, verify_cb)
        self.send_image('whorl')
        while self._verify_match is None:
            ctx.iteration(True)
        assert(self._verify_match)

        self._verify_match = None
        self._verify_fp = None
        self.dev.verify(fp_whorl_new, None, verify_cb)
        self.send_image('tented_arch')
        while self._verify_match is None:
            ctx.iteration(True)
        assert(not self._verify_match)


# avoid writing to stderr
unittest.main(testRunner=unittest.TextTestRunner(stream=sys.stdout, verbosity=2))

