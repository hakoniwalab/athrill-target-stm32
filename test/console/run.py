#!/usr/bin/env python3
"""Console input from Athrill's debugger (athrill-target-stm32#64):

    run.py <athrill3> <device_config> [timeout]

Runs console.elf interactively (-i): "cont 1000000" runs it past READY (its
receiver on) and stops; once stopped ("CONT TIMEOUT"; the debugger takes
commands only while the CPU is stopped), "serial 0 AB" and "cont".
The firmware prints READY on USART3 and echoes each received character as
"<c>": the test passes when "<A><B>" follows READY, i.e. both characters
went from the debugger through the console UART model to the firmware, in
order. athrill3 is stopped as soon as the result is known.
"""
import os
import selectors
import subprocess
import sys
import time


def main():
    if len(sys.argv) < 3:
        print(__doc__, file=sys.stderr)
        return 2
    athrill, config = sys.argv[1], sys.argv[2]
    timeout = float(sys.argv[3]) if len(sys.argv) > 3 else 30.0
    here = os.path.dirname(os.path.abspath(__file__))
    process = subprocess.Popen(
        [athrill, "-c1", "-i", "-m", os.path.join(here, "memory.txt"), "-d", config,
         os.path.join(here, "console.elf")],
        stdin=subprocess.PIPE, stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    process.stdin.write(b"cont 1000000\n")
    process.stdin.flush()
    typed = False
    selector = selectors.DefaultSelector()
    selector.register(process.stdout, selectors.EVENT_READ)
    output = b""
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline and b"<A><B>" not in output:
        if not selector.select(timeout=0.2):
            if process.poll() is not None:
                break
            continue
        chunk = process.stdout.read1(65536)
        if not chunk:
            break
        output += chunk
        if not typed and b"READY" in output and b"CONT TIMEOUT" in output:
            process.stdin.write(b"serial 0 AB\ncont\n")
            process.stdin.flush()
            typed = True
    if process.poll() is None:
        process.kill()
    process.wait()
    text = output.decode(errors="replace")
    ready = text.find("READY")
    echo = text.find("<A><B>")
    print(text[-400:])
    if ready >= 0 and echo > ready:
        print("PASS")
        return 0
    print("FAIL: expected READY then <A><B>")
    return 1


if __name__ == "__main__":
    sys.exit(main())
