#!/usr/bin/env python3
"""Capture the device's boot log over USB CDC.

Resets the board and prints whatever it says for a few seconds — the ESP-IDF
boot banner, our own Serial.println output, and any panic/backtrace. Read-only:
it never writes flash.

    ~/.platformio/penv/bin/python tools/serial-log.py [--seconds 10] [--port P]
"""
import argparse
import glob
import sys
import time

import serial


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port")
    ap.add_argument("--seconds", type=float, default=10.0)
    ap.add_argument("--no-reset", action="store_true",
                    help="just listen; do not pulse the reset line")
    args = ap.parse_args()

    port = args.port or next(iter(sorted(glob.glob("/dev/cu.usbmodem*"))), None)
    if not port:
        sys.exit("no /dev/cu.usbmodem* found — wake the device or replug USB")
    print(f"# port {port}, listening {args.seconds:g}s", flush=True)

    s = serial.Serial()
    s.port = port
    s.baudrate = 115200
    s.timeout = 0.2
    # Do not assert control lines on open; some boards reset on DTR/RTS.
    s.dtr = False
    s.rts = False
    s.open()

    if not args.no_reset:
        # Pulse reset so we catch the whole boot, panic included.
        s.setDTR(False)
        s.setRTS(True)
        time.sleep(0.12)
        s.setRTS(False)
        time.sleep(0.05)

    deadline = time.time() + args.seconds
    got = False
    while time.time() < deadline:
        data = s.read(4096)
        if data:
            got = True
            sys.stdout.write(data.decode("utf-8", "replace"))
            sys.stdout.flush()
    s.close()
    if not got:
        print("\n# (no output at all — the chip may be halted, in download mode, "
              "or not running our firmware)")


if __name__ == "__main__":
    main()
