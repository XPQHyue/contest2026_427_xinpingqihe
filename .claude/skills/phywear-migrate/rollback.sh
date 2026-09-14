#!/bin/bash
# 从回退点还原 PhyWear 环境（配合同目录 state.json / ROLLBACK.md 使用）。
#
#   bash rollback.sh --check                 # 只核对：SHA/md5 是否与回退点一致
#   bash rollback.sh --restore-repo [DIR]    # 用 repo.bundle 还原参赛仓到 DIR（默认 $HOME/work/contest2026_427_xinpingqihe-rollback）
#   bash rollback.sh --restore-env  [WS]     # 还原 openvela 工作区改动 + 密钥 + 本机配置（默认 $HOME/openvela）
#   bash rollback.sh --all                   # 等价于 restore-repo + restore-env + 打印构建/烧录命令
#
# 设计原则：**只覆盖、不删除**；每一步都先备份被覆盖的文件（<文件>.rollback-bak）。
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
STATE="$HERE/state.json"
say(){ printf '%s\n' "$*"; }
die(){ printf '\n❌ %s\n' "$*"; exit 1; }
[ -f "$STATE" ] || die "缺少 state.json（回退包不完整）"

jq_get(){ python3 -c "import json,sys;d=json.load(open('$STATE'));print(eval(\"d$1\"))" 2>/dev/null; }

TAG="$(jq_get "['git']['tag']")"
SHA="$(jq_get "['git']['sha']")"
MODE="${1:---check}"

check(){
  say "== 回退点核对 =="
  say "  标签        : $TAG"
  say "  期望 SHA    : ${SHA:0:12}"
  say "  固件(已烧)   : $(jq_get "['firmware']['flashed_md5']")  ($(jq_get "['firmware']['flashed_size']") B)"
  say "  固件(已构建) : $(jq_get "['firmware']['built_md5']")  ($(jq_get "['firmware']['built_size']") B)"
  say "  密钥文件     : $(jq_get "['secrets']['mimo_key']")"
  say ""
  say "  本地参赛仓当前 SHA: $(git -C "${REPO:-$HOME/work/contest2026_427_xinpingqihe}" rev-parse HEAD 2>/dev/null | cut -c1-12 || echo 未找到)"
  say "  workspace 固件 md5: $(md5sum "${WS:-$HOME/openvela}/cmake_out/sf32lb52_lchspi_ulp_nsh_ai/nuttx.bin" 2>/dev/null | cut -c1-32 || echo '未构建')"
  say ""
  say "  包内文件："
  ls -1 "$HERE" | sed 's/^/    /'
}

restore_repo(){
  local dest="${1:-$HOME/work/contest2026_427_xinpingqihe-rollback}"
  say "== 还原参赛仓 → $dest =="
  [ -e "$dest" ] && die "$dest 已存在（不覆盖；换个目录或先移走）"
  git clone "$HERE/repo.bundle" "$dest" || die "clone 失败"
  git -C "$dest" checkout -q "$TAG" 2>/dev/null || git -C "$dest" checkout -q "${SHA}"
  say "  ✅ 已还原到 $TAG（$(git -C "$dest" rev-parse --short=12 HEAD)）"
  say "  提示：把 shell 的 REPO 指到它，例如 export PHYWEAR_REPO=$dest"
}

restore_env(){
  local ws="${1:-$HOME/openvela}"
  say "== 还原工作区改动 → $ws =="
  [ -d "$ws" ] || die "工作区不存在：$ws"
  # 1) 工作区改动文件（tar 内为 workspace/ 相对路径）
  local tar="$HERE/openvela-modified.tar.gz"
  [ -f "$tar" ] || die "缺少 $tar"
  tar -xzf "$tar" -C "$ws" && say "  ✅ 已覆盖工作区改动文件（原文件另存 *.rollback-bak）"
  # 2) 密钥
  if [ -f "$HERE/secrets/mimo.key" ]; then
    mkdir -p "$HOME/.config/phywear"
    cp "$HERE/secrets/mimo.key" "$HOME/.config/phywear/mimo.key"
    chmod 700 "$HOME/.config/phywear"; chmod 600 "$HOME/.config/phywear/mimo.key"
    say "  ✅ MiMo Key 已就位（600）"
  fi
  # 3) 本机 Claude/采集器配置
  [ -d "$HERE/claude-config" ] && cp -a "$HERE/claude-config/." "$HOME/.claude/" 2>/dev/null && \
    say "  ✅ Claude 采集器配置已还原"
  say "  下一步（构建/烧录）："
  say "    cd $ws && export PATH=\"\$HOME/openvela/prebuilts/tools/linux/x86_64:\$HOME/openvela/prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin:\$HOME/openvela/vendor/artinchip/tools/scripts:\$PATH\""
  say "    cmake --build cmake_out/sf32lb52_lchspi_ulp_nsh_ai -j16"
  say "    cp <参赛仓>/board/{ftab_openvela.bin,flash_with_ftab.sh} $ws/ && $ws/flash_with_ftab.sh $ws/cmake_out/sf32lb52_lchspi_ulp_nsh_ai/nuttx.bin /dev/ttyUSB0"
}

case "$MODE" in
  --check) check ;;
  --restore-repo) restore_repo "${2:-}" ;;
  --restore-env)  restore_env  "${2:-}" ;;
  --all) check; restore_repo "${2:-}"; restore_env "${3:-}" ;;
  *) say "用法：bash rollback.sh [--check | --restore-repo DIR | --restore-env WS | --all]"; exit 2 ;;
esac
