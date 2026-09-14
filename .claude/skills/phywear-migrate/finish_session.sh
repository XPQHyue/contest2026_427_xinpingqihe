#!/bin/bash
# PhyWear 会话收尾：把本次 Claude Code 会话导出到参赛仓 logs/ 并校验。
#
#   bash finish_session.sh                 # 导出今天 + 校验 + 提交（不推送）
#   bash finish_session.sh --push          # 额外推送到 fork
#   PUSH=1 bash finish_session.sh          # 同 --push
#   REPO=/path/to/repo bash finish_session.sh
#
# 为什么必须跑：AI Coding 日志是**独立 10 分维度**；Claude 的会话默认只落在本机
# ~/.claude/contest-collector-staging，不会自动进仓库，必须主动导出。
# 采集器只在含 .repo/ 的 openvela 工作区内生效 —— 本脚本由 ~/openvela 里的
# Claude 会话调用即可；若在参赛仓目录跑，导出的是本机已采集的历史会话。
set -u

REPO="${REPO:-$HOME/work/contest2026_427_xinpingqihe}"
WS="${PHYWEAR_WS:-$HOME/openvela}"
LOGIN="${GITHUB_LOGIN:-XPQHyue}"
TEAM="${TEAM_ID:-contest2026_427_xinpingqihe}"
COLLECTOR="$WS/.claude/skills/contest-log-collector"
PUSH="${PUSH:-0}"
[ "${1:-}" = "--push" ] && PUSH=1

echo "== 参赛仓：$REPO"
[ -d "$REPO/.git" ] || { echo "❌ 参赛仓不存在：$REPO"; exit 1; }

echo
echo "== 1/4 采集器自检 =="
if [ -f "$HOME/.claude/contest-collector.env" ] && [ -d "$COLLECTOR" ]; then
  echo "✅ 采集器已安装（$HOME/.claude/contest-collector.env）"
else
  echo "⚠️ 采集器未安装，尝试安装…"
  bash "$COLLECTOR/onboarding/install.sh" --team-id "$TEAM" --github-login "$LOGIN" || {
    echo "❌ 安装失败：先确认 $COLLECTOR 存在（repo sync 带来）"; exit 1; }
fi

echo
echo "== 2/4 导出今天的会话到 logs/ =="
python3 "$COLLECTOR/tools/export-session.py" --today \
        --dest "$REPO" --github-login "$LOGIN" --confirm || true

echo
echo "== 3/4 校验日志格式 =="
python3 "$COLLECTOR/tools/validate-log.py" "$REPO/logs" | tail -5

echo
echo "== 4/4 提交 logs/ =="
cd "$REPO" || exit 1
git add logs
if git diff --cached --quiet; then
  echo "（logs/ 无变化，跳过提交）"
else
  git -c user.name=XPQHyue -c user.email=15770782523@163.com \
      commit -q -m "logs: 导出 $(date +%F) Claude Code 会话（$(find logs -name '*.jsonl' | wc -l) 个会话）"
  echo "✅ 已提交。推送：git push fork dev-ai-contest-2026"
  [ "$PUSH" = "1" ] && git push fork HEAD
fi

echo
echo "提示：白名单工具只有 claude-code / codex / opencode / kiro；DeepSeek Harness 不计入。"
