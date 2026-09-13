#!/usr/bin/env python3
"""A1 evidence capture: PhyWear Markdown Skill reaches the AI Agent.

Runs two short simulator sessions and records, for each one, the number of
bytes in the agent's skills summary (the text the model sees):

  phase A - agent only: the 10 built-in skills.
  phase B - PhyWear first: the app installs its skill, so the summary grows by
            exactly one entry.

Phase B also pulls the installed skill back off the device and compares it with
the repository copy byte for byte.

Usage:
    tools/phywear/a1_skill_evidence.py [--out DIR] [--keep-running]
"""

import argparse
import hashlib
import os
import re
import shutil
import signal
import subprocess
import sys
import time

OPENVELA = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = "cmake_out/vela_goldfish-arm64-v8a-ap-phywear"
SKILL = "phywear-physics-coach.md"
SKILL_SRC = os.path.join(OPENVELA, "apps", "examples", "phywear", "skills", SKILL)
SKILL_DST = "/data/agent/skills/" + SKILL
ADB = ["adb", "-s", "127.0.0.1:5555"]

PROMPT = b"goldfish-armv8a-ap>"
SUMMARY = re.compile(rb"\[skills\] Skills summary: (\d+) bytes")


class Sim:
    def __init__(self, out_dir, tag):
        self.log_path = os.path.join(out_dir, "console-%s.log" % tag)
        self.fifo = os.path.join(out_dir, "console-%s.in" % tag)
        for path in (self.log_path, self.fifo):
            if os.path.exists(path):
                os.unlink(path)
        self.driver = subprocess.Popen(
            [sys.executable, os.path.join(OPENVELA, "tools", "phywear", "sim_console.py"),
             BUILD, "--log", self.log_path, "--in", self.fifo],
            cwd=OPENVELA,
            stdout=open(os.path.join(out_dir, "driver-%s.log" % tag), "wb"),
            stderr=subprocess.STDOUT)

    def send(self, line, pause=0.4):
        with open(self.fifo, "ab", buffering=0) as fh:
            fh.write(line.encode() + b"\n")
        time.sleep(pause)

    def send_verified(self, line, expect, tries=6, timeout=10):
        """Send a line until the expected reaction shows up in the log.

        NSH and the agent CLI both read the same console, so a line can land on
        the wrong reader.  Retrying until the expected output appears makes the
        script deterministic without knowing who owns stdin.
        """

        for _ in range(tries):
            self.send(line)
            if self.wait(expect, timeout=timeout):
                return True
        return False

    def read(self):
        try:
            with open(self.log_path, "rb") as fh:
                return fh.read()
        except FileNotFoundError:
            return b""

    def wait(self, pattern, timeout=90):
        deadline = time.time() + timeout
        while time.time() < deadline:
            data = self.read()
            if isinstance(pattern, bytes):
                m = pattern in data
            else:
                m = pattern.search(data)
            if m:
                return m
            time.sleep(1)
        return None

    def stop(self):
        self.driver.send_signal(signal.SIGTERM)
        try:
            self.driver.wait(timeout=15)
        except subprocess.TimeoutExpired:
            self.driver.kill()
        # sim_console terminates the emulator, but make sure nothing is left.
        subprocess.run(["pkill", "-f", "[q]emu-system-aarch64"], check=False)
        time.sleep(3)


def park_nsh(sim):
    """Run `sleep 600` on NSH so the agent CLI becomes the console reader.

    Both NSH and the agent CLI read the same console.  While NSH is inside a
    foreground command it does not read, which leaves the console to the agent
    CLI.  Whether a line reaches NSH or the agent is a race, so look for the
    agent's "Unknown command" complaint and retry until NSH really is parked.
    """

    for _ in range(8):
        mark = len(sim.read())
        sim.send("sleep 600", 1.5)
        tail = sim.read()[mark:]
        if b"Unknown command: sleep 600" not in tail:
            return True
    return False


def start_agent(sim):
    """Start the agent, then park NSH so the agent CLI owns the console."""

    sim.send_verified("ai_agent &", b"AI Agent ready")
    park_nsh(sim)
    time.sleep(1)


def summary_bytes(sim, marker):
    for _ in range(4):
        mark = len(sim.read())
        sim.send("ask " + marker)
        for _ in range(20):
            m = SUMMARY.search(sim.read()[mark:])
            if m:
                return int(m.group(1))
            time.sleep(1)
    return None


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.expanduser("~/mimo-work/2026-09-13-a1-skill"))
    ap.add_argument("--keep-running", action="store_true")
    ap.add_argument("--no-wipe", action="store_true",
                    help="keep the existing emulator data image")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    results = {}

    # Never inherit an emulator from an earlier run: the AVD refuses to start
    # twice, and a leftover guest would keep /data populated.
    subprocess.run(["pkill", "-f", "[q]emu-system-aarch64"], check=False)
    subprocess.run(["pkill", "-f", "[s]im_console.py"], check=False)
    time.sleep(3)

    # The emulator keeps /data in vela_data.bin, which survives reboots.  Start
    # from a freshly generated image so "before" really is before.
    if not args.no_wipe:
        # gen_images only runs when its output is missing, so remove the data
        # image first: the guest keeps /data in it across reboots.
        data_img = os.path.join(OPENVELA, BUILD, "vela_data.bin")
        if os.path.exists(data_img):
            os.unlink(data_img)
        env = dict(os.environ)
        env["PATH"] = ":".join([
            os.path.join(OPENVELA, "prebuilts", "tools", "linux", "x86_64"),
            os.path.join(OPENVELA, "prebuilts", "gcc", "linux-x86_64",
                         "aarch64-none-elf", "bin"),
            os.path.join(OPENVELA, "vendor", "artinchip", "tools", "scripts"),
            env.get("PATH", "")])
        subprocess.run(["ninja", "-C", os.path.join(OPENVELA, BUILD), "gen_images"],
                       check=False, env=env,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if not os.path.exists(data_img):
            sys.exit("failed to regenerate %s" % data_img)

    # ---- phase A: agent only -> built-in skills only -----------------------
    print("== phase A: agent only ==")
    sim = Sim(args.out, "A")
    try:
        if sim.wait(PROMPT, timeout=120) is None:
            sys.exit("emulator did not boot")
        start_agent(sim)
        sim.send("echo PHASE-A-ASK")
        results["summary_builtin"] = summary_bytes(sim, "A")
        print("   skills summary (built-in only):", results["summary_builtin"])
    finally:
        sim.stop()

    # ---- phase B: PhyWear installs the skill ------------------------------
    print("== phase B: phywear installs its skill ==")
    sim = Sim(args.out, "B")
    try:
        if sim.wait(PROMPT, timeout=120) is None:
            sys.exit("emulator did not boot")
        sim.send("phywear &", 1.0)
        m = sim.wait(re.compile(
            rb"\[phywear\] installed skill (\S+) \((\d+) bytes\)"),
            timeout=60)
        if m:
            results["installed_path"] = m.group(1).decode()
            results["installed_bytes"] = int(m.group(2))
            print("   app log:", m.group(0).decode())

        subprocess.run(["adb", "connect", "127.0.0.1:5555"], check=False,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        time.sleep(1)

        start_agent(sim)
        sim.send("echo PHASE-B-ASK")
        results["summary_with_skill"] = summary_bytes(sim, "B")
        print("   skills summary (with PhyWear skill):",
              results["summary_with_skill"])

        # Pull the device copy back and compare with the repository source.
        pulled = os.path.join(args.out, "pulled-" + SKILL)
        subprocess.run(ADB + ["pull", SKILL_DST, pulled], check=False,
                       stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        if os.path.exists(pulled):
            with open(pulled, "rb") as fh:
                got = hashlib.md5(fh.read()).hexdigest()
            with open(SKILL_SRC, "rb") as fh:
                want = hashlib.md5(fh.read()).hexdigest()
            results["md5_device"] = got
            results["md5_repo"] = want
            results["byte_identical"] = got == want
            print("   device md5", got, "repo md5", want,
                  "identical" if got == want else "MISMATCH")
    finally:
        if not args.keep_running:
            sim.stop()

    delta = None
    if results.get("summary_builtin") and results.get("summary_with_skill"):
        delta = results["summary_with_skill"] - results["summary_builtin"]
    results["summary_delta_bytes"] = delta

    report = os.path.join(args.out, "RESULT.md")
    with open(report, "w") as fh:
        fh.write("# A1 证据：PhyWear Skill 自动安装到 /data/agent/skills/\n\n")
        fh.write("| 项 | 值 |\n|---|---|\n")
        for key in ("installed_path", "installed_bytes", "summary_builtin",
                    "summary_with_skill", "summary_delta_bytes", "md5_device",
                    "md5_repo", "byte_identical"):
            fh.write("| %s | %s |\n" % (key, results.get(key)))
        fh.write("\n控制台日志：`console-A.log`（仅 Agent）、`console-B.log`（含 PhyWear Skill）\n")

    print("report:", report)
    for key, val in results.items():
        print("  %-22s %s" % (key, val))


if __name__ == "__main__":
    main()
