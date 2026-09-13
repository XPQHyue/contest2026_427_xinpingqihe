#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""生成 PhyWear 复现清单 manifest.json（快照 ↔ openvela 工作区 映射 + md5）。

用法（在参赛仓根目录执行）：
    python3 .claude/skills/phywear-reproduce/gen_manifest.py            # 只报告
    python3 .claude/skills/phywear-reproduce/gen_manifest.py --check    # CI/自检：有差异则退出码 1
    python3 .claude/skills/phywear-reproduce/gen_manifest.py --write    # 落地 manifest.json

设计要点
  * 参赛仓里的快照是**权威代码状态**：app/phywear（原创） + src/**（公共仓改动） + board/*.defconfig。
  * 本文件定义「快照路径 → 工作区路径」的映射（RULES），是全项目唯一的映射权威；
    check_progress.py / restore_code.py 只读 manifest.json，不再各自硬编码路径。
  * critical=True 表示「本队原创或本队改动」，复现必须一致；False 表示官方基线参考快照，
    repo sync 之后出现差异属正常（只提示，不算失败）。
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import sys
import time
from pathlib import Path

HOME = Path.home()
WS_DEFAULT = Path(os.environ.get("PHYWEAR_WS", HOME / "openvela"))
REPO_DEFAULT = Path(os.environ.get("PHYWEAR_REPO", HOME / "work/contest2026_427_xinpingqihe"))
MANIFEST = Path(__file__).resolve().parent / "manifest.json"

# (规则 id, 说明, 快照相对路径, 工作区相对路径, 比较方式, 是否本队原创/关键)
RULES = [
    ("app", "PhyWear 应用源码（本队原创，LVGL 手表端）",
     "app/phywear", "apps/examples/phywear", "exact", True),
    ("tools", "主机侧脚本（真机自检 / 截图 / 证据生成）",
     "tools", "tools/phywear", "exact", True),
    ("aiagent", "packages/ai_agent（本队新增 4 个 phywear 工具 + 关键词意图 + 构建接线）",
     "src/packages/ai_agent", "packages/ai_agent", "exact", True),
    ("nuttx-drv", "nuttx 传感器驱动（MMC5603 / LTR-303 本队原创）",
     "src/nuttx", "nuttx", "code", True),
    ("bsp-our", "黄山派 BSP 本队改动（启动时序等待、/dev/spk0 注册）",
     "src/vendor/sifli/boards/sf32lb52/sf32lb52_lchspi_ulp/src",
     "vendor/sifli/boards/sf32lb52/sf32lb52_lchspi_ulp/src", "exact", True),
    ("audio", "AUDCODEC 麦克风/扬声器驱动（本队原创）",
     "src/vendor/sifli/boards/sf32lb52/drivers/audio",
     "vendor/sifli/boards/sf32lb52/drivers/audio", "exact", True),
    ("heap", "1.8V LDO 建立等待（本队修复）",
     "src/vendor/sifli/chips/sf32lb52/sifli_allocateheap.c",
     "vendor/sifli/chips/sf32lb52/sifli_allocateheap.c", "exact", True),
    ("lvgl-our", "LVGL 构建修复（本队，官方 PR #41 构建所需）",
     "src/lvgl/src/osal/lv_os_private.h",
     "apps/graphics/lvgl/lvgl/src/osal/lv_os_private.h", "exact", True),
    ("lvgl-ref", "LVGL EPIC 绘制后端（官方 PR #41，参考快照）",
     "src/lvgl", "apps/graphics/lvgl/lvgl", "exact", False),
    ("vendor-ref", "vendor/sifli 官方基线快照（参考，含 EPIC PR #31）",
     "src/vendor", "vendor", "exact", False),
    ("board-ai", "真机 nsh-ai 板级配置（PhyWear + AI Agent + SPK）",
     "board/sf32lb52_lchspi_ulp-nsh-ai.defconfig",
     "vendor/sifli/boards/sf32lb52/sf32lb52_lchspi_ulp/configs/nsh-ai/defconfig", "exact", True),
    ("board-epic", "真机 nsh 板级配置（官方基线 + EPIC 使能）",
     "board/sf32lb52_lchspi_ulp-nsh-epic.defconfig",
     "vendor/sifli/boards/sf32lb52/sf32lb52_lchspi_ulp/configs/nsh/defconfig", "exact", True),
    ("board-sim", "模拟器 goldfish-phywear 板级配置（本队自建，含 AI Agent）",
     "board/goldfish-phywear.defconfig",
     "vendor/openvela/boards/vela/configs/goldfish-arm64-v8a-ap-phywear/defconfig", "exact", True),
]

SKIP_NAMES = {"__pycache__", ".DS_Store", ".git", ".pytest_cache"}
SKIP_SUFFIX = (".o", ".d", ".pyc", ".tmp", ".orig", ".rej")


def md5_of(path: Path) -> str:
    h = hashlib.md5()
    with path.open("rb") as fh:
        for chunk in iter(lambda: fh.read(1 << 20), b""):
            h.update(chunk)
    return h.hexdigest()


def expand(rule) -> list[tuple[str, str]]:
    """把规则展开成 (快照相对路径, 工作区相对路径) 列表。"""
    rid, _desc, snap_rel, dst_rel, _cmp, _crit = rule
    snap = REPO_DEFAULT / snap_rel
    out: list[tuple[str, str]] = []
    if snap.is_dir():
        for root, dirs, files in os.walk(snap):
            dirs[:] = [d for d in dirs if d not in SKIP_NAMES]
            for name in sorted(files):
                if name in SKIP_NAMES or name.endswith(SKIP_SUFFIX):
                    continue
                full = Path(root) / name
                rel = full.relative_to(snap)
                out.append((str(Path(snap_rel) / rel), str(Path(dst_rel) / rel)))
    elif snap.is_file():
        out.append((snap_rel, dst_rel))
    else:
        print(f"⚠️  规则 {rid} 的快照路径不存在：{snap}")
    return out


def build(ws: Path, repo: Path) -> dict:
    files = []
    claimed: set[str] = set()
    for rule in RULES:
        rid, desc, snap_rel, dst_rel, cmp_mode, critical = rule
        for snap, dst in expand(rule):
            if snap in claimed:          # 关键规则优先，避免重复计数
                continue
            claimed.add(snap)
            snap_path = repo / snap
            entry = {
                "rule": rid,
                "rule_desc": desc,
                "snap": snap,
                "dst": dst,
                "compare": cmp_mode,
                "critical": critical,
                "md5": md5_of(snap_path),
                "size": snap_path.stat().st_size,
            }
            dst_path = ws / dst
            entry["present"] = dst_path.is_file()
            files.append(entry)
    files.sort(key=lambda e: (not e["critical"], e["dst"]))
    return {
        "schema": 1,
        "generated_at": time.strftime("%Y-%m-%d %H:%M:%S"),
        "generated_at_epoch": int(time.time()),
        "workspace_default": str(WS_DEFAULT),
        "repo_default": str(REPO_DEFAULT),
        "stats": {
            "files": len(files),
            "critical": sum(1 for f in files if f["critical"]),
            "bytes": sum(f["size"] for f in files),
            "missing_in_workspace": sum(1 for f in files if not f["present"]),
        },
        "files": files,
    }


def main() -> int:
    ap = argparse.ArgumentParser(description="生成 PhyWear 复现清单 manifest.json")
    ap.add_argument("--workspace", default=str(WS_DEFAULT))
    ap.add_argument("--repo", default=str(REPO_DEFAULT))
    ap.add_argument("--write", action="store_true", help="写入 manifest.json")
    ap.add_argument("--check", action="store_true", help="与已有 manifest.json 比对，不一致退出码 1")
    args = ap.parse_args()

    ws, repo = Path(args.workspace), Path(args.repo)
    if not (repo / "app" / "phywear").is_dir():
        print(f"❌ 参赛仓快照不完整：{repo}/app/phywear 不存在")
        return 2
    data = build(ws, repo)
    st = data["stats"]
    print(f"快照根：{repo}")
    print(f"工作区：{ws}")
    print(f"文件数：{st['files']}（关键 {st['critical']}）  共 {st['bytes'] / 1024:.1f} KB")
    print(f"工作区缺失：{st['missing_in_workspace']}")

    if args.check:
        if not MANIFEST.is_file():
            print("❌ manifest.json 不存在，先跑 --write")
            return 1
        old = json.loads(MANIFEST.read_text())
        old_map = {f["snap"]: f["md5"] for f in old.get("files", [])}
        new_map = {f["snap"]: f["md5"] for f in data["files"]}
        added = sorted(set(new_map) - set(old_map))
        removed = sorted(set(old_map) - set(new_map))
        changed = sorted(s for s in set(old_map) & set(new_map) if old_map[s] != new_map[s])
        for label, items in (("新增", added), ("删除", removed), ("内容变化", changed)):
            if items:
                print(f"⚠️  manifest {label} {len(items)} 个：")
                for s in items[:20]:
                    print(f"     {s}")
                if len(items) > 20:
                    print(f"     … 其余 {len(items) - 20} 个")
        if added or removed or changed:
            print("→ 需要重新生成：python3 gen_manifest.py --write")
            return 1
        print("✅ manifest.json 与快照一致")
        return 0

    if args.write:
        MANIFEST.write_text(json.dumps(data, ensure_ascii=False, indent=1) + "\n")
        print(f"✅ 已写入 {MANIFEST}")
    else:
        print("（未加 --write，仅打印；加 --write 落地）")
    return 0


if __name__ == "__main__":
    sys.exit(main())
