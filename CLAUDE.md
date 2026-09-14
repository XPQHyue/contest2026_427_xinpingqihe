# CLAUDE.md —— PhyWear 项目记忆（Claude Code / 小米 MiMo 必读）

> 本文件是本项目**唯一权威记忆**：任何 Claude Code / MiMo 会话**开始前先读它**。
> 细则在 `.claude/skills/*/SKILL.md` 里（本文件只放"必须知道"的部分）。

## 0. 角色分工（2026-09-14 队内决定，勿擅自更改）

| 角色 | 谁 | 职责 |
|---|---|---|
| **主力** | DeepSeek Harness（DSH） | 实现、驱动/BSP、构建、烧录、真机验证、文档、证据、提交 |
| **辅助** | Claude Code + 小米 MiMo（**本机**运行） | ①独立复核 DSH 的改动（审核员）②产出可入 `logs/` 的 AI Coding 日志 ③文档/脚本二次校对 |

- **不再迁移到新电脑**（原"换机器"计划作废）：MiMo 留在**本机**，以辅助身份参与。
- **禁止刷日志**：只为**真实任务**开会话。`logs/` 必须反映真实工作，不许为凑数跑无意义会话——评审会看内容，造假扣分。
- 涉及改代码 / 编译 / 烧录 / 摸板子的活归 DSH；MiMo 以**审核 + 校对 + 取证**为主，除非队长明确指定。

## 1. 当前状态（每次改动后请同步更新本节）

| 项 | 值 |
|---|---|
| 参赛仓 / 分支 | `contest2026_427_xinpingqihe` / `dev-ai-contest-2026` |
| 本地 HEAD = fork 默认分支 | `0b2dd6bee070`（DSH 侧） |
| 官方仓分支 | `9406ed0a6230`（**PR #13 未合并前不含最新**，报告里要如实说明） |
| 待合并 PR | **#13**（回退点机制 + 2026-09-14 交接日志 + 报告口径同步，4 提交，需 rebase merge） |
| 真机固件 | 板上 `eb13cf4ba9aea4ee9ba7d4fcf0f77383`（2,056,576 B）；已构建待烧 `484b64ca2e9cd157a191f8ea7d022f9a`（含 Skill 输出规范 5,165 B） |
| AI 日志 | `logs/XPQHyue/` **50 会话 / 15,283 事件**，`validate-log.py` ✅ ALL OK |
| 回退点 | `~/桌面/PhyWear-rollback-20260914-2335/`（tag `phywear-handover-20260914-2335`） |
| 迁移包 | `~/桌面/PhyWear-migrate-20260914.zip` |
| 板子 | 立创·黄山派 SF32LB52-MOD-1-N16R8；串口 `/dev/ttyUSB0` @1000000 8N1 |

## 2. 铁律（违反 = 停止并报告，不许绕过）

1. **提交只走脚本**：`bash .claude/skills/phywear-submit/submit_427.sh`（默认演练，确认后 `--execute`）。不许手敲 `git push`、不许 force-push 官方仓。细则见 `phywear-submit/SKILL.md` 的 S1–S13。
2. **串口铁律**：`pyserial` 打开后立刻 `dtr=False; rts=False`；`picocom` 必须 `--noreset --lower-rts --lower-dtr`；`/dev/ttyUSB0` **独占**；**禁止 `erase_flash`**；**一个 boot 只跑一个 `phywear` GUI**。
3. **密钥不入库**：MiMo Key 只在 `~/.config/phywear/mimo.key`（600）；`tp-`/`sk-`/`ghp_` 出现在提交里 = 直接中止。
4. **构建产物不入库**：`cmake_out/`、`nuttx.bin`、`*.o/*.a`、`.zip`；唯一允许的二进制是 `board/ftab_openvela.bin`。
5. **只许 rebase**：不许产生 merge commit；提交作者固定 `XPQHyue <15770782523@163.com>`。
6. **改完必须回仓**：工作区（`~/openvela`）改动要 `sync_back.py --execute` + 重生成 `manifest.json`，否则评审 clone 看不到。
7. **数据可追溯**：每个数字都要指到代码/日志/证据；**模拟器数据 ≠ 真机测量**；未实现不得写成已实现（写进「已知限制」不扣分）。
8. **归属如实**：EPIC 硬件加速来自官方 PR #31/#41/#121（非本队原创）；phyphox 仅灵感来源（见 `app/phywear/NOTICE.md`）；主动场景默认关闭；真机无网络栈。

## 3. 每次会话结束必做（独立 10 分维度）

```bash
bash .claude/skills/phywear-migrate/finish_session.sh
```
把本次 Claude Code 会话导出到 `logs/XPQHyue/<日期>/` 并校验。**白名单工具只有 claude-code / codex / opencode / kiro**；DSH 不计，只能作补充证据。

## 4. 常用命令

```bash
python3 .claude/skills/phywear-migrate/check_env.py          # 本机环境检测
python3 .claude/skills/phywear-reproduce/check_progress.py   # 进度/一致性 P0–P7（P2 = 快照↔工作区）
python3 .claude/skills/phywear-reproduce/sync_back.py        # 工作区 → 参赛仓 快照回写（先演练）
bash    .claude/skills/phywear-submit/submit_427.sh          # 提交（唯一入口，默认演练）
bash    .claude/skills/phywear-migrate/make_rollback.sh      # 打新回退点
bash    .claude/skills/phywear-migrate/rollback.sh --check    # 与回退点比对
```

## 5. 审核 DSH 改动时看什么

按 **审核员清单** 执行（条目见 `docs/07 §7.2` 与本文档第 2 节）：红线扫描（密钥/产物/静默忽略/merge commit/邮箱）→
一致性（P2、Skill blob、manifest）→ 真实性（数字对代码/日志）→ 功能回归（真机自检清单 `docs/07 §5`）→
文档口径 → 流程合规（`submit_427.sh`、远端 SHA、日志导出）→ 回退保障。
**证据不足时结论只能是 ⚠️ 或 ❌，不许给 ✅。**
