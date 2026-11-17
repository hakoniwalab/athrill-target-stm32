#!/usr/bin/env python3
"""Acceptance test for examples/attack-serial-control.py with no flight controller:
the driver floods a local pymavlink UDP loopback, and this checks that every emitted
message is a well-formed SERIAL_CONTROL with the configured device, timeout and
RESPOND flag, and that the count matches rate*duration. Exit 0 on pass. Needs pymavlink.

    examples/test-attack-serial-control.py
"""
import importlib.util
import os
import sys
import threading
import time

from pymavlink import mavutil

HERE = os.path.dirname(os.path.abspath(__file__))
PORT = 14757
RATE = 20.0
DURATION = 1.0
DEVICE_INDEX = 0
TIMEOUT_MS = 3000
BAUD = 115200


def load_driver():
    """Load the hyphenated driver script as a module."""
    path = os.path.join(HERE, "attack-serial-control.py")
    spec = importlib.util.spec_from_file_location("attack_serial_control", path)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def main():
    driver = load_driver()
    device = driver.serial_device(DEVICE_INDEX)

    # receiver first (udpin binds and learns the peer from the first packet)
    receiver = mavutil.mavlink_connection(f"udpin:127.0.0.1:{PORT}")
    sender = mavutil.mavlink_connection(f"udpout:127.0.0.1:{PORT}", source_system=255)

    # run the real attack loop against the loopback, in a thread
    sent_holder = {}

    def run():
        sent_holder["n"] = driver.attack(sender, device, TIMEOUT_MS, BAUD, RATE, DURATION,
                                         report=lambda *_: None)

    t = threading.Thread(target=run)
    t.start()

    # collect what arrives for the duration plus a margin to drain
    serial_control = []
    deadline = time.time() + DURATION + 2.0
    while time.time() < deadline:
        msg = receiver.recv_match(blocking=True, timeout=0.2)
        if msg is None:
            if not t.is_alive() and time.time() > deadline - 1.8:
                # attack finished and the buffer looks drained
                extra = receiver.recv_match(blocking=False)
                if extra is None:
                    break
                msg = extra
            else:
                continue
        if msg.get_type() == "SERIAL_CONTROL":
            serial_control.append(msg)
    t.join()

    sent = sent_holder.get("n", 0)
    failures = []

    if sent <= 0:
        failures.append(f"the driver reported sending {sent} messages")
    if len(serial_control) == 0:
        failures.append("no SERIAL_CONTROL message was received")

    for m in serial_control:
        if m.device != device:
            failures.append(f"device {m.device} != {device}")
            break
        if m.timeout != TIMEOUT_MS:
            failures.append(f"timeout {m.timeout} != {TIMEOUT_MS}")
            break
        if not (m.flags & mavutil.mavlink.SERIAL_CONTROL_FLAG_RESPOND):
            failures.append(f"RESPOND flag missing (flags={m.flags})")
            break

    # the count should be near rate*duration (timing tolerance; some may still be in flight)
    expected = RATE * DURATION
    received = len(serial_control)
    if not (0.5 * expected <= received <= 1.5 * expected):
        failures.append(f"received {received} SERIAL_CONTROL, expected ~{expected:.0f}")

    if failures:
        for f in failures:
            print(f"  [FAIL] {f}")
        print("FAIL")
        return 1
    print(f"  [PASS] {received} SERIAL_CONTROL to device {device}, timeout {TIMEOUT_MS}, RESPOND set "
          f"(driver sent {sent})")
    print("PASS")
    return 0


if __name__ == "__main__":
    sys.exit(main())
