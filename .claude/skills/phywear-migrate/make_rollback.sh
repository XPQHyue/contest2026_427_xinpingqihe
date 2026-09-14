#!/bin/bash
# 生成「回退点」包：把当前状态整体冻结，可一键还原。
#
#   bash make_rollback.sh                 # 生成到 ~/桌面/PhyWear-rollback-<日期>/
#   OUT=/tmp/rb bash make_rollback.sh      # 指定输出目录
#
# 包内容：
#   state.json             全部 SHA / md5 / 尺寸 / PR 状态（回退点的"身份证"）
#   repo.bundle            参赛仓全量 git bundle（含所有分支与 tag，可离线 clone）
#   openvela-modified.tar.gz  openvela 工作区里我们改过的文件（含 .rollback-bak 备份）
#   firmware/              已构建固件 nuttx.bin + System.map + .config
#   secrets/mimo.key       MiMo Key（600）—— 仅内部传递
#   claude-config/         采集器与 Claude 本机配置（不含任何密钥）
#   rollback.sh / ROLLBACK.md  还原脚本与说明
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="${REPO:-$HOME/work/contest2026_427_xinpingqihe}"
WS="${PHYWEAR_WS:-$HOME/openvela}"
DATE="$(date +%Y%m%d-%H%M)"
TAG="${TAG:-phywear-handover-$DATE}"
OUT="${OUT:-$HOME/桌面/PhyWear-rollback-$DATE}"
FIRMWARE="$WS/cmake_out/sf32lb52_lchspi_ulp_nsh_ai"
say(){ printf '%s\n' "$*"; }

[ -d "$REPO/.git" ] || { echo "❌ 参赛仓不存在：$REPO"; exit 1; }
mkdir -p "$OUT"/{firmware,secrets,claude-config}
cd "$REPO" || exit 1

say "== 1/6 打 tag（回退锚点）=="
git tag -a "$TAG" -m "PhyWear 交接回退点 $(date +%F\ %H:%M)（MiMo 接手中）" 2>/dev/null || say "  （tag 已存在，复用）"
say "  tag = $TAG → $(git rev-parse --short=12 "$TAG")"
git push fork "$TAG" >/dev/null 2>&1 && say "  ✅ 已推到 fork" || say "  ⚠️ 推 tag 失败（网络），本地 tag 仍在"

say "== 2/6 git bundle =="
git bundle create "$OUT/repo.bundle" --all >/dev/null 2>&1 && say "  ✅ $(du -h "$OUT/repo.bundle" | cut -f1)"

say "== 3/6 工作区改动打包 =="
python3 - "$REPO" "$WS" "$OUT" <<'PY'
import json, pathlib, subprocess, sys, tarfile, os
repo, ws, out = map(pathlib.Path, sys.argv[1:4])
man = json.loads((repo/'.claude/skills/phywear-reproduce/manifest.json').read_text())
files = [f['dst'] for f in man['files']]
extra = ['flash_with_ftab.sh', 'tools/phywear/gen_skill_blob.py']
added = 0
with tarfile.open(out/'openvela-modified.tar.gz', 'w:gz') as tf:
    for rel in files + extra:
        p = ws/rel
        if p.is_file():
            tf.add(p, arcname=rel); added += 1
print(f"  打包 {added} 个工作区文件")
PY
say "  ✅ $(du -h "$OUT/openvela-modified.tar.gz" | cut -f1)"

say "== 4/6 固件与配置 =="
for f in nuttx.bin System.map .config; do
  [ -f "$FIRMWARE/$f" ] && cp "$FIRMWARE/$f" "$OUT/firmware/" && say "  ✅ $f"
done
[ -f "$HOME/.config/phywear/mimo.key" ] && cp "$HOME/.config/phywear/mimo.key" "$OUT/secrets/" && \
  chmod 600 "$OUT/secrets/mimo.key" && say "  ✅ MiMo Key（⚠️ 仅内部传递）"
for f in contest-collector.env settings.json; do
  [ -f "$HOME/.claude/$f" ] && cp "$HOME/.claude/$f" "$OUT/claude-config/" && say "  ✅ ~/.claude/$f"
done
[ -d "$HOME/.claude/contest-shared" ] && cp -a "$HOME/.claude/contest-shared" "$OUT/claude-config/" && say "  ✅ contest-shared/"

say "== 5/6 状态清单 state.json =="
python3 - "$REPO" "$WS" "$OUT" "$TAG" <<'PY'
import hashlib, json, pathlib, subprocess, sys, time
repo, ws, out, tag = pathlib.Path(sys.argv[1]), pathlib.Path(sys.argv[2]), pathlib.Path(sys.argv[3]), sys.argv[4]
def sh(*a):
    return subprocess.run(a, capture_output=True, text=True).stdout.strip()
def md5(p):
    p = pathlib.Path(p)
    if not p.is_file(): return ""
    h = hashlib.md5()
    with p.open('rb') as f:
        for b in iter(lambda: f.read(1<<20), b''): h.update(b)
    return h.hexdigest()
fw = ws/'cmake_out/sf32lb52_lchspi_ulp_nsh_ai/nuttx.bin'
state = {
 "created_at": time.strftime("%Y-%m-%d %H:%M:%S"),
 "git": {
   "tag": tag,
   "sha": sh("git","-C",str(repo),"rev-parse","HEAD"),
   "tag_sha": sh("git","-C",str(repo),"rev-parse",tag+"^{commit}"),
   "branch": sh("git","-C",str(repo),"branch","--show-current"),
   "fork_default_sha": sh("git","-C",str(repo),"ls-remote","fork","refs/heads/dev-ai-contest-2026").split("\t")[0],
   "origin_default_sha": sh("git","-C",str(repo),"ls-remote","origin","refs/heads/dev-ai-contest-2026").split("\t")[0],
   "pr": "PR #11 merged（19 提交）、PR #12 merged（ftab 修复）；未合并 PR = 0",
 },
 "firmware": {
   "built_md5": md5(fw), "built_size": fw.stat().st_size if fw.is_file() else 0,
   "flashed_md5": "eb13cf4ba9aea4ee9ba7d4fcf0f77383", "flashed_size": 2056576,
   "note": "板上为 flashed_md5；built_md5 含 Skill 输出规范（5,165 B），构建完成待烧录",
 },
 "secrets": {"mimo_key": "secrets/mimo.key（Token Plan，tp- 前缀；仅内部传递，勿入库）"},
 "logs": {"sessions": len(list((repo/'logs').rglob('*.jsonl')))},
 "workspace": str(ws), "repo": str(repo),
}
(out/'state.json').write_text(json.dumps(state, ensure_ascii=False, indent=1))
print("  ✅ state.json 已写出")
PY

say "== 6/6 说明与脚本 =="
cp "$HERE/rollback.sh" "$OUT/rollback.sh"; chmod +x "$OUT/rollback.sh"
cat > "$OUT/ROLLBACK.md" <<'EOF'
# PhyWear 回退点 —— 一眼看懂

本目录冻结了 **MiMo 接手前**的完整状态：代码、工作区改动、固件、密钥、本机配置、PR 状态。
任何一步被改坏，都可以回到这里。

## 先核对（不落盘）
```bash
bash rollback.sh --check
```

## 还原
```bash
bash rollback.sh --restore-repo ~/work/contest2026_427_xinpingqihe-rollback   # 用 repo.bundle 还原参赛仓到指定 tag
bash rollback.sh --restore-env  ~/openvela                                    # 还原工作区改动 + 密钥 + Claude 配置
bash rollback.sh --all                                                        # 两步都做 + 打印构建/烧录命令
```
约定：**只覆盖、不删除**；被覆盖的文件会被原样备份成 `<文件>.rollback-bak`。

## 回退点内容
| 路径 | 内容 |
|---|---|
| `state.json` | 全部 SHA / md5 / 尺寸 / PR 状态（回退点身份证） |
| `repo.bundle` | 参赛仓全量 git bundle（含所有分支与 tag，可离线 clone） |
| `openvela-modified.tar.gz` | openvela 工作区里我们改过的文件 |
| `firmware/` | 已构建固件 `nuttx.bin` + `System.map` + `.config` |
| `secrets/mimo.key` | MiMo Key（600）——**仅内部传递，勿上传公开位置** |
| `claude-config/` | 采集器与 Claude 本机配置（不含密钥） |

## 板上固件的还原
板子当前跑的是 `eb13cf4b…`（2,056,576 B）。要回到它：
```bash
cp <参赛仓>/board/{ftab_openvela.bin,flash_with_ftab.sh} ~/openvela/
~/openvela/flash_with_ftab.sh <某个 nuttx.bin> /dev/ttyUSB0
```
（`firmware/nuttx.bin` 是"已构建待烧"的那份，md5 见 `state.json`。）
EOF
say "  ✅ ROLLBACK.md / rollback.sh"
say ""
say "✅ 回退点已生成：$OUT"
say "   ⚠️ 含明文 MiMo Key：只在本队内部传递"
