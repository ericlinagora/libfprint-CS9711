#!/usr/bin/python3

import traceback
import sys
import time
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

d.open_sync()

assert d.get_driver() == "egismoc"
assert not d.has_feature(FPrint.DeviceFeature.CAPTURE)
assert d.has_feature(FPrint.DeviceFeature.IDENTIFY)
assert d.has_feature(FPrint.DeviceFeature.VERIFY)
assert d.has_feature(FPrint.DeviceFeature.DUPLICATES_CHECK)
assert d.has_feature(FPrint.DeviceFeature.STORAGE)
assert d.has_feature(FPrint.DeviceFeature.STORAGE_LIST)
assert d.has_feature(FPrint.DeviceFeature.STORAGE_DELETE)
assert d.has_feature(FPrint.DeviceFeature.STORAGE_CLEAR)

def enroll_progress(*args):
    print("finger status: ", d.get_finger_status())
    print('enroll progress: ' + str(args))

def identify_done(dev, res):
    global identified
    identified = True
    identify_match, identify_print = dev.identify_finish(res)
    print('indentification_done: ', identify_match, identify_print)
    assert identify_match.equal(identify_print)

# Beginning with list and clear assumes you begin with >0 prints enrolled before capturing

print("listing - device should have prints")
stored = d.list_prints_sync()
assert len(stored) > 0
del stored

print("clear device storage")
d.clear_storage_sync()
print("clear done")

print("listing - device should be empty")
stored = d.list_prints_sync()
assert len(stored) == 0
del stored

print("enrolling")
template = FPrint.Print.new(d)
template.set_finger(FPrint.Finger.LEFT_INDEX)
assert d.get_finger_status() == FPrint.FingerStatusFlags.NONE
p1 = d.enroll_sync(template, None, enroll_progress, None)
assert d.get_finger_status() == FPrint.FingerStatusFlags.NONE
print("enroll done")
del template

print("listing - device should have 1 print")
stored = d.list_prints_sync()
assert len(stored) == 1
assert stored[0].equal(p1)

print("verifying")
assert d.get_finger_status() == FPrint.FingerStatusFlags.NONE
verify_res, verify_print = d.verify_sync(p1)
assert d.get_finger_status() == FPrint.FingerStatusFlags.NONE
print("verify done")
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

print("try to enroll duplicate")
template = FPrint.Print.new(d)
template.set_finger(FPrint.Finger.RIGHT_INDEX)
assert d.get_finger_status() == FPrint.FingerStatusFlags.NONE
try:
    d.enroll_sync(template, None, enroll_progress, None)
except GLib.Error as error:
    assert error.matches(FPrint.DeviceError.quark(),
                         FPrint.DeviceError.DATA_DUPLICATE)
except Exception as exc:
    raise
assert d.get_finger_status() == FPrint.FingerStatusFlags.NONE
print("duplicate enroll attempt done")

print("listing - device should still only have 1 print")
stored = d.list_prints_sync()
assert len(stored) == 1
assert stored[0].equal(p1)
del stored

print("enroll new finger")
template = FPrint.Print.new(d)
template.set_finger(FPrint.Finger.RIGHT_INDEX)
assert d.get_finger_status() == FPrint.FingerStatusFlags.NONE
p2 = d.enroll_sync(template, None, enroll_progress, None)
assert d.get_finger_status() == FPrint.FingerStatusFlags.NONE
print("enroll new finger done")
del template

print("listing - device should have 2 prints")
stored = d.list_prints_sync()
assert len(stored) == 2
assert (stored[0].equal(p1) and stored[1].equal(p2)) or (stored[0].equal(p2) and stored[1].equal(p1))
del stored

print("deleting first print")
d.delete_print_sync(p1)
print("delete done")
del p1

print("listing - device should only have second print")
stored = d.list_prints_sync()
assert len(stored) == 1
assert stored[0].equal(p2)
del stored
del p2

print("clear device storage")
d.clear_storage_sync()
print("clear done")

print("listing - device should be empty")
stored = d.list_prints_sync()
assert len(stored) == 0
del stored

d.close_sync()

del d
del c
