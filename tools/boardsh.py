#!/usr/bin/env python3
"""Talk to the Huangshan Pi NSH console over the USB serial port.

Serial iron rules for this board (hard learned):
  * the CH340N wires RTS to the SoC reset, so DTR/RTS must be deasserted right
    after opening, otherwise the board is held in reset;
  * a second `phywear` run in the same boot hangs in the LCD driver, and Ctrl-C
    cannot kill it (CONFIG_TTY_SIGINT is off on the board) - reset between runs;
  * never use erase_flash.

Usage:
    boardsh.py --reset --run "ls /data" --run "ai_agent &" --wait 10
    boardsh.py --watch 20                   # just print what the board says
"""

import argparse
import sys
import time

import serial

PORT = "/dev/ttyUSB0"
BAUD = 1000000


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--port", default=PORT)
    ap.add_argument("--baud", type=int, default=BAUD)
    ap.add_argument("--reset", action="store_true", help="pulse RTS first")
    ap.add_argument("--watch", type=float, default=0, help="just listen N seconds")
    ap.add_argument("--run", action="append", default=[], help="NSH command")
    ap.add_argument("--wait", type=float, default=4.0,
                    help="seconds to listen after each command")
    ap.add_argument("--settle", type=float, default=1.0)
    args = ap.parse_args()

    p = serial.Serial(args.port, args.baud, timeout=0.2)
    p.dtr = False
    p.rts = False
    time.sleep(0.3)

    if args.reset:
        p.rts = True
        time.sleep(0.15)
        p.rts = False
        time.sleep(args.settle)
        if not args.run:
            args.watch = args.watch or 15.0

    def drain(seconds):
        t0 = time.time()
        while time.time() - t0 < seconds:
            data = p.read(4096)
            if data:
                sys.stdout.write(data.decode("utf-8", "replace"))
                sys.stdout.flush()

    drain(args.settle)

    for cmd in args.run:
        p.write(cmd.encode() + b"\r")
        p.flush()
        drain(args.wait)

    if args.watch:
        drain(args.watch)

    p.close()


if __name__ == "__main__":
    main()
