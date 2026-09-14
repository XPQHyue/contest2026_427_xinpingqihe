#!/bin/bash
# 生成「换电脑迁移包」zip：一个压缩包 = 完整复刻当前进度所需的一切。
#
#   bash make_bundle.sh                 # 生成到 ~/桌面/PhyWear-migrate-<日期>.zip
#   bash make_bundle.sh --no-key        # 不打包 MiMo Key（更安全，新机器手工填）
#   OUT=/tmp/x.zip bash make_bundle.sh  # 指定输出
#
# 包内容：
#   START-HERE.md      新电脑三步上手
#   project/           参赛仓完整快照（不含 .git，含 app/src/board/tools/docs/logs/.claude/skills）
#   repo.bundle        参赛仓 git 全量 bundle（保留提交历史与分支，可离线 clone）
#   skills/            phywear-migrate + phywear-reproduce 两个 SKILL 的独立副本
#   secrets/           MiMo Key（600）+ 安全提醒（--no-key 时只放提醒）
#   CHECKLIST.md       迁移后逐项打勾 + 赛事提交自查清单
set -eu

HERE="$(cd "$(dirname "$0")" && pwd)"
REPO="${REPO:-$HOME/work/contest2026_427_xinpingqihe}"
DATE="$(date +%Y%m%d)"
OUT="${OUT:-$HOME/桌面/PhyWear-migrate-$DATE.zip}"
KEY="${MIMO_KEY_FILE:-$HOME/.config/phywear/mimo.key}"
WITH_KEY=1
[ "${1:-}" = "--no-key" ] && WITH_KEY=0

STAGE="$(mktemp -d)"
ROOT="$STAGE/PhyWear-migrate-$DATE"
mkdir -p "$ROOT"/{project,skills,secrets}

echo "== 1/6 参赛仓快照 → project/ =="
rsync -a --exclude '.git' --exclude '__pycache__' "$REPO/" "$ROOT/project/"

echo "== 2/6 git bundle（保留历史，可离线 clone）=="
git -C "$REPO" bundle create "$ROOT/repo.bundle" --all >/dev/null 2>&1 && \
  echo "   ✅ repo.bundle $(du -h "$ROOT/repo.bundle" | cut -f1)" || echo "   ⚠️ bundle 失败（不致命）"

echo "== 3/6 SKILL 独立副本 =="
cp -a "$REPO/.claude/skills/phywear-migrate" "$REPO/.claude/skills/phywear-reproduce" \
      "$REPO/.claude/skills/phywear-submit" "$ROOT/skills/"

echo "== 4/6 密钥 =="
if [ "$WITH_KEY" = "1" ] && [ -f "$KEY" ]; then
  cp "$KEY" "$ROOT/secrets/mimo.key"; chmod 600 "$ROOT/secrets/mimo.key"
  echo "   ✅ 已打包 MiMo Key（⚠️ 此 zip 不要上传到任何公开位置）"
else
  echo "   ℹ️ 未打包 Key"
fi
cat > "$ROOT/secrets/README.md" <<'EOF'
# 密钥说明（务必读完再分发这个压缩包）

- `mimo.key`：小米 MiMo **Token Plan** 密钥（`tp-` 开头），只用于本队开发。
- 放到新电脑后执行：
  ```bash
  mkdir -p ~/.config/phywear && cp mimo.key ~/.config/phywear/ && chmod 600 ~/.config/phywear/mimo.key
  ```
- **绝不要**把密钥提交进 git、写进文档、或出现在截图/录屏里。
- ⚠️ 本压缩包含明文密钥：**只在本队内部传递**，不要上传到飞书/网盘等公开位置；
  若需要把迁移包外发，请改用 `bash make_bundle.sh --no-key` 重新生成。
- 比赛要求的"作品提交压缩包"是另一份（技术报告 + 视频等材料），**不要混用**。
EOF

echo "== 5/6 上手说明与清单 =="
cat > "$ROOT/START-HERE.md" <<'EOF'
# PhyWear 迁移包 · 三步上手（新电脑）

前置：Ubuntu 22.04+、能联网、有黄山派板子（没有也能用模拟器开发）。

```bash
# 0) 解压后进入本目录
unzip PhyWear-migrate-*.zip && cd PhyWear-migrate-*

# 1) 参赛仓与代码
git clone repo.bundle project          # 离线还原完整仓库（含分支/历史）
#    或直接从 project/ 用文件（无 git 历史）
#    或联网：git clone https://github.com/open-vela/contest2026_427_xinpingqihe.git
export PHYWEAR_REPO="$PWD/project"

# 2) 密钥 + 环境检测
mkdir -p ~/.config/phywear && cp secrets/mimo.key ~/.config/phywear/ && chmod 600 ~/.config/phywear/mimo.key
python3 project/.claude/skills/phywear-migrate/check_env.py     # 缺什么按提示装

# 3) openvela 工作区 + 代码落位 + 构建
repo init -u https://gitee.com/open-vela/manifests.git -b dev-ai-contest-2026 && repo sync -c -j8
export PHYWEAR_WS="$HOME/openvela"
python3 project/.claude/skills/phywear-reproduce/restore_code.py --execute
python3 project/.claude/skills/phywear-reproduce/check_progress.py        # P0–P7 应全绿
cp project/board/{ftab_openvela.bin,flash_with_ftab.sh} ~/openvela/       # 烧录所需
```

提交只能走这一个入口（含红线预检 + 默认分支同步 + 远端校验）：
`bash project/.claude/skills/phywear-submit/submit_427.sh --execute -m "feat: …"`

然后在 `~/openvela` 里启动 Claude Code 继续开发（SKILL 见 `project/.claude/skills/`），
每个工作时段结束运行：`bash project/.claude/skills/phywear-migrate/finish_session.sh`。

**读这两个文件了解现状与规划**：`project/.claude/skills/phywear-migrate/HANDOFF.md`、`project/docs/07_构建烧录与复现指南.md`。
EOF

cat > "$ROOT/CHECKLIST.md" <<'EOF'
# 迁移后逐项打勾

## A. 环境与代码
- [ ] `check_env.py` 无 ❌（工具链/工作区/参赛仓/ftab/密钥）
- [ ] `restore_code.py --all` 演练输出"全部一致"
- [ ] `check_progress.py` P0–P7 全绿（93 个关键文件 md5 一致）
- [ ] `board/ftab_openvela.bin`、`board/flash_with_ftab.sh` 已拷到 `~/openvela/`

## B. 构建与真机
- [ ] 真机固件构建成功（flash ≈12.26%，SRAM ≈93.4%）
- [ ] 烧录后启动链 `SFBL → ABCD → NuttShell (NSH)`
- [ ] 分块读回前 16 KB 逐字节一致（sftool 报"不一致"是已知抖动，重试即可）
- [ ] `phywear lang zh` 中文界面、主菜单 8 图标、进出带曲线页面 ≥8 次曲线正常
- [ ] `ai_agent &` → `Agent loop started`；`ls /data/agent/skills` 有 `phywear-physics-coach.md`
- [ ] 「AI 教练」页点「读加速度」返回真实读数（不是 `accelerometer not available`）

## C. 会话日志（独立 10 分维度）
- [ ] `finish_session.sh` 跑过，`logs/` 有新会话
- [ ] `validate-log.py` 输出 ✅ ALL OK
- [ ] 日志数量与报告声明一致（当前 44 会话 / 14,277 事件）

## D. 赛事提交（截止 2026-09-20）
- [ ] 代码在 `dev-ai-contest-2026` 分支，PR 已 merge（当前 PR #11 open → 需 merge）
- [ ] 技术报告用**官方模板**、3.1–3.7 逐节写全、信息表无"待填"
- [ ] 至少 1 个自定义 Skill（触发词 + 步骤 + 输出规范）
- [ ] 性能数据有代码/日志佐证；未完成项写进 README「已知限制」
- [ ] 真机证据（照片/录屏/串口日志任一）已在 `docs/evidence/`
- [ ] 演示视频 ≤5 min、能正常播放
- [ ] 压缩包命名 `<队伍名称>-<作品名称>-<仓库名称>.zip`，上传飞书「作品提交」表单
EOF

echo "== 6/6 打包 =="
mkdir -p "$(dirname "$OUT")"
( cd "$STAGE" && zip -qr "$OUT" "PhyWear-migrate-$DATE" )
rm -rf "$STAGE"
echo
echo "✅ 迁移包：$OUT  ($(du -h "$OUT" | cut -f1))"
[ "$WITH_KEY" = "1" ] && echo "⚠️ 含明文 MiMo Key：只在本队内部传递，不要上传公开位置"
