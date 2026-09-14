#!/bin/bash
# 黄山派 SF32LB52 烧录（含 ROM FlashTable / ftab）
#
# ⚠️ 关键：SiFli ROM 必须先读到 flash 起始 0x12000000 的 ftab，才知道去哪取镜像。
#    本板出厂 flash 该区域是空的 —— 不写 ftab 就会只打印 SFBL 并停在下载模式。
#    ftab 里 bootloader 条目的 xip_base 必须是 0x12010000（原地 XIP），
#    以匹配 openvela 链接脚本 flash ORIGIN = 0x12010000。
#
# 用法: ./flash_with_ftab.sh [nuttx.bin 路径] [串口]
set -u

BIN="${1:-cmake_out/sf32lb52_lchspi_ulp_nsh_v2/nuttx.bin}"
PORT="${2:-/dev/ttyUSB0}"
# ftab 查找顺序：$FTAB -> 参赛仓 board/ -> 桌面 SKILLS（换电脑后前两者就够）
REPO="${REPO:-$HOME/work/contest2026_427_xinpingqihe}"
if [ -z "${FTAB:-}" ]; then
  for cand in "$REPO/board/ftab_openvela.bin" \
              "$HOME/SKILLS/openvela-huangshan-pi/assets/ftab_openvela.bin"; do
    [ -f "$cand" ] && { FTAB="$cand"; break; }
  done
fi
FTAB="${FTAB:-}"
BAUD=1000000

[ -f "$BIN" ]  || { echo "错误: 固件不存在 $BIN"; exit 1; }
[ -f "$FTAB" ] || { echo "错误: ftab 不存在 $FTAB"; exit 1; }

echo "ftab : $FTAB  ($(stat -c%s "$FTAB") bytes)"
echo "固件 : $BIN  ($(stat -c%s "$BIN") bytes)"
echo "串口 : $PORT @ $BAUD"
echo

echo "=== [1/3] 写 ftab -> 0x12000000 ==="
sftool -c SF32LB52 -p "$PORT" -b $BAUD \
  --before default_reset --after no_reset \
  write_flash "$FTAB@0x12000000" || exit 1

echo
echo "=== [2/3] 写固件 -> 0x12010000 ==="
sftool -c SF32LB52 -p "$PORT" -b $BAUD \
  --before default_reset --after soft_reset \
  write_flash "$BIN@0x12010000" || exit 1

echo
echo "=== [3/3] 读回校验（ftab 前 256B + 固件前 64KB）==="
T=$(mktemp -d)
sftool -c SF32LB52 -p "$PORT" -b $BAUD --before default_reset --after no_reset \
  read_flash "$T/ftab_rb.bin@0x12000000:256" >/dev/null 2>&1
head -c 256 "$FTAB" > "$T/ftab_src.bin"
cmp -s "$T/ftab_rb.bin" "$T/ftab_src.bin" && echo "  ftab   ✅ 逐字节一致" || echo "  ftab   ❌ 不一致"

sftool -c SF32LB52 -p "$PORT" -b $BAUD --before default_reset --after soft_reset \
  read_flash "$T/bin_rb.bin@0x12010000:65536" >/dev/null 2>&1
head -c 65536 "$BIN" > "$T/bin_src.bin"
cmp -s "$T/bin_rb.bin" "$T/bin_src.bin" && echo "  固件   ✅ 逐字节一致" || echo "  固件   ❌ 不一致"
rm -rf "$T"

echo
echo "烧录完成。串口查看（必须让 RTS 在 open 时 deasserted）："
echo "  picocom -b 1000000 --noreset --lower-rts --lower-dtr $PORT"
echo "期望看到:  SFBL  ->  ABCD  ->  ADC calibration...  ->  NuttShell (NSH) / nsh>"
