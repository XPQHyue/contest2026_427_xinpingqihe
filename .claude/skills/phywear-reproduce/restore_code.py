#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把参赛仓快照里的代码恢复到 openvela 工作区（复现的「代码落位」那一步）。

    python3 .claude/skills/phywear-reproduce/restore_code.py                  # 演练（默认，不落盘）
    python3 .claude/skills/phywear-reproduce/restore_code.py --execute        # 真恢复（先自动备份）
    python3 .claude/skills/phywear-reproduce/restore_code.py --execute --all  # 连官方基线参考快照一起恢复
    python3 .claude/skills/phywear-reproduce/restore_code.py --execute --filter 'phywear_tone*'

安全约定：
  * 默认只处理 critical（本队原创/本队改动）文件；官方基线快照要显式 --all。
  * 只覆盖 manifest.json 里列出的目标文件，绝不删除任何文件。
  * --execute 时默认把被覆盖的旧文件备份到 ~/openvela/backup/reproduce-<时间戳>/，
    用 --no-backup 可关闭（不建议）。
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
import time
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from check_progress import md5_of, strip_comments, read_text  # noqa: E402

HOME = Path.home()
WS_DEFAULT = HOME / "openvela"
REPO_DEFAULT = HOME / "work/contest2026_427_xinpingqihe"
MANIFEST = HERE / "manifest.json"


def state(entry: dict, ws: Path, repo: Path) -> str:
    dst = ws / entry["dst"]
    if not dst.is_file():
        return "missing"
    snap = repo / entry["snap"]
    if entry["compare"] == "code":
        if strip_comments(read_text(snap)) == strip_comments(read_text(dst)):
            return "equivalent"
        return "diff"
    if md5_of(dst) == entry["md5"]:
        return "ok"
    a, b = read_text(snap), read_text(dst)
    if a and a.replace("\r\n", "\n") == b.replace("\r\n", "\n"):
        return "crlf"
    return "diff"


def main() -> int:
    ap = argparse.ArgumentParser(description="从参赛仓快照恢复代码到 openvela 工作区")
    ap.add_argument("--workspace", default=str(WS_DEFAULT))
    ap.add_argument("--repo", default=str(REPO_DEFAULT))
    ap.add_argument("--execute", action="store_true", help="真正写入（默认只演练）")
    ap.add_argument("--all", action="store_true", help="连非关键的官方基线快照一起恢复")
    ap.add_argument("--force", action="store_true", help="连「仅注释/换行差异」的文件也重写")
    ap.add_argument("--filter", default="", help="只处理 dst 路径包含该子串的文件")
    ap.add_argument("--no-backup", action="store_true", help="不备份被覆盖的文件")
    args = ap.parse_args()

    ws, repo = Path(args.workspace), Path(args.repo)
    if not MANIFEST.is_file():
        print(f"❌ 缺少 {MANIFEST}，先跑 gen_manifest.py --write")
        return 2
    if not (repo / "app/phywear").is_dir():
        print(f"❌ 快照不完整：{repo}/app/phywear 不存在")
        return 2

    files = json.loads(MANIFEST.read_text())["files"]
    todo, skip = [], []
    for e in files:
        if not e["critical"] and not args.all:
            continue
        if args.filter and args.filter not in e["dst"]:
            continue
        st = state(e, ws, repo)
        if st == "ok" or (st in ("equivalent", "crlf") and not args.force):
            skip.append((e, st))
        else:
            todo.append((e, st))

    mode = "执行恢复" if args.execute else "演练（不落盘）"
    scope = "全部快照" if args.all else "仅本队原创/改动（critical）"
    print(f"PhyWear 代码恢复 —— {mode}；范围：{scope}")
    print(f"工作区 {ws}\n快照   {repo}\n")
    print(f"需要恢复 {len(todo)} 个，已一致跳过 {len(skip)} 个\n")
    for e, st in todo:
        label = {"missing": "新增", "diff": "覆盖", "equivalent": "重写(注释差异)", "crlf": "重写(换行差异)"}[st]
        print(f"  {label}  {e['dst']}")
    if not todo:
        print("✅ 全部一致，无需恢复")
        return 0
    if not args.execute:
        print("\n（演练结束；确认后加 --execute 真正恢复，会自动备份到 ~/openvela/backup/）")
        return 0

    stamp = time.strftime("%Y%m%d-%H%M%S")
    backup = ws / "backup" / f"reproduce-{stamp}"
    done = 0
    for e, _st in todo:
        src, dst = repo / e["snap"], ws / e["dst"]
        dst.parent.mkdir(parents=True, exist_ok=True)
        if dst.is_file() and not args.no_backup:
            b = backup / e["dst"]
            b.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(dst, b)
        shutil.copy2(src, dst)
        done += 1
    print(f"\n✅ 已恢复 {done} 个文件" + ("" if args.no_backup else f"；旧文件备份在 {backup}"))
    print("下一步：python3 .claude/skills/phywear-reproduce/check_progress.py --brief")
    return 0


if __name__ == "__main__":
    sys.exit(main())
