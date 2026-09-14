#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""打「作品提交包」—— 按官方模板要求命名，且中文文件名兼容各系统。

    python3 tools/make_submission_zip.py --video ~/桌面/PhyWear演示.mp4
    python3 tools/make_submission_zip.py --video <视频> --photos <照片目录> \
            --report docs/PhyWear_技术报告_官方模板.docx

为什么不用 `zip` 命令：本机 Info-ZIP **不会给非 ASCII 文件名设置 UTF-8 标志位**，
换一台机器（尤其 Windows）解压时中文名会变成乱码（实测 `03_µèÇµ£»µèÑσæè.md`）。
本脚本用 Python `zipfile`，自动设置 UTF-8 标志。

命名规则（官方模板原文）：`<队伍名称>-<作品名称>-<仓库名称>.zip`
**源码与 AI Coding 日志留在专属仓，不放进压缩包。**
"""

from __future__ import annotations

import argparse
import sys
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parents[1]


def add(zf: zipfile.ZipFile, path: Path, arcname: str | None = None) -> bool:
    if not path.exists():
        return False
    if path.is_dir():
        for f in sorted(path.rglob("*")):
            if f.is_file():
                zf.write(f, f"{arcname or path.name}/{f.relative_to(path).as_posix()}")
        return True
    zf.write(path, arcname or path.name)
    return True


def main() -> int:
    ap = argparse.ArgumentParser(description="生成作品提交包（UTF-8 文件名安全）")
    ap.add_argument("--team", default="contest2026_427_xinpingqihe", help="队伍名称")
    ap.add_argument("--work", default="PhyWear", help="作品名称")
    ap.add_argument("--repo-name", default="contest2026_427_xinpingqihe", help="仓库名称")
    ap.add_argument("--report", default="docs/PhyWear_技术报告_官方模板.docx")
    ap.add_argument("--video", help="演示视频（≤5 min，mp4/mov）")
    ap.add_argument("--photos", help="硬件实物多角度照片目录（可选，涉及硬件则需交）")
    ap.add_argument("--poster", help="海报 .pdf/.jpg/.pptx（入围/线下展示才需）")
    ap.add_argument("--ppt", help="答辩 PPT（入围才需）")
    ap.add_argument("--out", help="输出路径（默认 ~/桌面/<队伍>-<作品>-<仓库>.zip）")
    args = ap.parse_args()

    out = Path(args.out) if args.out else Path.home() / "桌面" / f"{args.team}-{args.work}-{args.repo_name}.zip"
    out.parent.mkdir(parents=True, exist_ok=True)
    if out.exists():
        out.unlink()

    included, missing = [], []
    with zipfile.ZipFile(out, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as zf:
        for label, p in (("技术报告", Path(args.report)),
                         ("演示视频", Path(args.video) if args.video else None),
                         ("作品照片", Path(args.photos) if args.photos else None),
                         ("海报", Path(args.poster) if args.poster else None),
                         ("答辩PPT", Path(args.ppt) if args.ppt else None)):
            if p is None:
                if label != "作品照片":
                    missing.append(label)
                continue
            # 相对路径按参赛仓解析
            src = p if p.is_absolute() else (REPO / p)
            if add(zf, src, label if src.is_dir() else f"{label}/{src.name}"):
                included.append(f"{label}: {src}")
            else:
                missing.append(f"{label}（找不到 {src}）")

    # 校验 UTF-8 标志
    zf = zipfile.ZipFile(out)
    nonascii = [i for i in zf.infolist() if any(ord(c) > 127 for c in i.filename)]
    utf8_ok = all(i.flag_bits & 0x800 for i in nonascii)

    print(f"✅ 已生成：{out}  ({out.stat().st_size / 1048576:.1f} MB, {len(zf.infolist())} 个条目)")
    for line in included:
        print("   含 " + line)
    print(f"   非 ASCII 文件名 {len(nonascii)} 个，UTF-8 标志: {'✅ 全部正确' if utf8_ok else '❌ 有缺失'}")
    if missing:
        print("\n⚠️ 还缺（必交项缺了会被扣分/形式审查不过）：")
        for m in missing:
            print("   - " + m)
    print("\n提醒：解压后自己播一遍视频确认可播放；源码与日志留在专属仓，不要打进本包。")
    return 0


if __name__ == "__main__":
    sys.exit(main())
