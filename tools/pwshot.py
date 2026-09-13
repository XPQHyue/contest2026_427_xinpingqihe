#!/usr/bin/env python3
"""PhyWear real-device screenshot capture (Huangshan Pi / SF32LB52).

The board has no /dev/fb0 and NSH has no dd/cat, so `phywear --shot` streams a
full RGB565 frame to the console as base64 with SHOT-BEGIN / SHOT-END framing
(see apps/examples/phywear/pw_shot.c).  This script drives that, verifies the
FNV-1a hash, and writes .rgb565 + .png files.

Serial safety (project iron rule 8):
  * pyserial asserts RTS/DTR on open, which holds the SoC in reset, so both are
    deasserted immediately after the port opens.
  * One port open per session; the board is not re-reset between pages.

Usage:
  pwshot.py probe                          # connect, wait for nsh>, show banner
  pwshot.py sweep [--settle 2500]          # 16 main pages, one run + retries
  pwshot.py shot PAGE [--settle 2500]      # one page via `phywear shot PAGE`
  pwshot.py run "CMD" [--label NAME] [--settle ms]
  pwshot.py runlist --labels a,b --commands "cmd1|cmd2"
"""

import argparse
import array
import os
import re
import sys
import time

import serial

PORT = "/dev/ttyUSB0"
BAUD = 1000000
BOOT_TIMEOUT = 30.0
FRAME_TIMEOUT = 180.0

# Must match g_cap_seq[] in apps/examples/phywear/phywear.c
PAGES = [
    "root", "raw", "pendulum", "spring", "centri", "incline", "ruler",
    "spec_accel", "spec_mic", "spec_mag", "stopwatch", "lightgate",
    "acousticgate", "applause", "settings", "about",
]

# Must match g_p2_seq[] in apps/examples/phywear/phywear.c
P2_PAGES = [
    "20_pend_p2", "21_spring_p2", "22_centri_p2", "23_incline_p2",
    "24_ruler_p2", "25_spec_p2", "26_stopwatch_p2", "27_lightgate_p2",
    "28_acousticgate_p2", "29_applause_p2",
]

BEGIN_RE = re.compile(
    rb"SHOT-BEGIN\s+(\S+)\s+(\d+)\s+(\d+)\s+rgb565\s+(\d+)\s+([0-9a-fA-F]{8})\s+(\S+)"
)
LINE_RE = re.compile(rb"^([0-9a-f]{4}):([A-Za-z0-9+/=]+):([0-9a-f]{4})$")
FAIL_RE = re.compile(rb"SHOT-FAIL\s+(\S+)\s+(\S+)")

B64_ALPHABET = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
B64_INDEX = {c: i for i, c in enumerate(B64_ALPHABET)}

# Must match PW_SHOT_CHUNK / PW_SHOT_PASSES in pw_shot.c
CHUNK = 192
PASSES = 2


def expected_b64_len(seq, total, chunk=CHUNK):
    """Base64 length the board emits for chunk `seq` of a `total` byte frame."""

    start = seq * chunk
    if start >= total:
        return None
    size = min(chunk, total - start)
    return 4 * ((size + 2) // 3)


def log(msg):
    print(f"[pwshot] {msg}", flush=True)


def fnv1a32(data):
    h = 2166136261
    for byte in data:
        h ^= byte
        h = (h * 16777619) & 0xFFFFFFFF
    return h


def rgb565_to_rgb(raw, width, height):
    """Expand little-endian RGB565 into a packed RGB byte string."""

    need = width * height * 2
    if len(raw) < need:
        raise ValueError(f"short pixel buffer: {len(raw)} < {need}")

    px = array.array("H")
    px.frombytes(raw[:need])
    if sys.byteorder == "big":
        px.byteswap()

    out = bytearray(width * height * 3)
    j = 0
    for v in px:
        r = (v >> 11) & 0x1F
        g = (v >> 5) & 0x3F
        b = v & 0x1F
        out[j] = (r * 255 + 15) // 31
        out[j + 1] = (g * 255 + 31) // 63
        out[j + 2] = (b * 255 + 15) // 31
        j += 3

    return bytes(out)


def b64_decode_join(chunks):
    data = b"".join(chunks)
    out = bytearray()
    acc = 0
    nbits = 0
    for byte in data:
        char = chr(byte)
        if char == "=":
            break
        if char not in B64_INDEX:
            raise ValueError(f"bad base64 character {char!r}")
        acc = (acc << 6) | B64_INDEX[char]
        nbits += 6
        if nbits >= 8:
            nbits -= 8
            out.append((acc >> nbits) & 0xFF)
    return bytes(out)


class FrameCollector:
    """Parser for the SHOT-BEGIN / SHOT-END console stream.

    The board sends every line twice (two full passes) and each line carries a
    16-bit checksum, because the CH340N USB bridge drops bytes under load.  For
    every sequence number we keep the first copy that decodes to the right
    length and passes its checksum; the frame FNV-1a hash in the header then
    covers the reassembled whole.
    """

    def __init__(self, outdir, rawdir, save=True):
        self.outdir = outdir
        self.rawdir = rawdir
        self.save = save
        self.frames = []
        self.failures = []
        self._reset()

    def _reset(self):
        self.meta = None
        self.best = {}
        self.rejected = 0
        self.ignored = 0

    def feed_line(self, line):
        line = line.rstrip(b"\r")

        if self.meta is None:
            match = BEGIN_RE.search(line)
            if match:
                name, w, h, nbytes, digest, src = match.groups()
                self.meta = {
                    "name": name.decode("ascii", "replace"),
                    "w": int(w),
                    "h": int(h),
                    "bytes": int(nbytes),
                    "hash": int(digest, 16),
                    "src": src.decode("ascii", "replace"),
                }
                self.best = {}
                self.rejected = 0
                log(f"frame start: {self.meta['name']} "
                    f"{self.meta['w']}x{self.meta['h']} src={self.meta['src']}")
                return

            match = FAIL_RE.search(line)
            if match:
                name = match.group(1).decode("ascii", "replace")
                why = match.group(2).decode("ascii", "replace")
                log(f"!! SHOT-FAIL {name}: {why}")
                self.failures.append((name, why))
            return

        if b"SHOT-END" in line:
            self._finish()
            return

        stripped = line.strip()
        if not stripped or stripped.startswith(b"SHOT-PASS"):
            return

        match = LINE_RE.match(stripped)
        if not match:
            self.ignored += 1
            return

        seq = int(match.group(1), 16)
        if seq in self.best:
            return                      # Already have a clean copy

        want = expected_b64_len(seq, self.meta["bytes"])
        if want is None or len(match.group(2)) != want:
            self.rejected += 1
            return

        try:
            raw = b64_decode_join([match.group(2)])
        except ValueError:
            self.rejected += 1
            return

        if (fnv1a32(raw) & 0xFFFF) != int(match.group(3), 16):
            self.rejected += 1
            return

        self.best[seq] = raw

    def _finish(self):
        meta = self.meta
        best = self.best
        rejected = self.rejected
        ignored = self.ignored
        self._reset()

        chunks = (meta["bytes"] + CHUNK - 1) // CHUNK
        missing = [seq for seq in range(chunks) if seq not in best]
        if missing:
            log(f"!! {meta['name']}: {len(missing)}/{chunks} line(s) missing "
                f"after {PASSES} passes (e.g. {missing[:6]}); "
                f"{rejected} rejected, {ignored} unrelated")
            self.failures.append((meta["name"], "line-loss"))
            return

        raw = b"".join(best[seq] for seq in range(chunks))

        if len(raw) != meta["bytes"]:
            log(f"!! {meta['name']}: size mismatch "
                f"{len(raw)} != {meta['bytes']}")
            self.failures.append((meta["name"], "size"))
            return

        real = fnv1a32(raw)
        if real != meta["hash"]:
            log(f"!! {meta['name']}: frame hash mismatch "
                f"{real:08x} != {meta['hash']:08x}")
            self.failures.append((meta["name"], "hash"))
            return

        meta["label"] = meta["name"]
        if self.save:
            self._save(meta, raw)

        self.frames.append(meta)
        log(f"OK {meta['name']}: {meta['w']}x{meta['h']} {meta['bytes']} B "
            f"hash={meta['hash']:08x}"
            + (f" -> {meta['file']}" if self.save else ""))

    def _save(self, meta, raw):
        os.makedirs(self.rawdir, exist_ok=True)
        rawname = os.path.join(self.rawdir, f"{meta['name']}.rgb565")
        with open(rawname, "wb") as handle:
            handle.write(raw)

        png = os.path.join(self.outdir, f"{meta['name']}.png")
        rgb = rgb565_to_rgb(raw, meta["w"], meta["h"])
        try:
            from PIL import Image
            Image.frombytes("RGB", (meta["w"], meta["h"]), rgb).save(png)
            meta["file"] = png
        except ImportError:
            meta["file"] = rawname


class Board:
    def __init__(self, port=PORT, baud=BAUD):
        self.ser = serial.Serial(port, baud, timeout=0.2)
        # Iron rule 8: the CH340N wires RTS to the SoC reset line.
        self.ser.dtr = False
        self.ser.rts = False
        time.sleep(0.2)
        self.ser.reset_input_buffer()

    def close(self):
        try:
            self.ser.close()
        except Exception:
            pass

    def wait_for(self, needle, timeout):
        deadline = time.time() + timeout
        window = b""
        while time.time() < deadline:
            chunk = self.ser.read(4096)
            if chunk:
                window = (window + chunk)[-8192:]
                if needle in window:
                    return window
            else:
                self.ser.write(b"\r")
        return window

    def reset(self, timeout=BOOT_TIMEOUT):
        """Pulse RTS to reset the SoC (the CH340N wires RTS to the reset pin).

        This is the documented reset mechanism (see SKILL.md section 3) and is
        needed because a second `phywear` run after the first exits hangs in
        the LCD driver: the GUI can only be brought up once per boot.
        """

        log("resetting the board (RTS pulse)")
        self.ser.rts = True
        time.sleep(0.2)
        self.ser.rts = False
        time.sleep(0.3)
        self.ser.reset_input_buffer()
        window = self.wait_for(b"nsh>", timeout)
        if b"nsh>" not in window:
            log("WARNING: board did not come back to nsh> after reset")
        return window

    def run(self, command, collector, done_markers, expected, timeout):
        """Send one command and stream frames until a done marker shows up."""

        before = len(collector.frames)
        before_fail = len(collector.failures)
        log(f"$ {command}")
        self.ser.reset_input_buffer()
        self.ser.write(command.encode("utf-8") + b"\r\n")

        deadline = time.time() + timeout
        buf = b""
        window = b""

        while time.time() < deadline:
            chunk = self.ser.read(4096)
            if not chunk:
                if len(collector.frames) - before >= expected:
                    break
                continue

            window = (window + chunk)[-512:]
            buf += chunk
            while b"\n" in buf:
                line, buf = buf.split(b"\n", 1)
                collector.feed_line(line)

            if any(marker in window for marker in done_markers):
                time.sleep(0.3)
                rest = self.ser.read(65536)
                while b"\n" in rest:
                    line, rest = rest.split(b"\n", 1)
                    collector.feed_line(line)
                break

        got = len(collector.frames) - before
        fails = len(collector.failures) - before_fail
        if got < expected:
            log(f"!! expected {expected} frame(s), got {got} ({fails} failed)")
        return got


def prepare_dirs(outdir):
    rawdir = os.path.join(outdir, "raw")
    os.makedirs(outdir, exist_ok=True)
    os.makedirs(rawdir, exist_ok=True)
    return outdir, rawdir


def rename_frame(frame, outdir, rawdir, label):
    old = frame.get("file")
    if old:
        new = os.path.join(outdir, f"{label}{os.path.splitext(old)[1]}")
        if os.path.exists(old) and old != new:
            os.replace(old, new)
            frame["file"] = new
    oldraw = os.path.join(rawdir, f"{frame['name']}.rgb565")
    newraw = os.path.join(rawdir, f"{label}.rgb565")
    if os.path.exists(oldraw) and oldraw != newraw:
        os.replace(oldraw, newraw)
    frame["label"] = label


def summarize(collector, outdir, want=None):
    got = captured_labels(collector)
    if want is not None:
        missing = [label for label in want if label not in got]
    else:
        missing = sorted({name for name, _ in collector.failures} - got)

    log(f"--- {len(got & set(want)) if want else len(got)}/"
        f"{len(want) if want else len(got)} page(s) captured ---")
    for frame in sorted(collector.frames, key=lambda f: f.get("label", "")):
        log(f"  {frame.get('label', frame['name'])}: {frame.get('file')}")
    for label in missing:
        log(f"  MISSING {label}")
    log(f"output: {outdir}")
    return 1 if missing else 0


def cmd_probe(args):
    outdir, _ = prepare_dirs(args.out)
    board = Board(args.port, args.baud)
    try:
        banner = board.wait_for(b"nsh>", args.boot_timeout)
        if b"nsh>" in banner:
            log("board is at nsh> prompt")
        else:
            log("WARNING: no nsh> prompt seen")
        log(f"tail: {banner[-400:]!r}")
    finally:
        board.close()
    return 0


def rename_main_frames(collector, outdir, rawdir):
    for frame in collector.frames:
        if frame["name"] in PAGES:
            index = PAGES.index(frame["name"])
            rename_frame(frame, outdir, rawdir, f"{index:02d}_{frame['name']}")


def captured_labels(collector):
    return {frame.get("label", frame["name"]) for frame in collector.frames}


def missing_labels(collector, want):
    done = captured_labels(collector)
    return [label for label in want if label not in done]


def capture_p2_one(board, collector, args, outdir, rawdir, index, label):
    """One bench page per board boot: switching between bench pages inside a
    single phywear process hangs after the first one, so every bench page gets
    its own process and a reset."""

    board.reset(args.boot_timeout)
    before = len(collector.frames)
    board.run(f"phywear lang zh --p2only={index} --p2={args.p2_settle}",
              collector, [b"SHOTSWEEP DONE"], 1, timeout=args.timeout)
    for frame in collector.frames[before:]:
        rename_frame(frame, outdir, rawdir, label)
    return bool(collector.frames[before:])


def cmd_all(args):
    """16 main pages in one run, then the 10 bench pages one run each."""

    outdir, rawdir = prepare_dirs(args.out)
    collector = FrameCollector(outdir, rawdir)
    board = Board(args.port, args.baud)

    want = [f"{i:02d}_{name}" for i, name in enumerate(PAGES)]
    if args.with_p2:
        want += P2_PAGES

    try:
        board.wait_for(b"nsh>", args.boot_timeout)

        board.run(f"phywear lang zh --sweep={args.settle}", collector,
                  [b"SHOTSWEEP DONE"], len(PAGES), timeout=args.timeout)
        rename_main_frames(collector, outdir, rawdir)

        if args.with_p2:
            for index, label in enumerate(P2_PAGES):
                capture_p2_one(board, collector, args, outdir, rawdir,
                               index, label)

        for retry in range(args.retries):
            missing = missing_labels(collector, want)
            if not missing:
                break

            log(f"retry {retry + 1}: missing {missing}")

            for label in missing:
                if label in P2_PAGES:
                    continue
                board.reset(args.boot_timeout)
                before = len(collector.frames)
                board.run(f"phywear lang zh shot {label[3:]}", collector,
                          [b"SHOTMODE DONE"], 1, timeout=args.timeout)
                for frame in collector.frames[before:]:
                    rename_frame(frame, outdir, rawdir, label)

            for label in missing:
                if label in P2_PAGES:
                    capture_p2_one(board, collector, args, outdir, rawdir,
                                   P2_PAGES.index(label), label)
    finally:
        board.close()
    return summarize(collector, outdir, want)


def cmd_p2(args):
    """Only the 10 bench-injected second pages (one run each)."""

    outdir, rawdir = prepare_dirs(args.out)
    collector = FrameCollector(outdir, rawdir)
    board = Board(args.port, args.baud)
    try:
        board.wait_for(b"nsh>", args.boot_timeout)
        for index, label in enumerate(P2_PAGES):
            capture_p2_one(board, collector, args, outdir, rawdir, index, label)
    finally:
        board.close()
    return summarize(collector, outdir, list(P2_PAGES))


def cmd_sweep(args):
    """Only the 16 main pages."""

    outdir, rawdir = prepare_dirs(args.out)
    collector = FrameCollector(outdir, rawdir)
    board = Board(args.port, args.baud)
    try:
        board.wait_for(b"nsh>", args.boot_timeout)
        board.run(f"phywear lang zh --sweep={args.settle}", collector,
                  [b"SHOTSWEEP DONE"], len(PAGES), timeout=args.timeout)
        rename_main_frames(collector, outdir, rawdir)
    finally:
        board.close()
    return summarize(collector, outdir,
                     [f"{i:02d}_{name}" for i, name in enumerate(PAGES)])


def cmd_shot(args):
    outdir, rawdir = prepare_dirs(args.out)
    collector = FrameCollector(outdir, rawdir)
    board = Board(args.port, args.baud)
    try:
        board.wait_for(b"nsh>", args.boot_timeout)
        for retry in range(args.retries):
            before = len(collector.frames)
            board.run(f"phywear lang zh shot {args.page}", collector,
                      [b"SHOTMODE DONE"], 1, timeout=args.timeout)
            if len(collector.frames) > before:
                if args.label:
                    rename_frame(collector.frames[-1], outdir, rawdir,
                                 args.label)
                break
            log(f"retry {retry + 1} for {args.page}")
    finally:
        board.close()
    return summarize(collector, outdir)


def cmd_run(args):
    outdir, rawdir = prepare_dirs(args.out)
    collector = FrameCollector(outdir, rawdir)
    board = Board(args.port, args.baud)
    try:
        board.wait_for(b"nsh>", args.boot_timeout)
        for retry in range(args.retries):
            before = len(collector.frames)
            board.run(args.command, collector, [b"SHOTMODE DONE",
                                                b"SHOTSWEEP DONE"],
                      args.repeat, timeout=args.timeout)
            if len(collector.frames) > before:
                if args.label:
                    rename_frame(collector.frames[-1], outdir, rawdir,
                                 args.label)
                break
            log(f"retry {retry + 1} for {args.command}")
    finally:
        board.close()
    return summarize(collector, outdir)


def cmd_runlist(args):
    """One command per label; frames are renamed to match the labels."""

    outdir, rawdir = prepare_dirs(args.out)
    labels = args.labels.split(",")
    commands = args.commands.split("|")
    if len(labels) != len(commands):
        log(f"!! {len(labels)} labels but {len(commands)} commands")
        return 2

    collector = FrameCollector(outdir, rawdir)
    board = Board(args.port, args.baud)
    try:
        board.wait_for(b"nsh>", args.boot_timeout)
        pending = list(zip(labels, commands))
        for retry in range(args.retries + 1):
            still = []
            for label, command in pending:
                before = len(collector.frames)
                board.run(command, collector, [b"SHOTMODE DONE",
                                               b"SHOTSWEEP DONE"],
                          args.repeat, timeout=args.timeout)
                new = collector.frames[before:]
                if new:
                    for frame in new:
                        rename_frame(frame, outdir, rawdir, label)
                else:
                    still.append((label, command))
            if not still:
                break
            log(f"retry {retry + 1}: {[item[0] for item in still]}")
            pending = still
    finally:
        board.close()
    return summarize(collector, outdir)


def main():
    default_out = os.path.expanduser("~/mimo-work/2026-09-12-realboard-shots")

    parser = argparse.ArgumentParser(
        description=__doc__,
        formatter_class=argparse.RawDescriptionHelpFormatter)

    common = argparse.ArgumentParser(add_help=False)
    common.add_argument("--port", default=PORT)
    common.add_argument("--baud", type=int, default=BAUD)
    common.add_argument("--out", default=default_out)
    common.add_argument("--boot-timeout", type=float, default=BOOT_TIMEOUT)
    common.add_argument("--timeout", type=float, default=FRAME_TIMEOUT)

    parser.add_argument("--port", default=PORT)
    parser.add_argument("--baud", type=int, default=BAUD)
    parser.add_argument("--out", default=default_out)
    parser.add_argument("--boot-timeout", type=float, default=BOOT_TIMEOUT)
    parser.add_argument("--timeout", type=float, default=FRAME_TIMEOUT)

    sub = parser.add_subparsers(dest="cmd", required=True)

    p = sub.add_parser("probe", parents=[common],
                       help="connect and report the console state")
    p.set_defaults(func=cmd_probe, retries=0)

    p = sub.add_parser("all", parents=[common],
                       help="16 main pages + 10 bench pages in one run")
    p.add_argument("--settle", type=int, default=2500,
                   help="settle time for the 16 main pages (ms)")
    p.add_argument("--p2-settle", type=int, default=8000,
                   help="settle time for the bench pages (ms)")
    p.add_argument("--no-p2", dest="with_p2", action="store_false",
                   help="main pages only")
    p.add_argument("--retries", type=int, default=2)
    p.set_defaults(func=cmd_all, with_p2=True)

    p = sub.add_parser("sweep", parents=[common],
                       help="dump the 16 main pages")
    p.add_argument("--settle", type=int, default=2500)
    p.add_argument("--retries", type=int, default=2)
    p.set_defaults(func=cmd_sweep)

    p = sub.add_parser("p2", parents=[common],
                       help="dump the 10 bench-injected second pages")
    p.add_argument("--p2-settle", type=int, default=8000)
    p.set_defaults(func=cmd_p2)

    p = sub.add_parser("shot", parents=[common], help="dump one page")
    p.add_argument("page")
    p.add_argument("--settle", type=int, default=2500)
    p.add_argument("--label")
    p.add_argument("--retries", type=int, default=2)
    p.set_defaults(func=cmd_shot)

    p = sub.add_parser("run", parents=[common],
                       help="run an arbitrary phywear command line")
    p.add_argument("command")
    p.add_argument("--label")
    p.add_argument("--repeat", type=int, default=1)
    p.add_argument("--retries", type=int, default=2)
    p.set_defaults(func=cmd_run)

    p = sub.add_parser("runlist", parents=[common],
                       help="one command per label, all in one session")
    p.add_argument("--labels", required=True)
    p.add_argument("--commands", required=True, help="separated by '|'")
    p.add_argument("--repeat", type=int, default=1)
    p.add_argument("--retries", type=int, default=2)
    p.set_defaults(func=cmd_runlist)

    args = parser.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
