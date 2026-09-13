#!/bin/bash
# 重新生成 PhyWear 的 5 个字号字体子集（拉丁 Montserrat + 中文 DroidSansFallback）。
#
# 为什么需要脚本：CJK 子集必须覆盖 g_pw_str_zh[] 里出现的**全部**汉字，
# 否则界面上会显示缺字方框（真机踩过：新加的"播放/扬声器"等字缺失）。
# 每次往 i18n 表里加中文，跑一次本脚本即可。
#
# 依赖：lv_font_conv（npm 全局）、DroidSansFallbackFull.ttf、LVGL 自带 Montserrat。
set -euo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
APP="${APP:-$HOME/openvela/apps/examples/phywear}"
LFC="${LFC:-$HOME/.npm-global/lib/node_modules/lv_font_conv/lv_font_conv.js}"
MONTSERRAT="$HOME/openvela/apps/graphics/lvgl/lvgl/scripts/built_in_font/Montserrat-Medium.ttf"
DROID="/usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf"

[ -f "$LFC" ] || { echo "缺少 lv_font_conv: $LFC"; exit 1; }
[ -f "$MONTSERRAT" ] || { echo "缺少 Montserrat: $MONTSERRAT"; exit 1; }
[ -f "$DROID" ] || { echo "缺少 DroidSansFallbackFull: $DROID"; exit 1; }

# 1) 从 ZH 表 + 应用源码的字面量里收集所有需要渲染的 CJK 码点
RANGES=$(python3 "$HERE/gen_font_ranges.py" "$APP")
echo "CJK 码点区间: $(echo "$RANGES" | tr ',' '\n' | wc -l) 段"

LATIN="0x20-0x7F,0xB0,0xB2,0x2014,0x2022"

for SIZE in 14 16 20 24 28; do
  TMP=$(mktemp -d)
  node "$LFC" --no-compress --no-prefilter --bpp 4 --size "$SIZE" \
    --font "$MONTSERRAT" -r "$LATIN" \
    --font "$DROID" -r "$RANGES" \
    --format lvgl -o "$TMP/pw_font_$SIZE.c" --force-fast-kern-format >/dev/null

  # 保留原文件的项目头注释（lv_font_conv 自己的 Opts 注释会重新生成）
  python3 - "$APP/pw_font_$SIZE.c" "$TMP/pw_font_$SIZE.c" <<'PY'
import sys, re
dst, src = sys.argv[1], sys.argv[2]
old = open(dst, encoding='utf-8', errors='ignore').read()
m = re.search(r'^/\*{5,}\s*$', old, re.M)
header = old[:m.start()] if m else ''
new = open(src, encoding='utf-8', errors='ignore').read()
open(dst, 'w', encoding='utf-8').write(header + new)
PY
  rm -rf "$TMP"
  echo "  已生成 pw_font_$SIZE.c ($(stat -c%s "$APP/pw_font_$SIZE.c") B)"
done

echo "完成。记得重新编译并真机检查中文是否还有缺字方框。"
