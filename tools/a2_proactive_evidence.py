#!/usr/bin/env python3
"""A2 evidence capture: PhyWear notices a swing and acts on its own.

Boots the simulator (whose IMU stub generates a 0.8 Hz swing waveform), starts
the AI Agent and then PhyWear, and records the three steps of the proactive
scenario:

  1. PhyWear detects sustained swinging and pushes an event   [phywear] 巡检…
  2. The agent turns the event into a tool call               NL fast path: …
  3. The experiment runs and the reply returns to PhyWear     proactive agent reply …

No LLM backend is needed: the event text matches the agent's offline intent
table, which is also how the scenario runs on the real board.

Usage:
    tools/phywear/a2_proactive_evidence.py [--out DIR]
"""

import argparse
import os
import re
import signal
import subprocess
import sys
import time

OPENVELA = os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__))))
BUILD = "cmake_out/vela_goldfish-arm64-v8a-ap-phywear"
PROMPT = b"goldfish-armv8a-ap>"

DETECTED = re.compile(r"\[phywear\] (PhyWear 巡检[^\r\n]*)".encode("utf-8"))
FASTPATH = re.compile(rb"NL fast path: (\w+)")
REPLY = re.compile(rb"proactive agent reply \((\d+)\): (.{0,200})")


class Sim:
    def __init__(self, out_dir):
        self.log_path = os.path.join(out_dir, "console.log")
        self.fifo = os.path.join(out_dir, "console.in")
        for path in (self.log_path, self.fifo):
            if os.path.exists(path):
                os.unlink(path)
        self.driver = subprocess.Popen(
            [sys.executable, os.path.join(OPENVELA, "tools", "phywear", "sim_console.py"),
             BUILD, "--log", self.log_path, "--in", self.fifo],
            cwd=OPENVELA,
            stdout=open(os.path.join(out_dir, "driver.log"), "wb"),
            stderr=subprocess.STDOUT)

    def send(self, line, pause=0.5):
        with open(self.fifo, "ab", buffering=0) as fh:
            fh.write(line.encode() + b"\n")
        time.sleep(pause)

    def read(self):
        try:
            with open(self.log_path, "rb") as fh:
                return fh.read()
        except FileNotFoundError:
            return b""

    def wait_bytes(self, pattern, timeout=90):
        deadline = time.time() + timeout
        while time.time() < deadline:
            data = self.read()
            if isinstance(pattern, bytes):
                if pattern in data:
                    return True
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
        subprocess.run(["pkill", "-f", "[q]emu-system-aarch64"], check=False)
        time.sleep(3)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.expanduser("~/mimo-work/2026-09-13-a2-proactive"))
    args = ap.parse_args()
    os.makedirs(args.out, exist_ok=True)

    subprocess.run(["pkill", "-f", "[q]emu-system-aarch64"], check=False)
    subprocess.run(["pkill", "-f", "[s]im_console"], check=False)
    time.sleep(3)

    results = {}
    sim = Sim(args.out)
    try:
        if sim.wait_bytes(PROMPT, timeout=120) is None:
            sys.exit("emulator did not boot")

        # Agent first so PhyWear can open the client link on startup.  Both
        # commands must reach NSH, so start both before parking it; the
        # scenario itself needs no further console input.
        sim.send("ai_agent &", 1.0)
        if not sim.wait_bytes(b"AI Agent ready", timeout=60):
            sys.exit("agent did not start")
        sim.send("phywear &", 1.0)
        sim.send("sleep 600", 1.0)

        m = sim.wait_bytes(DETECTED, timeout=90)
        if m:
            results["detected"] = m.group(1).decode("utf-8", "replace")
            print("1. detected:", results["detected"])

        m = sim.wait_bytes(FASTPATH, timeout=90)
        if m:
            results["fast_path_tool"] = m.group(1).decode()
            print("2. agent tool:", results["fast_path_tool"])

        m = sim.wait_bytes(REPLY, timeout=120)
        if m:
            results["reply_status"] = m.group(1).decode()
            results["reply"] = m.group(2).decode("utf-8", "replace")
            print("3. reply:", results["reply"][:160])
    finally:
        sim.stop()

    report = os.path.join(args.out, "RESULT.md")
    with open(report, "w") as fh:
        fh.write("# A2 证据：PhyWear 主动巡检（设备自触发 → Agent 执行）\n\n")
        fh.write("| 步骤 | 实测 |\n|---|---|\n")
        fh.write("| 1 设备自己发现摆动 | %s |\n" % results.get("detected"))
        fh.write("| 2 Agent 转成工具调用（离线意图，无需 LLM） | %s |\n"
                 % results.get("fast_path_tool"))
        fh.write("| 3 实验结果回传给 PhyWear（status=%s） | `%s` |\n"
                 % (results.get("reply_status"), results.get("reply")))
        fh.write("\n完整串口日志：`console.log`\n")

    print("report:", report)


if __name__ == "__main__":
    main()
