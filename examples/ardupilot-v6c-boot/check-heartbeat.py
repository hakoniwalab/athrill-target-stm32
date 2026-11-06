#!/usr/bin/env python3
"""Validate ArduCopter HEARTBEAT frames in a raw UART capture, without pymavlink."""

import sys


def heartbeats(data: bytes) -> list[bytes]:
    frames = []
    offset = 0
    while offset < len(data):
        magic = data[offset]
        if magic == 0xFD and offset + 12 <= len(data):
            payload_len = data[offset + 1]
            signed = bool(data[offset + 2] & 1)
            size = 10 + payload_len + 2 + (13 if signed else 0)
            if offset + size <= len(data):
                frame = data[offset:offset + size]
                msgid = frame[7] | (frame[8] << 8) | (frame[9] << 16)
                if (msgid == 0 and frame[5] == 1 and payload_len >= 9
                        and frame[14] == 2 and frame[15] == 3):
                    frames.append(frame)
                offset += size
                continue
        if magic == 0xFE and offset + 8 <= len(data):
            payload_len = data[offset + 1]
            size = 6 + payload_len + 2
            if offset + size <= len(data):
                frame = data[offset:offset + size]
                if (frame[5] == 0 and frame[3] == 1 and payload_len >= 9
                        and frame[10] == 2 and frame[11] == 3):
                    frames.append(frame)
                offset += size
                continue
        offset += 1
    return frames


def main() -> int:
    if len(sys.argv) != 2:
        print(f"usage: {sys.argv[0]} mavlink.raw", file=sys.stderr)
        return 2
    with open(sys.argv[1], "rb") as source:
        frames = heartbeats(source.read())
    if not frames:
        print("FAIL: no ArduCopter HEARTBEAT from system 1", file=sys.stderr)
        return 1
    print(f"PASS: {len(frames)} ArduCopter HEARTBEAT messages from system 1")
    print(f"first: {frames[0].hex()}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
