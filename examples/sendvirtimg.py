#!/usr/bin/env python3

# This script can be used together with the virtual_imgdev to simulate an
# image based fingerprint reader.
#
# To use, set the FP_VIRTUAL_IMGDEV environment variable for both the
# libfprint using program (e.g. fprintd) and this script.
#
# Usually this would work by adding it into the systemd unit file. The
# best way of doing so is to create
#  /etc/systemd/system/fprintd.service.d/fprintd-test.conf
#
# [Service]
# RuntimeDirectory=fprint
# Environment=FP_VIRTUAL_IMGDEV=/run/fprint/virtimg_sock
# Environment=G_MESSAGES_DEBUG=all
# ReadWritePaths=$RUNTIME_DIR
#
# After that run:
#
#   systemctl daemon-reload
#   systemctl restart fprintd.service
#
# You may also need to disable selinux.
#
# Then run this script with e.g.
# FP_VIRTUAL_IMGDEV=/run/fprint/virtimg_sock ./sendvirtimg.py prints/whorl.png



import cairo
import sys
import os
import socket
import struct

if len(sys.argv) == 2:
    png = cairo.ImageSurface.create_from_png(sys.argv[1])

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
else:
    sys.stderr.write('You need to pass a PNG with an alpha channel!\n')
    sys.exit(1)

def write_dbg_img():
    dbg_img_rgb = cairo.ImageSurface(cairo.Format.RGB24, img.get_width(), img.get_height())
    dbg_cr = cairo.Context(dbg_img_rgb)
    dbg_cr.set_source_rgb(0, 0, 0)
    dbg_cr.paint()
    dbg_cr.set_source_rgb(1, 1, 1)
    dbg_cr.mask_surface(img, 0, 0)

    dbg_img_rgb.write_to_png('/tmp/test.png')

#write_dbg_img()

# Send image through socket
sockaddr = os.environ['FP_VIRTUAL_IMGDEV']

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(sockaddr)

mem = img.get_data()
mem = mem.tobytes()
assert len(mem) == img.get_width() * img.get_height()

encoded_img = struct.pack('ii', img.get_width(), img.get_height())
encoded_img += mem

sock.sendall(encoded_img)

