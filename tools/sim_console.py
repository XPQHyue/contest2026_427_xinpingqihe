#!/usr/bin/env python3
"""Drive the openvela goldfish emulator from a script.

The emulator does not open a TCP port for the guest serial console: it is
started as ``qemu ... -serial mon:stdio``, so the NSH prompt shares the
emulator process's own stdin/stdout.  Starting it from a shell redirection
therefore makes the console write-only.  This wrapper gives the console a pty
and exposes it as a pair of plain files:

    <log>  - everything the guest prints (append-only, flushed per read)
    <in>   - a FIFO; every line written to it is sent to the guest

Usage:
    sim_console.py <build-dir> [--log LOG] [--in FIFO] [--window]

Typical use:
    sim_console.py cmake_out/vela_goldfish-arm64-v8a-ap-phywear \
        --log /tmp/sim.log --in /tmp/sim.in &
    printf 'help\n' > /tmp/sim.in
    tail -f /tmp/sim.log
"""

import argparse
import os
import pty
import select
import signal
import subprocess
import sys
import time

OPENVELA = os.path.dirname(os.path.dirname(os.path.dirname(
    os.path.abspath(__file__))))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("build_dir")
    ap.add_argument("--log", default="/tmp/sim_console.log")
    ap.add_argument("--in", dest="fifo", default="/tmp/sim_console.in")
    ap.add_argument("--window", action="store_true",
                    help="show the emulator window (needs DISPLAY)")
    ap.add_argument("--extra", default="", help="extra emulator args")
    args = ap.parse_args()

    build_dir = args.build_dir
    if not os.path.isabs(build_dir):
        build_dir = os.path.join(OPENVELA, build_dir)

    for path in (args.log, args.fifo):
        if os.path.exists(path) and not os.path.isfile(path):
            os.unlink(path)
    if os.path.exists(args.fifo):
        os.unlink(args.fifo)
    os.mkfifo(args.fifo)

    log = open(args.log, "ab", buffering=0)

    master, slave = pty.openpty()
    cmd = [os.path.join(OPENVELA, "emulator.sh"), build_dir]
    if not args.window:
        cmd.append("-no-window")
    cmd.append("-no-audio")
    if args.extra:
        cmd.extend(args.extra.split())

    proc = subprocess.Popen(cmd, stdin=slave, stdout=slave, stderr=slave,
                            cwd=OPENVELA, close_fds=True)
    os.close(slave)

    fifo = os.open(args.fifo, os.O_RDONLY | os.O_NONBLOCK)

    def shutdown(*_):
        proc.terminate()
        log.close()
        try:
            os.unlink(args.fifo)
        except OSError:
            pass
        sys.exit(0)

    signal.signal(signal.SIGTERM, shutdown)
    signal.signal(signal.SIGINT, shutdown)

    pending = b""
    while True:
        rlist, _, _ = select.select([master, fifo], [], [], 0.5)
        if master in rlist:
            try:
                data = os.read(master, 65536)
            except OSError:
                break
            if not data:
                break
            log.write(data)

        if fifo in rlist:
            try:
                chunk = os.read(fifo, 4096)
            except OSError:
                chunk = b""
            if chunk:
                pending += chunk
                while b"\n" in pending:
                    line, pending = pending.split(b"\n", 1)
                    os.write(master, line + b"\r")
                    time.sleep(0.05)

        if proc.poll() is not None:
            break

    shutdown()


if __name__ == "__main__":
    main()
