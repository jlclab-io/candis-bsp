#!/usr/bin/env python3
"""Capture the Candis API suite across USB device stop/start transitions."""

from __future__ import annotations

import argparse
import re
import sys
import time
from pathlib import Path

from serial import Serial, SerialException
from serial.tools import list_ports


VID = 0x303A
PID_APPLICATION = 0x4001


def normalized_serial(value: str | None) -> str:
    return re.sub(r"[:-]", "", value or "").lower()


def find_port(serial_number: str | None, explicit: str | None):
    if explicit:
        for port in list_ports.comports():
            if port.device == explicit:
                return port
        return None
    for port in list_ports.comports():
        if port.vid != VID or port.pid != PID_APPLICATION:
            continue
        if serial_number and normalized_serial(port.serial_number) != normalized_serial(serial_number):
            continue
        return port
    return None


def wait_for_port(serial_number: str | None, explicit: str | None, deadline: float):
    while time.monotonic() < deadline:
        port = find_port(serial_number, explicit)
        if port is not None:
            return port
        time.sleep(0.1)
    return None


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--log", type=Path, required=True)
    parser.add_argument("--serial", help="USB serial number without separators")
    parser.add_argument("--port", help="explicit serial device")
    parser.add_argument("--timeout", type=float, default=900.0)
    parser.add_argument("--quiet", action="store_true")
    args = parser.parse_args()

    args.log.parent.mkdir(parents=True, exist_ok=True)
    deadline = time.monotonic() + args.timeout
    complete = False
    panic = False
    reconnects = 0
    with args.log.open("wb") as log:
        while time.monotonic() < deadline:
            port = wait_for_port(args.serial, args.port, deadline)
            if port is None:
                print("API_RUNTIME_PORT_TIMEOUT", file=sys.stderr, flush=True)
                return 2
            print(f"API_RUNTIME_PORT {port.device} {port.hwid}", file=sys.stderr, flush=True)
            try:
                with Serial(port.device, 115200, timeout=0.25, write_timeout=1,
                            exclusive=True) as link:
                    link.dtr = True
                    link.rts = False
                    while time.monotonic() < deadline:
                        data = link.read(4096)
                        if not data:
                            continue
                        log.write(data)
                        log.flush()
                        if not args.quiet:
                            sys.stdout.buffer.write(data)
                            sys.stdout.buffer.flush()
                        if b"API_TEST_COMPLETE" in data:
                            complete = True
                        lowered = data.lower()
                        if b"guru meditation" in lowered or b"panic" in lowered or b"abort() was called" in lowered:
                            panic = True
                        if complete or panic:
                            break
            except (SerialException, OSError) as exc:
                print(f"API_RUNTIME_RECONNECT {exc}", file=sys.stderr, flush=True)
                reconnects += 1
                time.sleep(0.15)
                continue
            if complete or panic:
                break
    print(f"API_RUNTIME_COMPLETE complete={complete} panic={panic} reconnects={reconnects} log={args.log}",
          file=sys.stderr, flush=True)
    if complete:
        return 0
    return 3 if panic else 4


if __name__ == "__main__":
    raise SystemExit(main())
