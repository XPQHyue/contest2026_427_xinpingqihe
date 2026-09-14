#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""PhyWear 迁移：新电脑环境检测。

用法：
    python3 check_env.py                 # 打印表格 + 每项修复命令
    python3 check_env.py --json          # 机器可读
    python3 check_env.py --strict        # 有 ❌ 时退出码 1

它只回答一件事：**这台新机器具备干活的条件了吗**。
（"代码/修复/固件做到哪一步"由 ../phywear-reproduce/check_progress.py 回答。）
"""

from __future__ import annotations

import argparse
import json
import os
import shutil
import subprocess
import sys
from pathlib import Path

HOME = Path.home()
WS = Path(os.environ.get("PHYWEAR_WS", HOME / "openvela"))
REPO = Path(os.environ.get("PHYWEAR_REPO", HOME / "work/contest2026_427_xinpingqihe"))

OK, WARN, FAIL, INFO = "ok", "warn", "fail", "info"
ICON = {OK: "✅", WARN: "⚠️", FAIL: "❌", INFO: "ℹ️"}


def run(cmd: list[str], timeout: int = 20) -> tuple[int, str]:
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return p.returncode, (p.stdout or "") + (p.stderr or "")
    except Exception as exc:  # noqa: BLE001
        return 127, str(exc)


class Report:
    def __init__(self) -> None:
        self.items: list[dict] = []

    def add(self, group: str, name: str, status: str, detail: str, fix: str = "") -> None:
        self.items.append({"group": group, "name": name, "status": status,
                           "detail": detail, "fix": fix})


def check_host(rep: Report) -> None:
    g = "主机工具"
    rc, ver = run(["cmake", "--version"])
    m = ver.split()[2] if rc == 0 and len(ver.split()) > 2 else ""
    major = tuple(int(x) for x in m.split(".")[:2]) if m[:1].isdigit() else (0, 0)
    rep.add(g, "cmake ≥ 3.22", OK if major >= (3, 22) else FAIL, m or "未安装",
            "sudo apt install -y cmake")

    rc, ver = run(["ninja", "--version"])
    rep.add(g, "ninja", OK if rc == 0 else FAIL, ver.strip() or "未安装",
            "sudo apt install -y ninja-build")

    rc, ver = run(["python3", "--version"])
    rep.add(g, "python3 ≥ 3.10", OK if rc == 0 else FAIL, ver.strip(),
            "sudo apt install -y python3")

    rc, out = run(["python3", "-c", "import serial;print(serial.__version__)"])
    rep.add(g, "pyserial", OK if rc == 0 else FAIL, out.strip() or "未安装",
            "python3 -m pip install pyserial")

    for tool, level, fix in (("git", FAIL, "sudo apt install -y git"),
                             ("picocom", WARN, "sudo apt install -y picocom"),
                             ("sftool", FAIL, "按 SiFli 官方说明安装 sftool（烧录必需）"),
                             ("node", WARN, "sudo apt install -y nodejs（重生成中文字体需要）"),
                             ("adb", INFO, "sudo apt install -y android-tools-adb（模拟器取图需要）")):
        p = shutil.which(tool)
        rep.add(g, tool, OK if p else level, p or "未找到", fix if not p else "")

    gcc = shutil.which("arm-none-eabi-gcc")
    pre = WS / "prebuilts/gcc/linux-x86_64/arm-none-eabi/bin/arm-none-eabi-gcc"
    rep.add(g, "arm-none-eabi-gcc", OK if (gcc or pre.is_file()) else FAIL,
            gcc or (str(pre) if pre.is_file() else "未找到"),
            "sudo apt install -y gcc-arm-none-eabi")

    aarch = WS / "prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin/aarch64-none-elf-gcc"
    rep.add(g, "aarch64-none-elf-gcc（模拟器）", OK if aarch.is_file() else WARN,
            str(aarch) if aarch.is_file() else "未找到（repo sync 后应有）",
            "先完成 repo sync；或只在真机开发时忽略")

    lfc = HOME / ".npm-global/lib/node_modules/lv_font_conv/lv_font_conv.js"
    rep.add(g, "lv_font_conv（重生成 CJK 字体）", OK if lfc.is_file() else WARN,
            str(lfc) if lfc.is_file() else "未安装",
            "npm i -g lv_font_conv")
    droid = Path("/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf")
    rep.add(g, "DroidSansFallbackFull.ttf", OK if droid.is_file() else WARN,
            str(droid) if droid.is_file() else "未安装",
            "sudo apt install -y fonts-droid-fallback")

    try:
        free = shutil.disk_usage(str(HOME)).free / (1 << 30)
        rep.add(g, "磁盘余量 ≥ 30 GB", OK if free >= 30 else WARN, f"{free:.1f} GB",
                "清理磁盘（单套 openvela + cmake_out 约需 25 GB）")
    except Exception:  # noqa: BLE001
        pass


def check_workspace(rep: Report) -> None:
    g = "openvela 工作区"
    rep.add(g, f"{WS}", OK if WS.is_dir() else FAIL, "存在" if WS.is_dir() else "不存在",
            "repo init -u <openvela manifest> -b <分支> && repo sync -c -j8")
    for rel, level in ((".repo", FAIL), ("nuttx", FAIL), ("apps", FAIL),
                       ("vendor/sifli", FAIL), ("vendor/openvela", WARN),
                       ("packages/ai_agent", FAIL), ("prebuilts", WARN)):
        f = WS / rel
        rep.add(g, rel, OK if f.exists() else level, "就绪" if f.exists() else "缺失",
                "" if f.exists() else "repo sync 补齐")

    link = WS / "nuttx/arch/arm/src/ipc_queue"
    tgt = WS / "vendor/sifli/chips/sf32lb52/ipc_queue"
    if link.exists():
        rep.add(g, "ipc_queue 符号链接", OK, "存在")
    else:
        rep.add(g, "ipc_queue 符号链接", INFO, "不存在（仅 BT 相关，当前构建不需要）",
                f'ln -s "{tgt}" "{link}"')


def check_repo(rep: Report) -> None:
    g = "参赛仓"
    rep.add(g, f"{REPO}", OK if REPO.is_dir() else FAIL,
            "存在" if REPO.is_dir() else "不存在",
            "git clone https://github.com/open-vela/contest2026_427_xinpingqihe.git")
    for rel in ("app/phywear/CMakeLists.txt", "src/MANIFEST.md", "docs/07_构建烧录与复现指南.md",
                "board/ftab_openvela.bin", "board/flash_with_ftab.sh",
                ".claude/skills/phywear-reproduce/manifest.json",
                ".claude/skills/phywear-migrate/SKILL.md"):
        f = REPO / rel
        rep.add(g, rel, OK if f.exists() else FAIL, "存在" if f.exists() else "缺失",
                "" if f.exists() else "重新 clone/更新参赛仓（迁移包里有完整快照）")
    logs = REPO / "logs"
    n = len(list(logs.rglob("*.jsonl"))) if logs.is_dir() else 0
    rep.add(g, "logs/ AI 会话", OK if n else WARN, f"{n} 个 jsonl",
            "bash .claude/skills/phywear-migrate/finish_session.sh")


def check_device(rep: Report) -> None:
    g = "硬件与密钥"
    dev = Path("/dev/ttyUSB0")
    busy = False
    if dev.exists():
        rc, out = run(["bash", "-c", f"fuser {dev} 2>/dev/null | wc -w"])
        busy = rc == 0 and out.strip() not in ("", "0")
    rep.add(g, "/dev/ttyUSB0", OK if dev.exists() else WARN,
            ("存在" + ("（被占用）" if busy else "（空闲）")) if dev.exists() else "未识别（没插板可忽略）",
            "插上黄山派；若被占用先退出 picocom")
    key = HOME / ".config/phywear/mimo.key"
    rep.add(g, "MiMo API Key", OK if key.is_file() else WARN,
            str(key) if key.is_file() else "未就位",
            "把迁移包 secrets/mimo.key 拷到 ~/.config/phywear/ 并 chmod 600（切勿入库）")
    rep.add(g, "仿真/真机目标", INFO,
            f"真机=nsh-ai；模拟器=goldfish-phywear；串口 1000000 8N1", "")


def render(rep: Report) -> str:
    out = [f"PhyWear 迁移环境检测  主机 {os.uname().nodename}  工作区 {WS}", ""]
    for group in dict.fromkeys(i["group"] for i in rep.items):
        items = [i for i in rep.items if i["group"] == group]
        bad = [i for i in items if i["status"] == FAIL]
        out.append(f"[{group}]  {'全绿' if not bad else f'{len(bad)} 项待修'}")
        for i in items:
            line = f"  {ICON[i['status']]} {i['name']:<34} {i['detail']}"
            out.append(line)
            if i["fix"] and i["status"] in (FAIL, WARN):
                out.append(f"      ↳ 修复：{i['fix']}")
        out.append("")
    n_fail = sum(1 for i in rep.items if i["status"] == FAIL)
    n_warn = sum(1 for i in rep.items if i["status"] == WARN)
    out.append(f"汇总：❌ {n_fail}   ⚠️ {n_warn}")
    out.append("下一步：环境全绿后跑 ../phywear-reproduce/restore_code.py --execute 恢复代码，"
               "再跑 check_progress.py 核对进度。")
    return "\n".join(out)


def main() -> int:
    ap = argparse.ArgumentParser(description="PhyWear 迁移环境检测")
    ap.add_argument("--json", action="store_true")
    ap.add_argument("--strict", action="store_true")
    args = ap.parse_args()

    rep = Report()
    check_host(rep)
    check_workspace(rep)
    check_repo(rep)
    check_device(rep)

    if args.json:
        print(json.dumps(rep.items, ensure_ascii=False, indent=1))
    else:
        print(render(rep))

    if args.strict and any(i["status"] == FAIL for i in rep.items):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
