#!/usr/bin/env python3

import sys
import os
import os.path
import shutil
import tempfile
import subprocess

if len(sys.argv) != 2:
    print("You need to specify exactly one argument, the directory with test data")

# Check that umockdev is available
try:
    umockdev_version = subprocess.check_output(['umockdev-run', '--version'])
    version = tuple(int(_) for _ in umockdev_version.split(b'.'))
    if version < (0, 13, 2):
        print('umockdev is too old for test to be reliable, expect random failures!')
        print('Please update umockdev to at least 0.13.2.')
except FileNotFoundError:
    print('umockdev-run not found, skipping test!')
    print('Please install umockdev.')
    sys.exit(77)

edir = os.path.dirname(sys.argv[0])
ddir = sys.argv[1]

tmpdir = tempfile.mkdtemp(prefix='libfprint-umockdev-test-')

assert os.path.isdir(ddir)
assert os.path.isfile(os.path.join(ddir, "device"))

def cmp_pngs(png_a, png_b):
    print("Comparing PNGs %s and %s" % (png_a, png_b))
    import cairo
    img_a = cairo.ImageSurface.create_from_png(png_a)
    img_b = cairo.ImageSurface.create_from_png(png_b)

    assert img_a.get_format() == cairo.FORMAT_RGB24
    assert img_b.get_format() == cairo.FORMAT_RGB24
    assert img_a.get_width() == img_b.get_width()
    assert img_a.get_height() == img_b.get_height()
    assert img_a.get_stride () == img_b.get_stride()

    data_a = img_a.get_data()
    data_b = img_b.get_data()
    stride = img_a.get_stride()

    for x in range(img_a.get_width()):
        for y in range(img_a.get_height()):
            assert(data_a[y * stride + x * 4] == data_b[y * stride + x * 4])

def capture():
    ioctl = os.path.join(ddir, "capture.ioctl")
    device = os.path.join(ddir, "device")
    dev = open(ioctl).readline().strip()
    assert dev.startswith('@DEV ')
    dev = dev[5:]

    subprocess.check_call(['umockdev-run', '-d', device,
                                           '-i', "%s=%s" % (dev, ioctl),
                                           '--',
                                           '%s' % os.path.join(edir, "capture.py"),
                                             '%s' % os.path.join(tmpdir, "capture.png")])

    assert os.path.isfile(os.path.join(tmpdir, "capture.png"))
    if os.path.isfile(os.path.join(ddir, "capture.png")):
        # Compare the images, they need to be identical
        cmp_pngs(os.path.join(tmpdir, "capture.png"), os.path.join(ddir, "capture.png"))

def custom():
    ioctl = os.path.join(ddir, "custom.ioctl")
    device = os.path.join(ddir, "device")
    dev = open(ioctl).readline().strip()
    assert dev.startswith('@DEV ')
    dev = dev[5:]

    subprocess.check_call(['umockdev-run', '-d', device,
                                           '-i', "%s=%s" % (dev, ioctl),
                                           '--',
                                           '%s' % os.path.join(ddir, "custom.py")])

try:
    if os.path.exists(os.path.join(ddir, "capture.ioctl")):
        capture()

    if os.path.exists(os.path.join(ddir, "custom.ioctl")):
        custom()

finally:
    shutil.rmtree(tmpdir)

