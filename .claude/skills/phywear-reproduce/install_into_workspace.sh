#!/bin/bash
# 把本 SKILL 挂进 openvela 工作区，让 `claude`（或其它 AI CLI）在 ~/openvela 里能自动发现它。
#
#   bash .claude/skills/phywear-reproduce/install_into_workspace.sh           # 软链（默认）
#   bash .claude/skills/phywear-reproduce/install_into_workspace.sh --copy    # 复制一份
#   bash .claude/skills/phywear-reproduce/install_into_workspace.sh --remove  # 卸载
#
# 说明：赛事采集器只在含 .repo/ 的 openvela 工作区内记录 AI 会话，
#       所以「用 claude 跑复现」必须在 ~/openvela 下执行 —— 本脚本就是把 SKILL 挂过去。
#       ⚠️ `repo sync` 可能会重置 ~/openvela/.claude/skills/，重置后重跑本脚本即可。
set -eu

SRC="$(cd "$(dirname "$0")" && pwd)"
WS="${PHYWEAR_WS:-$HOME/openvela}"
DST="$WS/.claude/skills/$(basename "$SRC")"
MODE="${1:-}"

if [ ! -d "$WS/.claude" ]; then
  echo "❌ 找不到 $WS/.claude（openvela 工作区不对？用 PHYWEAR_WS=... 指定）"
  exit 1
fi

case "$MODE" in
  --remove)
    rm -rf "$DST"
    echo "✅ 已移除 $DST"
    ;;
  --copy)
    rm -rf "$DST"; mkdir -p "$DST"
    cp -a "$SRC/." "$DST/"
    echo "✅ 已复制到 $DST"
    ;;
  *)
    rm -rf "$DST"
    ln -s "$SRC" "$DST"
    echo "✅ 已软链 $DST -> $SRC"
    ;;
esac

echo
echo "在 ~/openvela 下即可直接使用："
echo "  python3 .claude/skills/phywear-reproduce/check_progress.py"
