#!/usr/bin/python3

import traceback
import sys
import gi

gi.require_version('FPrint', '2.0')
from gi.repository import FPrint, GLib

# Exit with error on any exception, included those happening in async callbacks
sys.excepthook = lambda *args: (traceback.print_exception(*args), sys.exit(1))

ctx = GLib.main_context_default()

c = FPrint.Context()
c.enumerate()
devices = c.get_devices()

d = devices[0]
del devices

assert d.get_driver() == "realtek"
assert not d.has_feature(FPrint.DeviceFeature.CAPTURE)
assert d.has_feature(FPrint.DeviceFeature.IDENTIFY)
assert d.has_feature(FPrint.DeviceFeature.VERIFY)
assert not d.has_feature(FPrint.DeviceFeature.DUPLICATES_CHECK)
assert d.has_feature(FPrint.DeviceFeature.STORAGE)
assert d.has_feature(FPrint.DeviceFeature.STORAGE_LIST)
assert d.has_feature(FPrint.DeviceFeature.STORAGE_DELETE)
assert d.has_feature(FPrint.DeviceFeature.STORAGE_CLEAR)

d.open_sync()

# 1. verify clear storage command, 2. make sure later asserts are good
d.clear_storage_sync()

template = FPrint.Print.new(d)

def enroll_progress(*args):
    # assert d.get_finger_status() & FPrint.FingerStatusFlags.NEEDED
    print('enroll progress: ' + str(args))

def identify_done(dev, res):
    global identified
    identified = True
    try:
        identify_match, identify_print = dev.identify_finish(res)
    except gi.repository.GLib.GError as e:
        print("Please try again")
    else:
        print('indentification_done: ', identify_match, identify_print)
        assert identify_match.equal(identify_print)

def start_identify_async(prints):
    global identified
    print('async identifying')
    d.identify(prints, callback=identify_done)
    del prints

    while not identified:
        ctx.iteration(True)

    identified = False

# List, enroll, list, verify, identify, delete
print("enrolling")
assert d.get_finger_status() == FPrint.FingerStatusFlags.NONE
p = d.enroll_sync(template, None, enroll_progress, None)
assert d.get_finger_status() == FPrint.FingerStatusFlags.NONE
print("enroll done")

print("listing")
stored = d.list_prints_sync()
print("listing done")
assert len(stored) == 1
assert stored[0].equal(p)
print("verifying")
try:
    assert d.get_finger_status() == FPrint.FingerStatusFlags.NONE
    verify_res, verify_print = d.verify_sync(p)
    assert d.get_finger_status() == FPrint.FingerStatusFlags.NONE
except gi.repository.GLib.GError as e:
    print("Please try again")
else:
    print("verify done")
    del p
    assert verify_res == True

identified = False
deserialized_prints = []
for p in stored:
    deserialized_prints.append(FPrint.Print.deserialize(p.serialize()))
    assert deserialized_prints[-1].equal(p)
del stored

print('async identifying')
d.identify(deserialized_prints, callback=identify_done)
del deserialized_prints

while not identified:
    ctx.iteration(True)

print("deleting")
d.delete_print_sync(p)
print("delete done")

d.close_sync()

del d
del c
