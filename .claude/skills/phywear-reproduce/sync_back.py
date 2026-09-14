#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""把 openvela 工作区里的改动**回写**到参赛仓快照（restore_code.py 的逆向操作）。

    python3 sync_back.py              # 演练：只列出工作区与快照不一致的文件
    python3 sync_back.py --execute    # 真回写（工作区 → 参赛仓）
    python3 sync_back.py --execute --all   # 连官方基线参考快照一起回写

为什么必须有这个脚本：铁律 R6 —— 改了 `~/openvela` 不回写快照，换电脑/评审 clone 就看不到。
提交前 `submit_427.sh` 会自动调用它，并在回写后重生成 manifest、核对 P2 全绿。
"""

from __future__ import annotations

import argparse
import json
import shutil
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
from check_progress import md5_of, strip_comments, read_text  # noqa: E402

HOME = Path.home()
WS = Path.home() / "openvela"
REPO = Path.home() / "work/contest2026_427_xinpingqihe"
MANIFEST = HERE / "manifest.json"


def differs(entry: dict) -> bool:
    """工作区文件与快照是否不等价。"""
    dst = WS / entry["dst"]
    snap = REPO / entry["snap"]
    if not dst.is_file():
        return False                      # 工作区没有 → 不是"回写"的事（restore 负责）
    if not snap.is_file():
        return True
    if entry["compare"] == "code":
        return strip_comments(read_text(dst)) != strip_comments(read_text(snap))
    if md5_of(dst) == md5_of(snap):
        return False
    a, b = read_text(dst), read_text(snap)
    return not (a and a.replace("\r\n", "\n") == b.replace("\r\n", "\n"))


def main() -> int:
    global WS, REPO
    ap = argparse.ArgumentParser(description="工作区 → 参赛仓快照 回写")
    ap.add_argument("--workspace", default=str(WS))
    ap.add_argument("--repo", default=str(REPO))
    ap.add_argument("--execute", action="store_true")
    ap.add_argument("--all", action="store_true", help="连参考（官方基线）快照一起回写")
    args = ap.parse_args()

    WS, REPO = Path(args.workspace), Path(args.repo)
    if not MANIFEST.is_file():
        print(f"❌ 缺少 {MANIFEST}（先跑 gen_manifest.py --write）")
        return 2

    files = json.loads(MANIFEST.read_text())["files"]
    todo = [e for e in files
            if (e["critical"] or args.all) and differs(e)]

    mode = "执行回写" if args.execute else "演练（不落盘）"
    print(f"工作区 → 参赛仓 回写 —— {mode}")
    print(f"工作区 {WS}\n参赛仓 {REPO}\n")
    if not todo:
        print("✅ 没有需要回写的文件（快照已是最新）")
        return 0
    for e in todo:
        print(f"  回写  {e['dst']}  →  {e['snap']}")
    if not args.execute:
        print(f"\n（演练结束，共 {len(todo)} 个；加 --execute 真回写）")
        return 0
    for e in todo:
        src, dst = WS / e["dst"], REPO / e["snap"]
        dst.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(src, dst)
    print(f"\n✅ 已回写 {len(todo)} 个文件。下一步：gen_manifest.py --write && check_progress.py")
    return 0


if __name__ == "__main__":
    sys.exit(main())
