#!/usr/bin/env python3

# This script can be used together with the virtual_misdev to simulate an
# match-in-sensor device with internal storage.
#
# To use, set the FP_VIRTUAL_MISDEV environment variable for both the
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
# FP_VIRTUAL_IMGDEV=/run/fprint/virtimg_sock ./virtmissensor.py /tmp/storage
#
# Please note that the storage file should be pre-created with a few lines
# Each line represents a slot, if a print is stored, then it will contain a
# UUID (defined by the driver) and a matching string to identify it again.
# Note that the last slot line should not end with a \n

import sys
import os
import socket
import struct

import argparse

parser = argparse.ArgumentParser(description='Play virtual fingerprint device with internal storage.')
parser.add_argument('storage', metavar='storage', type=argparse.FileType('r+'),
                    help='The "storage" database (one line per slot)')
parser.add_argument('-e', dest='enroll', type=str,
                    help='Enroll a print using the string as identifier')
parser.add_argument('-v', dest='verify', type=str,
                    help='Verify print if the stored identifier matches the given identifier')
parser.add_argument('-d', dest='delete', action='store_const', const=True,
                    help='Delete print as requested by driver')

args = parser.parse_args()

cnt = 0
if args.enroll:
    cnt += 1
if args.verify:
    cnt += 1
if args.delete:
    cnt += 1

assert cnt == 1, 'You need to give exactly one command argument, -e or -v'

prints = []
for slot in args.storage.read().split('\n'):
    split = slot.split(' ', 1)
    if len(split) == 2:
        prints.append(split)
    else:
        prints.append(None)


# Send image through socket
sockaddr = os.environ['FP_VIRTUAL_MISDEV']

sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
sock.connect(sockaddr)

# Assume we get a full message
msg = sock.recv(1024)
assert(msg[-1] == ord(b'\n'))

if args.enroll:
    if not msg.startswith(b'ENROLL '):
        sys.stderr.write('Expected to enroll, but driver is not ready for enrolling (%s)\n' % str(msg.split(b' ', 1)[0]))
        sys.exit(1)
    uuid = msg[7:-1].decode('utf-8')

    for slot in prints:
        if slot is not None and slot[0] == uuid:
            sock.sendall(b'2\n') # ENROLL_FAIL
            sys.stderr.write('Failed to enroll; UUID has already been stored!\n')
            sys.exit(1)

    # Find an empty slot
    for i, slot in enumerate(prints):
        if slot is not None:
            continue

        prints[i] = (uuid, args.enroll)
        sock.sendall(b'1\n') # ENROLL_COMPLETE
        break
    else:
        # TODO: 2: ENROLL_FAIL, but we should send no empty slot!
        sock.sendall(b'2\n') # ENROLL_FAIL
        sys.stderr.write('Failed to enroll, no free slots!\n')
        sys.exit(1)

elif args.verify:
    if not msg.startswith(b'VERIFY '):
        sys.stderr.write('Expected to verify, but driver is not ready for verifying (%s)\n' % str(msg.split(b' ', 1)[0]))
        sys.exit(1)
    uuid = msg[7:-1].decode('utf-8')

    for slot in prints:
        if slot is not None and slot[0] == uuid:
            if slot[1] == args.verify:
                sock.sendall(b'1\n') # VERIFY_MATCH
            else:
                sock.sendall(b'0\n') # VERIFY_NO_MATCH
            sys.exit(0)
    else:
        sys.stderr.write('Slot ID is unknown, returning error\n')
        sock.sendall(b'-1') # error, need way to report that print is unkown

elif args.delete:
    if not msg.startswith(b'DELETE '):
        sys.stderr.write('Expected to delete, but driver is not ready for deleting (%s)\n' % str(msg.split(b' ', 1)[0]))
        sys.exit(1)
    uuid = msg[7:-1].decode('utf-8')

    for i, slot in enumerate(prints):
        if slot is not None and slot[0] == uuid:
            if slot[0] == uuid:
                prints[i] = None
                sock.sendall(b'0\n') # DELETE_COMPLETE
                break
    else:
        sys.stderr.write('Slot ID is unknown, just report back complete\n')
        sock.sendall(b'0') # DELETE_COMPLETE

prints_str = '\n'.join('' if p is None else '%s %s' % (p[0], p[1]) for p in prints)
prints_human_str = '\n'.join('empty slot' if p is None else '%s %s' % (p[0], p[1]) for p in prints)

print('Prints stored now:')
print(prints_human_str)
args.storage.seek(0)
args.storage.truncate()
args.storage.write(prints_str)
