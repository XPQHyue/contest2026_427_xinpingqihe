#!/bin/bash
# 提交 427 参赛仓 —— 唯一允许的提交入口（严格流程 + 红线预检）。
#
#   bash submit_427.sh                          # 演练（默认，不落盘/不推送）
#   bash submit_427.sh --execute -m "feat: …"   # 真提交并推送（含同步默认分支）
#   bash submit_427.sh --no-default             # 只推工作分支，不动默认分支
#   BRANCH=xxx bash submit_427.sh --execute     # 指定工作分支（默认 dev-ai-contest-2026，即合并后的主线）
#
# 顺序：回写快照 → 重生成清单 → P2 核对 → Skill blob 校验 → 红线预检 → 提交
#       → 推工作分支 → 备份并同步默认分支 → 远端 SHA 校验 → 打印 PR 信息
set -uo pipefail

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="${REPO:-$HOME/work/contest2026_427_xinpingqihe}"
WS="${PHYWEAR_WS:-$HOME/openvela}"
BRANCH="${BRANCH:-dev-ai-contest-2026}"
DEFAULT_BRANCH="${DEFAULT_BRANCH:-dev-ai-contest-2026}"
FORK_REMOTE="${FORK_REMOTE:-fork}"
AUTHOR_NAME="XPQHyue"; AUTHOR_EMAIL="15770782523@163.com"
MSG=""; EXECUTE=0; SYNC_DEFAULT=1; MAX_MB=5

while [ $# -gt 0 ]; do
  case "$1" in
    --execute) EXECUTE=1 ;;
    --no-default) SYNC_DEFAULT=0 ;;
    -m) shift; MSG="${1:-}" ;;
    *) echo "未知参数：$1"; exit 2 ;;
  esac
  shift
done

say(){ printf '%s\n' "$*"; }
step(){ printf '\n=== %s ===\n' "$*"; }
die(){ printf '\n❌ 中止：%s\n' "$*"; exit 1; }

[ -d "$REPO/.git" ] || die "参赛仓不存在：$REPO"
cd "$REPO" || exit 1
TODAY="$(date +%Y%m%d)"
MODE=$([ "$EXECUTE" = 1 ] && echo "执行" || echo "演练（不落盘、不推送）")
step "0/9 提交 427 参赛仓 —— $MODE ｜ 分支 $BRANCH ｜ 默认分支 $DEFAULT_BRANCH"
say "仓库：$REPO"
say "工作区：$WS"

# ── 1 回写快照 ───────────────────────────────────────────────
step "1/9 工作区 → 参赛仓 快照回写（sync_back.py）"
if [ "$EXECUTE" = 1 ]; then
  python3 .claude/skills/phywear-reproduce/sync_back.py --execute --workspace "$WS" --repo "$REPO" || die "回写失败"
else
  python3 .claude/skills/phywear-reproduce/sync_back.py --workspace "$WS" --repo "$REPO"
fi

# ── 2 清单 ──────────────────────────────────────────────────
step "2/9 重生成清单 manifest.json"
if [ "$EXECUTE" = 1 ]; then
  python3 .claude/skills/phywear-reproduce/gen_manifest.py --write --workspace "$WS" --repo "$REPO" | tail -3 \
    || die "清单生成失败"
else
  python3 .claude/skills/phywear-reproduce/gen_manifest.py --workspace "$WS" --repo "$REPO" | tail -3
fi

# ── 3 P2 核对 ───────────────────────────────────────────────
step "3/9 一致性核对（要求 P2 全绿）"
python3 .claude/skills/phywear-reproduce/check_progress.py --workspace "$WS" --repo "$REPO" --phase P2 --brief --strict
[ $? -ne 0 ] && die "快照与工作区仍不一致（P2 有 ❌）：先修平再提交（restore_code.py / sync_back.py）"
say "✅ P2 一致"

# ── 4 Skill blob ────────────────────────────────────────────
step "4/9 Skill blob 校验（改过 skills/*.md 必须重生成）"
if git diff --name-only -- 'app/phywear/skills' | grep -q . ; then
  if [ "$EXECUTE" = 1 ]; then
    python3 "$WS/tools/phywear/gen_skill_blob.py" >/dev/null || die "gen_skill_blob 失败"
    python3 "$WS/tools/phywear/gen_skill_blob.py" --check || die "blob 与正本不一致"
    python3 .claude/skills/phywear-reproduce/sync_back.py --execute --workspace "$WS" --repo "$REPO" >/dev/null
  fi
  say "✅ Skill blob 已同步并通过 --check"
else
  say "（本次未改 Skill）"
fi

# ── 5 红线预检 ──────────────────────────────────────────────
step "5/9 红线预检（密钥 / 禁止路径 / 大文件）"
git add -A
# 关键交付物可能被 .gitignore 命中（如 *.bin），必须强制跟踪并校验
if [ -f board/ftab_openvela.bin ]; then git add -f board/ftab_openvela.bin; fi
for must in board/ftab_openvela.bin; do
  if [ -f "$must" ] && ! git ls-files --error-unmatch "$must" >/dev/null 2>&1; then
    die "关键交付物未被 git 跟踪（检查 .gitignore）：$must"
  fi
done
STAGED="$(git diff --cached --name-only)"
[ -z "$STAGED" ] && say "（没有暂存改动）"
FAIL=0
# 5.1 密钥
if git diff --cached -U0 | grep -Eq '(tp-[a-z0-9]{20,}|sk-[A-Za-z0-9]{20,}|ghp_[A-Za-z0-9]{20,}|github_pat_[A-Za-z0-9_]{20,})'; then
  say "❌ 检测到疑似密钥内容（tp-/sk-/ghp_/github_pat_）"; FAIL=1
fi
# 5.2 禁止路径 + 二进制白名单
ALLOW_BIN='^board/ftab_openvela.bin$'
while IFS= read -r f; do
  [ -z "$f" ] && continue
  case "$f" in
    cmake_out/*|*/cmake_out/*|*.o|*.a|*.zip|*.key|.config|*/.config|secrets/*|*/mimo.key)
      say "❌ 禁止提交的路径：$f"; FAIL=1 ;;
    *.bin|*.png|*.jpg|*.docx|*.xlsx|*.ttf)
      if ! printf '%s' "$f" | grep -Eq "$ALLOW_BIN$"; then
        # 图片/文档/字体属于证据与报告，允许；仅 .bin 需白名单
        case "$f" in *.bin) say "❌ 二进制需白名单（仅 board/ftab_openvela.bin）：$f"; FAIL=1 ;; esac
      fi ;;
  esac
done <<< "$STAGED"
# 5.3 大文件
while IFS= read -r f; do
  [ -z "$f" ] || [ ! -f "$f" ] && continue
  MB=$(( $(stat -c%s "$f") / 1048576 ))
  [ "$MB" -ge "$MAX_MB" ] && { say "❌ 文件过大（${MB} MB ≥ ${MAX_MB} MB）：$f"; FAIL=1; }
done <<< "$STAGED"
[ "$FAIL" = 1 ] && die "红线预检未通过（见上）"
say "✅ 红线预检通过（$(printf '%s\n' "$STAGED" | grep -c . ) 个文件待提交）"

# ── 6 提交 ──────────────────────────────────────────────────
step "6/9 提交（作者 $AUTHOR_NAME <$AUTHOR_EMAIL>）"
if git diff --cached --quiet; then
  say "（无改动，跳过提交）"
else
  [ -z "$MSG" ] && MSG="chore(phywear): 同步改动 $(date +%F\ %H:%M)"
  if [ "$EXECUTE" = 1 ]; then
    git -c user.name="$AUTHOR_NAME" -c user.email="$AUTHOR_EMAIL" commit -q -m "$MSG" || die "提交失败"
  fi
  say "提交信息：$MSG"
fi
HEAD_SHA="$(git rev-parse HEAD)"
say "本地 HEAD：${HEAD_SHA:0:12}"
[ "$EXECUTE" = 0 ] && { say "\n（演练结束；确认后加 --execute 真提交并推送）"; exit 0; }

# ── 7 推工作分支 ────────────────────────────────────────────
step "7/9 推送工作分支 → $FORK_REMOTE/$BRANCH"
git push "$FORK_REMOTE" "HEAD:$BRANCH" || die "推送失败（网络/权限）"

# ── 8 同步默认分支（带备份） ─────────────────────────────────
if [ "$SYNC_DEFAULT" = 1 ]; then
  step "8/9 同步默认分支（先备份旧 tip，再 --force-with-lease）"
  OLD="$(git ls-remote "$FORK_REMOTE" "refs/heads/$DEFAULT_BRANCH" | awk '{print $1}')"
  if [ -n "$OLD" ] && [ "$OLD" != "$HEAD_SHA" ]; then
    git push "$FORK_REMOTE" "$OLD:refs/heads/backup-default-$TODAY" >/dev/null 2>&1 \
      && say "已备份旧 tip → backup-default-$TODAY (${OLD:0:12})"
    git push --force-with-lease="$DEFAULT_BRANCH:$OLD" "$FORK_REMOTE" "HEAD:$DEFAULT_BRANCH" \
      || die "默认分支同步失败（注意：绝不允许对官方仓 force-push）"
  else
    say "（默认分支已是最新，无需同步）"
  fi
else
  step "8/9 跳过默认分支同步（--no-default）"
fi

# ── 9 远端校验 ──────────────────────────────────────────────
step "9/9 远端 SHA 校验"
REMOTE_HEAD="$(git ls-remote "$FORK_REMOTE" "refs/heads/$BRANCH" | awk '{print $1}')"
REMOTE_DEF="$(  git ls-remote "$FORK_REMOTE" "refs/heads/$DEFAULT_BRANCH" | awk '{print $1}')"
say "本地   HEAD           ${HEAD_SHA:0:12}"
say "远端   $BRANCH  ${REMOTE_HEAD:0:12}"
[ "$SYNC_DEFAULT" = 1 ] && say "远端   $DEFAULT_BRANCH  ${REMOTE_DEF:0:12}"
[ "$REMOTE_HEAD" = "$HEAD_SHA" ] || die "工作分支远端 SHA 与本地不一致（不要口头声称已推送）"
if [ "$SYNC_DEFAULT" = 1 ] && [ "$REMOTE_DEF" != "$HEAD_SHA" ]; then die "默认分支远端 SHA 不一致"; fi
say "✅ 远端校验通过"

cat <<EOF

──────────────────────────────────────────────
✅ 提交完成
  · PR（merge 请用 Rebase）：https://github.com/open-vela/contest2026_427_xinpingqihe/pull/11
  · 我们的仓库首页（默认分支已同步）：https://github.com/XPQHyue/contest2026_427_xinpingqihe
  · 未 merge 前，官方仓 dev-ai-contest-2026 不含本次代码 —— 报告里要如实说明
别忘了（独立 10 分维度）：bash .claude/skills/phywear-migrate/finish_session.sh
──────────────────────────────────────────────
EOF
