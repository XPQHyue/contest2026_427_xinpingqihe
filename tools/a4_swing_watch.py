#!/usr/bin/env python3
"""A4 evidence: boot the board, start the agent + PhyWear, capture the proactive
swing scenario.

The CH340N can pulse the SoC reset when the port is opened, so this script
always assumes a fresh boot and (re)starts both applications itself:

    open port -> wait for nsh> -> `ai_agent &` -> `phywear &` -> listen

Then swing the watch.  The three steps of the proactive scenario are recorded:

  1. PhyWear detects sustained swinging            [phywear] PhyWear 巡检…
  2. The agent turns it into a tool call           NL fast path: phywear_…
  3. The experiment result comes back to PhyWear   proactive agent reply (…

Usage:
    tools/phywear/a4_swing_watch.py --minutes 10
"""

import argparse
import os
import sys
import time

import serial

MARKERS = (
    ("detect", "PhyWear 巡检"),
    ("fast", "NL fast path: phywear_"),
    ("reply", "proactive agent reply"),
)


class Board:
    def __init__(self, port, baud, log_path):
        os.makedirs(os.path.dirname(log_path), exist_ok=True)
        self.log = open(log_path, "ab", buffering=0)
        self.log.write(b"\n===== a4_swing_watch %s =====\n"
                       % time.strftime("%Y-%m-%d %H:%M:%S").encode())
        self.p = serial.Serial(port, baud, timeout=0.2)
        self.p.dtr = False
        self.p.rts = False
        # Always start from a clean boot: a second `phywear` in the same boot
        # hangs in the LCD driver, and this also makes the run reproducible.
        time.sleep(0.3)
        self.p.rts = True
        time.sleep(0.15)
        self.p.rts = False
        self.seen = {k: None for k, _ in MARKERS}
        self.tail = ""

    def feed(self, timeout, until=None, quiet=False):
        """Read for up to timeout seconds; stop early when `until` shows up."""

        deadline = time.time() + timeout
        while time.time() < deadline:
            data = self.p.read(4096)
            if not data:
                continue
            self.log.write(data)
            text = self.tail + data.decode("utf-8", "replace")
            self.tail = text[-300:]
            if not quiet:
                sys.stdout.write(text)
                sys.stdout.flush()
            for key, pat in MARKERS:
                if self.seen[key] is None and pat in text:
                    self.seen[key] = time.strftime("%H:%M:%S")
                    print("\n*** [%s] %s" % (self.seen[key], pat), flush=True)
            if until and until in text:
                return True
        return False

    def send(self, cmd):
        self.p.write((cmd + "\n").encode())
        self.p.flush()

    def close(self):
        self.p.close()
        self.log.close()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default="/dev/ttyUSB0")
    ap.add_argument("--baud", type=int, default=1000000)
    ap.add_argument("--minutes", type=float, default=10)
    ap.add_argument("--out", default=os.path.expanduser(
        "~/mimo-work/2026-09-13-a4-board/console.log"))
    args = ap.parse_args()

    b = Board(args.port, args.baud, args.out)
    try:
        print("waiting for the shell ...", flush=True)
        if not b.feed(40, until="nsh>"):
            sys.exit("board did not reach NSH")

        print("starting ai_agent ...", flush=True)
        b.send("ai_agent &")
        if not b.feed(60, until="AI Agent ready", quiet=True):
            sys.exit("agent did not start")

        print("starting phywear ...", flush=True)
        b.send("phywear &")
        b.feed(30, until="skills ready", quiet=True)
        print("both applications are up - SWING THE WATCH NOW", flush=True)

        deadline = time.time() + args.minutes * 60
        while time.time() < deadline and not all(b.seen.values()):
            b.feed(2, quiet=True)
    finally:
        b.close()

    print("summary:", b.seen, flush=True)
    if not all(b.seen.values()):
        sys.exit(1)


if __name__ == "__main__":
    main()
