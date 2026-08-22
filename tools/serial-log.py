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
    ap.add_argument("--wait-for-port", type=float, default=0.0, metavar="SECONDS",
                    help="poll for the port to appear, for up to SECONDS, before "
                         "listening. This is how a WAKE is captured: deep sleep "
                         "powers down USB, so the port disappears while the device "
                         "sleeps and re-enumerates when it wakes. Start this first, "
                         "then press power -- attaching after the wake misses "
                         "setup() entirely, which is the half you want.")
    args = ap.parse_args()

    def find_port():
        return args.port if args.port else next(
            iter(sorted(glob.glob("/dev/cu.usbmodem*"))), None)

    port = find_port()
    if port is None and args.wait_for_port > 0:
        print(f"# waiting up to {args.wait_for_port:g}s for a port to appear "
              f"— press the power button now", flush=True)
        deadline = time.monotonic() + args.wait_for_port
        while time.monotonic() < deadline:
            port = find_port()
            if port:
                break
            time.sleep(0.05)
        # The port node can appear a moment before it will accept an open, so a
        # first open often fails with EBUSY or ENOENT. Retrying briefly is the
        # difference between catching the boot and reporting a spurious error.
        if port:
            for _ in range(40):
                try:
                    probe = serial.Serial(port)
                    probe.close()
                    break
                except Exception:
                    time.sleep(0.05)
    if not port:
        sys.exit("no /dev/cu.usbmodem* found — wake the device or replug USB "
                 "(or pass --wait-for-port 30 and press power)")
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
