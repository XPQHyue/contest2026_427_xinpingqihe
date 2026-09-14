# PhyWear 交接说明（换电脑继续开发用）

> 生成时间：2026-09-14 ｜ 参赛仓：`contest2026_427_xinpingqihe`（官方仓 `open-vela/...`，分支 `dev-ai-contest-2026`）
> 配套 SKILL：`.claude/skills/phywear-migrate`（环境检测 + 迁移流程）、`.claude/skills/phywear-reproduce`（进度检测 + 代码恢复）

## 一、当前进度快照（可核对）

| 项 | 值 |
|---|---|
| 真机固件 | md5 `eb13cf4ba9aea4ee9ba7d4fcf0f77383`，**2,056,576 B**（flash 12.26%，SRAM 93.35%），已烧录并复验 |
| 硬件 | 立创·黄山派 SF32LB52-MOD-1-N16R8（16 MB NOR / 8 MB PSRAM / 390×450 AMOLED / FT6146 触控 / 麦克风 + 喇叭） |
| 应用 | `app/phywear/`（LVGL 9.1，独立 C 实现）+ 5 个 CJK 子集字体（539 码点）+ `NOTICE.md` |
| 页面 | 主菜单 8 板块 → 16 个已实现屏幕（14 实验/工具页 + 设置 + 关于）；Agent 页面目录登记 20 项 |
| AI | `packages/ai_agent` 定制：4 个 phywear 工具 + 离线意图表 + 手表端「AI 教练」页（状态/回执/4 快捷按钮）+ Skill 自动装机 |
| Skill | `app/phywear/skills/phywear-physics-coach.md`（3,779 B，含触发场景/步骤/输出规范），开机装到 `/data/agent/skills/` |
| LLM | 小米 MiMo Token Plan（`token-plan-cn.xiaomimimo.com` / `mimo-v2.5-pro`）**模拟器实测** `llm=ok` + 自主工具调用；真机无网络栈（如实） |
| 日志 | `logs/XPQHyue/` 44 会话 / 14,277 事件，`validate-log.py` ✅ ALL OK |
| 文档 | `docs/01–08` + `docs/evidence/{realboard,acoustic,a1-skill,a2-proactive,llm,ai-coach,ui}` |
| 提交状态 | PR #11 **open**（15 提交，base `dev-ai-contest-2026`）；官方仓未 merge |

## 二、大赛必做项完成度

| 必做项 | 状态 |
|---|---|
| 真机烧录 ai_agent 固件 | ✅ |
| LLM 后端 + 基础对话 | ✅ 模拟器实测（真机无网络，如实标注） |
| ≥1 交互渠道 | ✅ CLI（`vela>`）+ WebSocket + 端侧离线意图 |
| ≥1 自定义 Skill（`/data/agent/skills/`） | ✅ 且被 LLM 实际读取使用 |
| ≥1「主动+执行」场景 | ⚠️ 代码在、**默认关闭**（`PW_WATCH_PROACTIVE 0`，原因见 `docs/01 §已知限制`） |
| 应用场景说明 | ✅ `docs/04` |

## 三、最新规划（下一步，按优先级）

| 优先级 | 事项 | 说明 |
|---|---|---|
| **P0** | **PR #11 merge 进 `dev-ai-contest-2026`** | 提交要求"代码在该分支、PR 已 merge"。merge 前先 rebase |
| **P0** | **用官方《作品提交模板》写技术报告 3.1–3.7** | 模板在 `~/下载/2026 首届 openvela AI 硬件开发者大赛 - 作品提交模板.docx`；信息表不留"待填"；已有内容在 `docs/01–08` 可映射 |
| **P0** | **演示视频 ≤5 min**（程秋霞） | 脚本 `docs/08`（含真机声学 + 模拟器 LLM 段、诚实红线） |
| **P1** | UI 第二批（4–5 天） | 原始传感器 6 页"数字 + 迷你曲线"、倾角页指针盘、秒表环形进度（复用 `pw_scope`，零新增 SRAM） |
| **P1** | 提交前自查 | 见本包 `CHECKLIST.md`（代码/日志/Skill/报告/真实性/真机证据/视频 七维） |
| **P2** | 「主动+执行」场景 | 方案已定：`cron_service` 定时主动（官方唯一显式加分项）+ 端侧事件主动；需重新验证不抢 GUI |
| **P2** | 端云协作 / 记忆机制（加分项） | 时间允许再做 |

## 四、必须遵守的如实口径（别被"美化"带偏）

- 模拟器 IMU 是**合成波形**，不得当测量结果；模拟器 ≈22–23 FPS ≠ 真机 ≈41 FPS。
- EPIC 硬件加速来自**官方 PR #31/#41/#121**，非本队原创；phyphox 是灵感来源，代码为独立 C 重写。
- 主动场景**默认关闭**；真机 **没有网络栈**，LLM 只在模拟器演示；板载喇叭响度是硬件上限。

## 五、上手命令

```bash
python3 .claude/skills/phywear-migrate/check_env.py            # ① 环境
python3 .claude/skills/phywear-reproduce/restore_code.py       # ② 演练恢复
python3 .claude/skills/phywear-reproduce/check_progress.py     # ③ 进度（P0–P7）
bash    .claude/skills/phywear-migrate/finish_session.sh       # ④ 收尾导出日志（必须）
```
