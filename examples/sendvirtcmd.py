#!/usr/bin/env python3

# This script can be used together with the virtual_imgdev to simulate an
# image based fingerprint reader.
#
# To use, set the FP_VIRTUAL_IMAGE environment variable for both the
# libfprint using program (e.g. fprintd) and this script.
#
# Usually this would work by adding it into the systemd unit file. The
# best way of doing so is to create
#  /etc/systemd/system/fprintd.service.d/fprintd-test.conf
#
# [Service]
# RuntimeDirectory=fprint
# Environment=FP_VIRTUAL_DEVICE=/run/fprint/virtdev_sock
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
# FP_VIRTUAL_DEVICE=/run/fprint/virtdev_sock ./sendvirtimg.py "ADD <username> <finger> <success|failure>"



import cairo
import sys
import os
import socket
import struct

if len(sys.argv) != 2:
    sys.stderr.write('You need to pass commands!\n')
    sys.stderr.write('Usage: ./sendvirtimg.py "ADD <finger> <username> <success|failure>"\n')
    sys.exit(1)

command = sys.argv[1]

# Send image through socket
sockaddr = os.environ['FP_VIRTUAL_DEVICE']
if not sockaddr:
    sockaddr = os.environ['FP_VIRTUAL_DEVICE_IDENT']

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(sockaddr)

sock.sendall(command.encode('utf-8'))

