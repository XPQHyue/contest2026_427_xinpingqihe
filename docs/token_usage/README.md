# Token 用量证据（AI-Native 开发）

本目录存放 AI 开发 Token 用量的证据文件（对应技术报告 3.6 AI-Native 开发说明）。

## 1. 小米 MiMo Token Plan 用量（Claude Code 所用模型）

- **文件**：`token_plan_usage_data_202601_202612_2625305870.xlsx`
- **来源**：小米 MiMo Token Plan 控制台导出（队伍账号，表名 "Token plan usage detail"）
- **导出区间**：2026-01 ~ 2026-12（实际含 2 条月度记录）
- **文件内容**（逐行解析）：

| Date | Model | Total Tokens | Input Hit Tokens | Input Miss Tokens | Output Tokens | Request Count |
|---|---|---|---|---|---|---|
| 2026-08 | mimo-v2.5-pro | 423,476,595 | 414,620,416 | 7,454,879 | 1,401,300 | 5,061 |
| 2026-09 | mimo-v2.5-pro | 27,881,909 | 27,424,128 | 381,720 | 76,061 | 337 |
| **合计** | | **451,358,504** | 442,044,544 | 7,836,599 | 1,477,361 | **5,398** |

> 本队 AI 开发（Claude Code，模型 `mimo-v2.5-pro`）实测消耗 **451,358,504 tokens / 5,398 次请求**，
> 以上数字**完全由本文件支撑**，无估算。

## 2. DeepSeek Harness（DSH）用量

- **用量**：约 **1,874,519,405 tokens**（本队记录）。
- **说明**：DSH **不在赛事官方支持的 4 种 AI 工具之列**（官方为 claude-code / opencode / codex / kiro），
  因此**不计入 `logs/` 的 AI 工时统计**；但其会话日志已作为补充佐证提交：
  `supplementary/dsh-logs/`（13 场会话的原始事件子集 + 可读摘要）。

## 3. 合计

| 工具 | Tokens | 是否计入官方 logs/ | 证据 |
|---|---|---|---|
| 小米 MiMo（Claude Code 所用） | **451,358,504** | ✅ 是（Claude Code 为官方支持工具） | 本目录 xlsx |
| DeepSeek Harness | **1,874,519,405** | ❌ 否（非官方支持工具） | `supplementary/dsh-logs/` |
| **合计** | **约 2,325,877,909** | — | — |
