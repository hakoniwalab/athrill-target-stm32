#!/usr/bin/env python3
"""Writes an ArduPilot parameter file (NAME,VALUE lines) over MAVLink, then reads every parameter back and compares.

    load-params.py <file.parm> [tcp:127.0.0.1:5760]

All PARAM_SETs are sent back to back (twice), then one PARAM_REQUEST_LIST checks them: on the emulated 57600 bit/s
telemetry link this takes seconds, where waiting for each reply would take minutes. Exit status 1 if a parameter is
missing or has another value. Needs pymavlink.
"""
import sys
import time

from pymavlink import mavutil

SYSTEM = 1
COMPONENT = 1


def read_parm(path):
    params = []
    for line in open(path):
        line = line.strip()
        if not line or line.startswith("#"):
            continue
        name, value = line.split(",")[:2]
        params.append((name.strip(), float(value)))
    return params


def quiet(m):
    """Stops the telemetry streams, so that the parameter list has the link to itself."""
    for _ in range(3):
        m.mav.request_data_stream_send(SYSTEM, COMPONENT, mavutil.mavlink.MAV_DATA_STREAM_ALL, 0, 0)
        time.sleep(0.3)
    while m.recv_match(blocking=False) is not None:
        pass


def wait_ready(m, timeout_sec=120):
    """Waits until ArduPilot has finished booting (its heartbeat reports MAV_STATE_STANDBY): parameters written
    during the boot are not all taken."""
    t0 = time.time()
    while time.time() - t0 < timeout_sec:
        r = m.recv_match(type="HEARTBEAT", blocking=True, timeout=1)
        if r is None or r.get_srcSystem() != SYSTEM:
            continue
        if r.system_status == mavutil.mavlink.MAV_STATE_STANDBY:
            print(f"ArduPilot ready after {time.time() - t0:.0f} s", flush=True)
            return
    print(f"ArduPilot not in standby after {timeout_sec} s; going on", flush=True)


def read_missing(m, names, values):
    """Reads one by one what the list did not bring."""
    for name in names:
        if name in values:
            continue
        for _ in range(3):
            m.mav.param_request_read_send(SYSTEM, COMPONENT, name.encode(), -1)
            t0 = time.time()
            while time.time() - t0 < 5:
                r = m.recv_match(type="PARAM_VALUE", blocking=True, timeout=1)
                if r is not None:
                    values[r.param_id] = r.param_value
                if name in values:
                    break
            if name in values:
                break


def read_all(m):
    m.mav.param_request_list_send(SYSTEM, COMPONENT)
    values = {}
    count = None
    last = time.time()
    while time.time() - last < 20:
        r = m.recv_match(type="PARAM_VALUE", blocking=True, timeout=1)
        if r is None:
            continue
        values[r.param_id] = r.param_value
        count = r.param_count
        last = time.time()
        if len(values) >= count:
            break
    return values, count


def main():
    params = read_parm(sys.argv[1])
    url = sys.argv[2] if len(sys.argv) > 2 else "tcp:127.0.0.1:5760"
    m = mavutil.mavlink_connection(url, source_system=255)
    m.wait_heartbeat(timeout=300)
    wait_ready(m)
    quiet(m)
    for _ in range(2):
        for name, value in params:
            m.mav.param_set_send(SYSTEM, COMPONENT, name.encode(), value, mavutil.mavlink.MAV_PARAM_TYPE_REAL32)
            time.sleep(0.05)
        time.sleep(5)
    quiet(m)
    values, count = read_all(m)
    print(f"read {len(values)} of {count} parameters in the list", flush=True)
    read_missing(m, [name for name, _ in params], values)
    wrong = []
    for name, value in params:
        got = values.get(name)
        if got is None or abs(got - value) > max(1e-5, abs(value) * 1e-5):
            wrong.append((name, value, got))
    for name, value, got in wrong:
        print(f"NOT SET {name}: want {value}, have {got}")
    print(f"set {len(params) - len(wrong)} of {len(params)}")
    sys.exit(1 if wrong else 0)


if __name__ == "__main__":
    main()
