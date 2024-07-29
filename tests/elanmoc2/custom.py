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

assert d.get_driver() == "elanmoc2"
assert not d.has_feature(FPrint.DeviceFeature.CAPTURE)
assert d.has_feature(FPrint.DeviceFeature.IDENTIFY)
assert d.has_feature(FPrint.DeviceFeature.VERIFY)
assert d.has_feature(FPrint.DeviceFeature.DUPLICATES_CHECK)
assert d.has_feature(FPrint.DeviceFeature.STORAGE)
assert d.has_feature(FPrint.DeviceFeature.STORAGE_LIST)
assert d.has_feature(FPrint.DeviceFeature.STORAGE_DELETE)
assert d.has_feature(FPrint.DeviceFeature.STORAGE_CLEAR)

d.open_sync()

template = FPrint.Print.new(d)
template.set_finger(FPrint.Finger.LEFT_INDEX)


def dump_print(p: FPrint.Print):
    print("Type: ", p.get_property("fpi-type"))
    print("Finger: ", p.get_finger())
    print("Driver: ", p.get_driver())
    print("Device ID: ", p.get_device_id())
    print("FPI data: ", p.get_property("fpi-data"))
    print("User ID: ", bytes(p.get_property("fpi-data")[1]).decode("utf-8"))
    print("Description: ", p.get_description())
    print("Enroll date: ", p.get_enroll_date())
    print()


def enroll_progress(*args):
    print("finger status: ", d.get_finger_status())
    print('enroll progress: ' + str(args))


def identify_done(dev, res):
    global identified
    identified = True
    identify_match, identify_print = dev.identify_finish(res)
    print('identification done: ', identify_match, identify_print)
    assert identify_match.equal(identify_print)


print("clearing device storage")
d.clear_storage_sync()

print("ensuring device storage is empty")
stored = d.list_prints_sync()
assert len(stored) == 0

print("enrolling one finger")
assert d.get_finger_status() == FPrint.FingerStatusFlags.NONE
enrolled = d.enroll_sync(template, None, enroll_progress, None)
assert d.get_finger_status() == FPrint.FingerStatusFlags.NONE
print("enroll done")
del template

# Verify before listing since the device may not be in a good mood
print("verifying the enrolled finger")
assert d.get_finger_status() == FPrint.FingerStatusFlags.NONE
verify_res, verify_print = d.verify_sync(enrolled)
assert d.get_finger_status() == FPrint.FingerStatusFlags.NONE
print("verify done")
assert verify_res

print("ensuring device storage has the enrolled finger")
stored = d.list_prints_sync()
assert len(stored) == 1
assert stored[0].equal(enrolled)
del enrolled
del verify_print

print("attempting to enroll the same finger again")
template = FPrint.Print.new(d)
template.set_finger(FPrint.Finger.LEFT_INDEX)
assert d.get_finger_status() == FPrint.FingerStatusFlags.NONE
new_enrolled = d.enroll_sync(template, None, enroll_progress, None)
assert d.get_finger_status() == FPrint.FingerStatusFlags.NONE
print("enroll done")

print("ensuring device storage has the enrolled finger")
stored = d.list_prints_sync()
assert len(stored) == 1
assert stored[0].equal(new_enrolled)

print("enrolling another finger")
template: FPrint.Print = FPrint.Print.new(d)
template.set_finger(FPrint.Finger.RIGHT_LITTLE)
assert d.get_finger_status() == FPrint.FingerStatusFlags.NONE
enrolled2 = d.enroll_sync(template, None, enroll_progress, None)
assert d.get_finger_status() == FPrint.FingerStatusFlags.NONE
print("enroll done")
del template

print("verifying the enrolled finger")
assert d.get_finger_status() == FPrint.FingerStatusFlags.NONE
verify_res, verify_print = d.verify_sync(enrolled2)
assert d.get_finger_status() == FPrint.FingerStatusFlags.NONE
print("verify done")
assert verify_res
del verify_print

print("ensuring device storage has both enrolled fingers")
stored = d.list_prints_sync()
assert len(stored) == 2
for p in stored:
    assert p.equal(new_enrolled) or p.equal(enrolled2)

print("identifying the enrolled fingers")
identified = False
deserialized_prints = []
for p in stored:
    deserialized_prints.append(FPrint.Print.deserialize(p.serialize()))
    assert deserialized_prints[-1].equal(p)
del stored
del p

d.identify(deserialized_prints, callback=identify_done)
del deserialized_prints

while not identified:
    ctx.iteration(True)

print("delete the first enrolled finger")
d.delete_print_sync(new_enrolled)

print("ensuring device storage has only the second enrolled finger")
stored = d.list_prints_sync()
assert len(stored) == 1
assert stored[0].equal(enrolled2)

print("delete the second enrolled finger")
d.delete_print_sync(enrolled2)

print("ensuring device storage is empty")
stored = d.list_prints_sync()
assert len(stored) == 0

del stored
del enrolled2
del new_enrolled
d.close_sync()
del d
del c
del ctx
