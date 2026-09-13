#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""PhyWear 复现进度检测：把「这台机器现在做到哪一步了」打印成一张表。

用法（在参赛仓根目录执行，或任意目录用 --repo/--workspace 指定）：
    python3 .claude/skills/phywear-reproduce/check_progress.py            # 完整报告
    python3 .claude/skills/phywear-reproduce/check_progress.py --brief    # 只看非 OK 项
    python3 .claude/skills/phywear-reproduce/check_progress.py --json     # 机器可读
    python3 .claude/skills/phywear-reproduce/check_progress.py --phase P2 # 只看某阶段

它做三件事：
  1) 拿 manifest.json（参赛仓快照 md5）逐文件核对工作区 ~/openvela 的代码状态；
  2) 语义检查关键修复是否真的接线（启动时序等待、麦克风增益、/dev/spk0 注册、defconfig 使能、i18n 条目数…）；
  3) 检查构建产物 / 串口 / 验证脚本 / 文档证据 / AI 日志，给出「下一步该做什么」。

退出码：默认 0（纯报告）；--strict 时任一 ❌ 返回 1（给 CI / Agent 判断用）。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import re
import shutil
import subprocess
import sys
import time
import unicodedata
from pathlib import Path

HERE = Path(__file__).resolve().parent
MANIFEST = HERE / "manifest.json"

HOME = Path.home()
WS = Path(os.environ.get("PHYWEAR_WS", HOME / "openvela"))
REPO = Path(os.environ.get("PHYWEAR_REPO", HOME / "work/contest2026_427_xinpingqihe"))

BUILD_AI = "cmake_out/sf32lb52_lchspi_ulp_nsh_ai"
BUILD_SIM = "cmake_out/vela_goldfish-arm64-v8a-ap-phywear"
AI_DEFCONFIG = "vendor/sifli/boards/sf32lb52/sf32lb52_lchspi_ulp/configs/nsh-ai/defconfig"

# 阶段定义：(阶段 id, 标题, 该阶段的检查项 id 前缀)
PHASES = [
    ("P0", "主机环境"),
    ("P1", "工作区"),
    ("P2", "代码状态"),
    ("P3", "关键修复标记"),
    ("P4", "构建产物"),
    ("P5", "烧录链路"),
    ("P6", "验证工具"),
    ("P7", "交付与日志"),
]

PHASE_ORDER = [p[0] for p in PHASES]

OK, WARN, FAIL, SKIP, INFO = "ok", "warn", "fail", "skip", "info"
ICON = {OK: "✅", WARN: "⚠️", FAIL: "❌", SKIP: "➖", INFO: "ℹ️"}
RANK = {OK: 0, INFO: 0, SKIP: 0, WARN: 1, FAIL: 2}

NEXT_STEP = {
    "P0": "先装齐主机依赖（cmake/ninja/python3/sftool/picocom + pyserial），见 docs/07 §1",
    "P1": "openvela 工作区不完整：先 repo init/sync（见 docs/07 §1），再回来重跑本脚本",
    "P2": "代码与快照不一致：bash .claude/skills/phywear-reproduce/restore_code.sh 恢复（先看 dry-run）",
    "P3": "关键修复未接线：按 restore_code.sh 恢复对应文件，重点看 bsp_init.c / sf32lb52_mic.c / defconfig",
    "P4": "重新构建固件：见 docs/07 §2.1（真机）/ §2.2（模拟器）",
    "P5": "烧录前先释放串口（picocom 独占），再 ./flash_with_ftab.sh <bin> /dev/ttyUSB0，见 docs/07 §3",
    "P6": "主机侧验证脚本缺失：从参赛仓 tools/ 恢复（check 里已列出缺哪些）",
    "P7": "补齐交付物：docs/01..08、docs/evidence/、logs/（采集器见 docs/07 §7.2）",
}


# ---------------------------------------------------------------- 基础工具
def md5_of(path: Path) -> str:
    h = hashlib.md5()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def strip_comments(text: str) -> str:
    text = re.sub(r"/\*.*?\*/", " ", text, flags=re.S)
    text = re.sub(r"//[^\n]*", " ", text)
    return re.sub(r"\s+", " ", text).strip()


def read_text(path: Path, limit: int = 8 << 20) -> str:
    try:
        with path.open("rb") as fh:
            return fh.read(limit).decode("utf-8", "replace")
    except OSError:
        return ""


def run(cmd: list[str], timeout: int = 20) -> tuple[int, str]:
    try:
        p = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        return p.returncode, (p.stdout or "") + (p.stderr or "")
    except (OSError, subprocess.SubprocessError) as exc:
        return 127, str(exc)


def dwidth(s: str) -> int:
    return sum(2 if unicodedata.east_asian_width(c) in "WF" else 1 for c in s)


def pad(s: str, width: int) -> str:
    return s + " " * max(0, width - dwidth(s))


def human(n: float) -> str:
    n = float(n)
    for unit in ("B", "KB", "MB", "GB"):
        if n < 1024 or unit == "GB":
            return f"{n:,.0f} {unit}" if unit == "B" else f"{n:.1f} {unit}"
        n /= 1024.0
    return str(n)


# ---------------------------------------------------------------- 结果收集
class Report:
    def __init__(self) -> None:
        self.items: list[dict] = []

    def add(self, cid: str, title: str, status: str, detail: str = "") -> None:
        self.items.append({"id": cid, "phase": cid.split("-")[0], "title": title,
                           "status": status, "detail": detail})

    def phase_status(self, phase: str) -> str:
        worst = OK
        seen = False
        for it in self.items:
            if it["phase"] != phase:
                continue
            seen = True
            if RANK[it["status"]] > RANK[worst]:
                worst = it["status"]
        return worst if seen else SKIP

    def phase_counts(self, phase: str) -> tuple[int, int]:
        items = [i for i in self.items if i["phase"] == phase and i["status"] != SKIP]
        good = sum(1 for i in items if i["status"] in (OK, INFO))
        return good, len(items)


# ---------------------------------------------------------------- P0 主机环境
def check_host(rep: Report) -> None:
    p = "P0"
    _, ver = run(["cmake", "--version"])
    m = re.search(r"cmake version (\d+)\.(\d+)", ver)
    if m and (int(m.group(1)), int(m.group(2))) >= (3, 22):
        rep.add(f"{p}-cmake", "cmake ≥ 3.22", OK, m.group(0))
    elif m:
        rep.add(f"{p}-cmake", "cmake ≥ 3.22", WARN, f"版本偏低：{m.group(0)}")
    else:
        rep.add(f"{p}-cmake", "cmake ≥ 3.22", FAIL, "未找到 cmake")

    rc, ver = run(["ninja", "--version"])
    rep.add(f"{p}-ninja", "ninja", OK if rc == 0 else FAIL, ver.strip().splitlines()[0] if rc == 0 else "未找到 ninja")

    rc, ver = run(["python3", "--version"])
    m = re.search(r"(\d+)\.(\d+)", ver)
    ok = rc == 0 and m and (int(m.group(1)), int(m.group(2))) >= (3, 10)
    rep.add(f"{p}-python", "python3 ≥ 3.10", OK if ok else FAIL, ver.strip() or "未找到 python3")

    rc, out = run(["python3", "-c", "import serial;print(serial.__version__)"])
    rep.add(f"{p}-pyserial", "pyserial（串口脚本依赖）", OK if rc == 0 else WARN,
            out.strip() if rc == 0 else "未安装：pip install pyserial")

    for tool, level, why in (("sftool", FAIL, "烧录工具"), ("picocom", WARN, "人工串口终端")):
        path = shutil.which(tool)
        rep.add(f"{p}-{tool}", f"{tool}（{why}）", OK if path else level, path or "未找到")

    gcc = shutil.which("arm-none-eabi-gcc")
    prebuilt = WS / "prebuilts/gcc/linux-x86_64/arm-none-eabi/bin/arm-none-eabi-gcc"
    if gcc:
        rep.add(f"{p}-armgcc", "arm-none-eabi-gcc（真机工具链）", OK, gcc)
    elif prebuilt.is_file():
        rep.add(f"{p}-armgcc", "arm-none-eabi-gcc（真机工具链）", OK, f"{prebuilt}（prebuilts，未在 PATH）")
    else:
        rep.add(f"{p}-armgcc", "arm-none-eabi-gcc（真机工具链）", FAIL, "未找到，真机构建会失败")

    cand = {
        "aidl": ["prebuilts/tools/linux/x86_64/aidl"],
        "mcopy": ["prebuilts/tools/linux/x86_64/mcopy",
                  "vendor/artinchip/tools/scripts/mcopy"],
        "mformat": ["prebuilts/tools/linux/x86_64/mformat",
                    "vendor/artinchip/tools/scripts/mformat"],
        "genromfs": ["prebuilts/tools/linux/x86_64/genromfs"],
    }
    for name, rels in cand.items():
        found = next((WS / r for r in rels if (WS / r).is_file()), None)
        if found is None:
            found = shutil.which(name)
        rep.add(f"{p}-{name}", f"主机小工具 {name}（模拟器构建依赖）", OK if found else WARN,
                str(found) if found else "未找到：" + " / ".join(rels))


# ---------------------------------------------------------------- P1 工作区
def check_workspace(rep: Report) -> None:
    p = "P1"
    rep.add(f"{p}-ws", f"openvela 工作区 {WS}", OK if WS.is_dir() else FAIL,
            "存在" if WS.is_dir() else "不存在：先 repo init/sync")
    for rel, name, level in (
        (".repo", "多仓元数据 .repo/", FAIL),
        ("nuttx", "nuttx 子仓", FAIL),
        ("apps", "apps 子仓", FAIL),
        ("vendor/sifli", "vendor/sifli 子仓", FAIL),
        ("packages/ai_agent", "packages/ai_agent 子仓", WARN),
        ("prebuilts", "prebuilts/", WARN),
        ("tools/phywear", "本队主机脚本 tools/phywear/", WARN),
    ):
        f = WS / rel
        rep.add(f"{p}-{rel.replace('/', '_')}", name, OK if f.exists() else level,
                "就绪" if f.exists() else f"缺失：{rel}")


# ---------------------------------------------------------------- P2 代码状态
def compare_file(entry: dict) -> str:
    """返回 ok / equivalent（仅注释或换行差异）/ diff / missing。"""
    dst = WS / entry["dst"]
    if not dst.is_file():
        return "missing"
    snap = REPO / entry["snap"]
    if entry["compare"] == "code":
        if strip_comments(read_text(snap)) == strip_comments(read_text(dst)):
            return "equivalent"
        return "diff"
    if md5_of(dst) == entry["md5"]:
        return "ok"
    # 换行差异（CRLF/LF）不算代码差异
    a = read_text(snap).replace("\r\n", "\n")
    b = read_text(dst).replace("\r\n", "\n")
    if a == b and a:
        return "crlf"
    return "diff"


def check_code(rep: Report) -> dict:
    p = "P2"
    if not MANIFEST.is_file():
        rep.add(f"{p}-manifest", "manifest.json", FAIL, "缺失：先跑 gen_manifest.py --write")
        return {}
    files = json.loads(MANIFEST.read_text())["files"]
    groups: dict[str, dict] = {}
    diff_detail: list[str] = []
    equiv_detail: list[str] = []
    for e in files:
        g = groups.setdefault(e["rule"], {"desc": e["rule_desc"], "critical": e["critical"],
                                          "ok": 0, "diff": 0, "missing": 0, "equivalent": 0, "n": 0})
        g["n"] += 1
        st = compare_file(e)
        if st == "ok":
            g["ok"] += 1
        elif st in ("equivalent", "crlf"):
            g["equivalent"] += 1
            equiv_detail.append(e["dst"])
        elif st == "diff":
            g["diff"] += 1
            diff_detail.append(f"[{e['rule']}] {e['dst']}")
        else:
            g["missing"] += 1
            diff_detail.append(f"[{e['rule']}] 缺失 {e['dst']}")

    for rule, g in groups.items():
        bad = g["diff"] + g["missing"]
        tag = "关键" if g["critical"] else "参考"
        if bad == 0:
            extra = f"，其中 {g['equivalent']} 个仅注释/换行差异" if g["equivalent"] else ""
            rep.add(f"{p}-{rule}", f"{tag}：{g['desc']}", OK, f"{g['n']}/{g['n']} 一致{extra}")
        else:
            rep.add(f"{p}-{rule}", f"{tag}：{g['desc']}", FAIL if g["critical"] else INFO,
                    f"{g['ok'] + g['equivalent']}/{g['n']} 一致，差异 {g['diff']}，缺失 {g['missing']}")
    if diff_detail:
        shown = diff_detail[:8]
        rep.add(f"{p}-detail", "差异明细（前 8 条）", INFO if not any(
            "关键" in i["title"] and i["status"] == FAIL for i in rep.items) else WARN,
            "；".join(shown) + (f"；… 共 {len(diff_detail)} 条" if len(diff_detail) > 8 else ""))
    if equiv_detail:
        rep.add(f"{p}-equiv", "仅注释/换行差异（内容等价，属正常）", INFO,
                "、".join(equiv_detail[:6]) + (f" 等 {len(equiv_detail)} 个" if len(equiv_detail) > 6 else ""))
    return groups


# ---------------------------------------------------------------- P3 修复标记
def grep_count(path: Path, pattern: str) -> int:
    return len(re.findall(pattern, read_text(path)))


def check_markers(rep: Report) -> None:
    p = "P3"
    app = WS / "apps/examples/phywear"
    sifli = WS / "vendor/sifli"

    n = grep_count(sifli / "boards/sf32lb52/sf32lb52_lchspi_ulp/src/bsp_init.c", r"HAL_Delay_us\(2000\)")
    rep.add(f"{p}-bootdelay", "启动时序等待（修 AB/ABCD 卡死）", OK if n >= 3 else FAIL,
            f"HAL_Delay_us(2000) × {n}（期望 ≥3）")

    mic = sifli / "boards/sf32lb52/drivers/audio/sf32lb52_mic.c"
    t = read_text(mic)
    m = re.search(r"#define\s+MIC_VOLUME\s+(\d+)", t)
    rep.add(f"{p}-micgain", "麦克风增益（否则只有几十 LSB）", OK if m and int(m.group(1)) >= 30 else FAIL,
            f"MIC_VOLUME {m.group(1)}" if m else "未找到 MIC_VOLUME")

    spk = sifli / "boards/sf32lb52/drivers/audio/sf32lb52_spk.c"
    t = read_text(spk)
    has_fade = "SPK_FADE_STEPS" in t
    has_vol = "PW_SPK_VOL_DEFAULT" in t
    rep.add(f"{p}-spk", "扬声器驱动（含淡入淡出防爆音）", OK if (has_fade and has_vol) else FAIL,
            f"存在（fade={has_fade}, vol_default={has_vol}）" if spk.is_file() else "缺失 sf32lb52_spk.c")

    ap = read_text(sifli / "boards/sf32lb52/sf32lb52_lchspi_ulp/src/sifli_ap.c")
    rep.add(f"{p}-spkreg", "注册 /dev/spk0", OK if "/dev/spk0" in ap and "SF32LB52_SPK" in ap else FAIL,
            "/dev/spk0 已注册" if "/dev/spk0" in ap else "未注册")

    cfg = read_text(WS / AI_DEFCONFIG)
    need = ["CONFIG_EXAMPLES_PHYWEAR=y", "CONFIG_EXAMPLES_AI_AGENT_VELA=y",
            "CONFIG_SF32LB52_SPK=y", "CONFIG_LV_USE_SIFLI_EPIC=y"]
    miss = [k for k in need if k not in cfg]
    rep.add(f"{p}-defconfig", "真机 nsh-ai defconfig 使能项", OK if not miss else FAIL,
            "4/4 使能" if not miss else "缺少 " + ", ".join(miss))

    i18n_lines = read_text(app / "phywear_i18n_tables.inc").splitlines()
    counts = {"en": 0, "zh": 0}
    cur = None
    for line in i18n_lines:
        m = re.search(r"g_pw_str_(en|zh)\[\]", line)
        if m:
            cur = m.group(1)
        elif cur and re.match(r"\s*\[PW_STR_", line):
            counts[cur] += 1
    ids_en, ids_zh = counts["en"], counts["zh"]
    rep.add(f"{p}-i18n", "i18n 双语条目数（EN/ZH 必须等长）",
            OK if ids_en == ids_zh and ids_en >= 200 else WARN,
            f"EN {ids_en} / ZH {ids_zh} 条（共 {ids_en + ids_zh}）")

    blob = read_text(app / "pw_skill_blob.c")
    skill_md = app / "skills/phywear-physics-coach.md"
    ok = "phywear-physics-coach" in blob and skill_md.is_file()
    rep.add(f"{p}-skillblob", "Skill 自动安装（真机 /data/agent/skills/）", OK if ok else FAIL,
            f"blob 内嵌 + 正本 {skill_md.stat().st_size} B" if ok else "blob 或正本缺失")

    watch = read_text(app / "pw_watch.c")
    m = re.search(r"#\s*define\s+PW_WATCH_PROACTIVE\s+(\d)", watch)
    rep.add(f"{p}-proactive", "主动场景默认开关（如实口径：默认关闭）",
            OK if m and m.group(1) == "0" else WARN,
            f"PW_WATCH_PROACTIVE {m.group(1)}" if m else "未找到宏")

    n_src = len(list(app.glob("*.c"))) + len(list(app.glob("*.h")))
    gen = len(list(app.glob("pw_font_*.c")))
    rep.add(f"{p}-appsources", "应用源码规模", INFO, f"{n_src} 个 .c/.h（含生成字体源码 {gen} 个）")


# ---------------------------------------------------------------- P4 构建产物
def check_build(rep: Report) -> None:
    p = "P4"
    bdir = WS / BUILD_AI
    rep.add(f"{p}-dir", f"真机构建目录 {BUILD_AI}", OK if bdir.is_dir() else WARN,
            "存在" if bdir.is_dir() else "不存在：按 docs/07 §2.1 配置")
    cfg = bdir / ".config"
    rep.add(f"{p}-config", "已生成 .config（defconfig 生效）", OK if cfg.is_file() else WARN,
            "存在" if cfg.is_file() else "缺失：删掉 .config 后重新 cmake 配置")

    binp = bdir / "nuttx.bin"
    if binp.is_file():
        st = binp.stat()
        rep.add(f"{p}-bin", "真机固件 nuttx.bin", OK,
                f"{human(st.st_size)}  md5 {md5_of(binp)[:16]}…  {time.strftime('%m-%d %H:%M', time.localtime(st.st_mtime))}")
        newest = (0, "")
        for src in list((WS / "apps/examples/phywear").glob("*.c")) + [WS / AI_DEFCONFIG]:
            if src.is_file() and src.stat().st_mtime > newest[0]:
                newest = (src.stat().st_mtime, str(src.relative_to(WS)))
        if newest[0] > st.st_mtime:
            rep.add(f"{p}-stale", "源码是否比固件新", WARN, f"需要重新编译：{newest[1]} 比 nuttx.bin 新")
        else:
            rep.add(f"{p}-stale", "源码是否比固件新", OK, "固件是最新的")
    else:
        rep.add(f"{p}-bin", "真机固件 nuttx.bin", WARN, "未构建（docs/07 §2.1）")

    sim = WS / BUILD_SIM
    simbin = sim / "nuttx"
    rep.add(f"{p}-sim", "模拟器构建产物", OK if simbin.exists() else INFO,
            f"{BUILD_SIM} 就绪" if simbin.exists() else "未构建（仅演示 LLM 时需要，docs/07 §2.2）")


# ---------------------------------------------------------------- P5 烧录链路
def check_flash(rep: Report) -> None:
    p = "P5"
    dev = Path("/dev/ttyUSB0")
    rep.add(f"{p}-dev", "/dev/ttyUSB0（CH340N）", OK if dev.exists() else WARN,
            "存在" if dev.exists() else "未插板/未识别")
    if dev.exists():
        rc, out = run(["fuser", str(dev)])
        busy = rc == 0 and out.strip()
        holder = ""
        if busy:
            rc2, out2 = run(["bash", "-c", f"lsof {dev} 2>/dev/null | awk 'NR>1{{print $1}}' | sort -u | tr '\\n' ' '"])
            holder = out2.strip()
        rep.add(f"{p}-busy", "串口是否被占用（铁律 3：独占）", WARN if busy else OK,
                f"被占用：{holder or '未知进程'}（先退出 picocom/脚本）" if busy else "空闲")
    script = WS / "flash_with_ftab.sh"
    rep.add(f"{p}-script", "flash_with_ftab.sh（先 ftab 后固件）", OK if script.is_file() else FAIL,
            str(script) if script.is_file() else "缺失（见 docs/07 §3）")
    rep.add(f"{p}-erase", "严禁 erase_flash（铁律）", INFO, "见 docs/07 §3：擦了也不会产生 ftab")


# ---------------------------------------------------------------- P6 验证工具
def check_verify(rep: Report) -> None:
    p = "P6"
    tools = {
        "boardsh.py": "串口批处理（--run/--reset）",
        "pwshot.py": "真机整帧截图（base64 over UART）",
        "sim_console.py": "模拟器 pty+FIFO 控制台",
        "gen_manifest.py": "复现清单生成（本 SKILL）",
        "check_progress.py": "进度检测（本 SKILL）",
        "restore_code.py": "快照→工作区恢复（本 SKILL）",
        "a1_skill_evidence.py": "A1 Skill 证据采集",
        "a2_proactive_evidence.py": "A2 主动场景证据采集",
        "a4_swing_watch.py": "A4 单摆/挥动检测证据",
    }
    missing = []
    for name, why in tools.items():
        if name in ("gen_manifest.py", "check_progress.py", "restore_code.py"):
            f = HERE / name
        else:
            f = WS / "tools/phywear" / name
        if not f.is_file():
            missing.append(f"{name}（{why}）")
    rep.add(f"{p}-tools", "主机侧脚本齐备", OK if not missing else WARN,
            f"{len(tools)}/{len(tools)} 就绪" if not missing else "缺：" + "，".join(missing))
    ws_skill = WS / ".claude/skills/phywear-reproduce/SKILL.md"
    rep.add(f"{p}-skillinst", "复现 SKILL 已挂进工作区（AI CLI 可直接调用）",
            OK if ws_skill.exists() else INFO,
            str(ws_skill.parent) if ws_skill.exists() else
            "未挂载：bash <参赛仓>/.claude/skills/phywear-reproduce/install_into_workspace.sh")
    emu = WS / "emulator.sh"
    rep.add(f"{p}-emu", "模拟器启动脚本 emulator.sh", OK if emu.is_file() else INFO,
            "存在" if emu.is_file() else "未找到（本机无模拟器则忽略）")


# ---------------------------------------------------------------- P7 交付与日志
def check_delivery(rep: Report) -> None:
    p = "P7"
    docs = REPO / "docs"
    if docs.is_dir():
        want = ["01_项目描述_如实版.md", "02_作品介绍.md", "03_技术报告.md", "04_应用场景说明.md",
                "05_AI_Agent与Skill.md", "06_真机验证记录.md", "07_构建烧录与复现指南.md",
                "08_演示视频拍摄脚本.md"]
        miss = [d for d in want if not (docs / d).is_file()]
        rep.add(f"{p}-docs", "docs/01–08 文档齐全", OK if not miss else WARN,
                f"{len(want) - len(miss)}/{len(want)} 篇" + ("；缺 " + ", ".join(miss) if miss else ""))
        ev = docs / "evidence"
        subs = sorted(d.name for d in ev.iterdir() if d.is_dir()) if ev.is_dir() else []
        rep.add(f"{p}-evidence", "docs/evidence 原始证据", OK if len(subs) >= 3 else WARN,
                f"{len(subs)} 组：{', '.join(subs)}" if subs else "无")
    else:
        rep.add(f"{p}-docs", "docs/", FAIL, f"{docs} 不存在（--repo 指错？）")

    logs = REPO / "logs"
    jsonls = list(logs.rglob("*.jsonl")) if logs.is_dir() else []
    rep.add(f"{p}-logs", "logs/ AI Coding 会话", OK if jsonls else WARN,
            f"{len(jsonls)} 个会话 / {human(sum(f.stat().st_size for f in jsonls))}")

    env = HOME / ".claude/contest-collector.env"
    rep.add(f"{p}-collector", "日志采集器已安装", OK if env.is_file() else WARN,
            str(env) if env.is_file() else "未安装：docs/07 §7.2 install.sh")

    readme = REPO / "README.md"
    if readme.is_file():
        t = read_text(readme)
        want_keys = {"作品名称": ("作品名称", "作品简介", "作品"), "赛道": ("赛道",),
                     "运行方式": ("运行方式", "运行"), "简介": ("简介", "概述", "介绍")}
        keys = [k for k, alts in want_keys.items() if any(a in t for a in alts)]
        rep.add(f"{p}-readme", "README 四要素（名称/赛道/运行/简介）",
                OK if len(keys) >= 4 else WARN, f"命中 {len(keys)}/4：{', '.join(keys)}")
    else:
        rep.add(f"{p}-readme", "README.md", FAIL, "缺失")


# ---------------------------------------------------------------- 输出
def render_text(rep: Report, brief: bool, only_phase: str | None) -> str:
    out: list[str] = []
    out.append(f"PhyWear 复现进度检测  {time.strftime('%Y-%m-%d %H:%M:%S')}")
    out.append(f"工作区 {WS}  ←  快照 {REPO}")
    out.append("")
    for pid, title in PHASES:
        if only_phase and pid != only_phase:
            continue
        st = rep.phase_status(pid)
        good, total = rep.phase_counts(pid)
        head = f"{pid} {title}"
        out.append(f"{ICON[st]} {pad(head, 22)} {good}/{total} 项就绪")
    if only_phase:
        out.append("")
    # 明细
    out.append("")
    out.append("—— 明细 " + "—" * 40)
    for it in rep.items:
        if only_phase and it["phase"] != only_phase:
            continue
        if brief and it["status"] in (OK, SKIP):
            continue
        line = f"  {ICON[it['status']]} {it['id']:<14} {it['title']}"
        if it["detail"]:
            line += f"  —— {it['detail']}"
        out.append(line)

    # 进度条 + 下一步
    line = "  ".join(f"{pid}{ICON[rep.phase_status(pid)]}" for pid, _ in PHASES)
    out.append("")
    out.append("进度：" + line)
    # 先找最早的「❌ 阶段」，没有再看最早的「⚠️ 阶段」；同一阶段内取最靠前的触发项
    trigger = None
    for want in (FAIL, WARN):
        for pid, _title in PHASES:
            hits = [it for it in rep.items if it["phase"] == pid and it["status"] == want]
            if hits:
                trigger = hits[0]
                break
        if trigger:
            break
    if trigger:
        out.append(f"下一步：{NEXT_STEP.get(trigger['phase'], '')}（触发项：{trigger['id']} {trigger['title']}）")
    else:
        out.append("下一步：全绿 —— 可直接按 docs/07 §5 跑真机自检清单")
    # 汇总
    cnt = {k: sum(1 for i in rep.items if i["status"] == k) for k in (OK, WARN, FAIL, INFO, SKIP)}
    out.append(f"汇总：✅{cnt[OK]}  ⚠️{cnt[WARN]}  ❌{cnt[FAIL]}  ℹ️{cnt[INFO]}  ➖{cnt[SKIP]}")
    return "\n".join(out)


def main() -> int:
    global WS, REPO
    ap = argparse.ArgumentParser(description="PhyWear 复现进度检测")
    ap.add_argument("--workspace", default=str(WS))
    ap.add_argument("--repo", default=str(REPO))
    ap.add_argument("--brief", action="store_true", help="只显示非 OK 项")
    ap.add_argument("--json", action="store_true", help="输出 JSON")
    ap.add_argument("--phase", choices=PHASE_ORDER, help="只看某阶段明细")
    ap.add_argument("--strict", action="store_true", help="有 ❌ 时退出码 1")
    ap.add_argument("--quick", action="store_true", help="快检：只跑 P2 代码状态 + P4 构建产物")
    args = ap.parse_args()

    WS, REPO = Path(args.workspace), Path(args.repo)

    rep = Report()
    if args.quick:
        check_code(rep)
        check_build(rep)
    else:
        check_host(rep)
        check_workspace(rep)
        check_code(rep)
        check_markers(rep)
        check_build(rep)
        check_flash(rep)
        check_verify(rep)
        check_delivery(rep)

    if args.json:
        print(json.dumps({
            "workspace": str(WS), "repo": str(REPO),
            "phases": [{"id": pid, "title": title, "status": rep.phase_status(pid),
                        "ok": rep.phase_counts(pid)[0], "total": rep.phase_counts(pid)[1]}
                       for pid, title in PHASES],
            "items": rep.items,
            "summary": {k: sum(1 for i in rep.items if i["status"] == k)
                        for k in (OK, WARN, FAIL, INFO, SKIP)},
        }, ensure_ascii=False, indent=1))
    else:
        print(render_text(rep, args.brief, args.phase))

    if args.strict and any(i["status"] == FAIL for i in rep.items):
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
