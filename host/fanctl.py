#!/usr/bin/env python3
"""Command-line client for the STM32 PWM fan controller (USB CDC).

Examples:
    fanctl.py get
    fanctl.py set 2 40
    fanctl.py set all 60
    fanctl.py watch            # print status every second; keeps failsafe from tripping

The firmware returns all fans to 100 % when it hears nothing for 10 s,
so a control daemon must send a command (e.g. `ping`) more often than that.
"""
import argparse
import sys
import time

import serial

DEFAULT_PORT = "/dev/ttyACM0"


class FanController:
    def __init__(self, port, timeout=1.0):
        self.ser = serial.Serial(port, 115200, timeout=timeout)
        self.ser.reset_input_buffer()

    def command(self, line):
        """Send one command; return its output lines. Raises RuntimeError on 'err'."""
        self.ser.write((line + "\n").encode())
        lines = []
        while True:
            raw = self.ser.readline()
            if not raw:
                raise TimeoutError(f"no reply to {line!r}")
            text = raw.decode(errors="replace").strip()
            if text == "ok":
                return lines
            if text.startswith("err"):
                raise RuntimeError(text)
            lines.append(text)

    def status(self):
        """Parse 'get' into {'fan': {1: {...}}, 'ain': {...}, 'v12': {...}, ...}."""
        result = {}
        for text in self.command("get"):
            words = text.split()
            name, rest = words[0], words[1:]
            index = None
            if rest and "=" not in rest[0]:
                index, rest = int(rest[0]), rest[1:]
            fields = {}
            for kv in rest:
                key, _, value = kv.partition("=")
                fields[key] = float(value)
            if index is None:
                result[name] = fields
            else:
                result.setdefault(name, {})[index] = fields
        return result


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("-p", "--port", default=DEFAULT_PORT)
    sub = parser.add_subparsers(dest="cmd", required=True)
    sub.add_parser("get")
    p_set = sub.add_parser("set")
    p_set.add_argument("fan", help="1-4 or all")
    p_set.add_argument("duty", type=int, help="0-100")
    p_watch = sub.add_parser("watch")
    p_watch.add_argument("-i", "--interval", type=float, default=1.0)
    args = parser.parse_args()

    fc = FanController(args.port)
    try:
        if args.cmd == "get":
            print("\n".join(fc.command("get")))
        elif args.cmd == "set":
            fc.command(f"set {args.fan} {args.duty}")
        elif args.cmd == "watch":
            while True:
                print("\n".join(fc.command("get")), end="\n\n", flush=True)
                time.sleep(args.interval)
    except RuntimeError as e:
        sys.exit(str(e))
    except KeyboardInterrupt:
        pass


if __name__ == "__main__":
    main()
