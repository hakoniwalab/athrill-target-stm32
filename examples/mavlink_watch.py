"""MAVLink continuity for the flight scripts (fly.py, fly-motor-fault.py).

A firmware or a model that stops sending MAVLink must fail the flight at once, not time out or pass on the
messages it sent before (athrill-device#102: the 6C's MAVLink TX stopped after a while). The scripts call
`seen(time_boot_ms)` on every position message and `idle()` when a wait returned nothing:
- two position messages more than 2 s apart in the vehicle's time fail the flight (the scripts ask for them at
  4 Hz, and the firmware sends them at that rate or faster);
- no position message for 60 s of wall time fails it (a stopped link gives no next message to measure a gap with).
"""
import sys
import time

MAX_GAP_MS = 2000
MAX_SILENCE_SEC = 60.0


class PositionWatch:
    def __init__(self):
        self.last_ms = None
        self.last_wall = time.time()

    def seen(self, time_boot_ms):
        if self.last_ms is not None and time_boot_ms - self.last_ms > MAX_GAP_MS:
            sys.exit(f"FAIL: MAVLink gap: no position message from {self.last_ms / 1000.0:.2f} s "
                     f"to {time_boot_ms / 1000.0:.2f} s of vehicle time")
        self.last_ms = time_boot_ms
        self.last_wall = time.time()

    def idle(self):
        if time.time() - self.last_wall > MAX_SILENCE_SEC:
            sys.exit(f"FAIL: MAVLink stopped: no position message for {MAX_SILENCE_SEC:.0f} s (wall time)")
