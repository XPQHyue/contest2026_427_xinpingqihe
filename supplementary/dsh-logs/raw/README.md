# supplementary/dsh-logs/raw/ — DeepSeek Harness 原始事件证据（子集）

本目录保存 **DeepSeek Harness (DSH)** 开发会话的**原始事件子集**，作为开发工作量的坚实证据。

## 这是什么

每个 `<session-id>.jsonl` 是从对应 DSH 会话的 `session.jsonl`（原始全量）**忠实过滤**后的子集，
**保留了原始事件的每一行 JSON**，只剔除了体量占比 ~95% 的噪音回显
（`assistant/chunk`、`reasoning-chunks`、`agent/inbox/spliced` 等），以控制仓库体积。

**保留的事件类型**（均为原始事件，未改写）：
- `session` — 会话元信息（id / createdAt / cwd）
- `user/message` — 用户（人类）指令
- `assistant/message` — 助手回复
- `tool/call` — AI 调用的工具（bash / edit / read / write / job_output ...）
- `todo/write` — 任务清单拆解
- `goal/change` — 目标变更

每行仍是原始 JSON（含原 `seq`/`time`/会话 id），**可逐行核实、可对原始全量溯源**。

## 去重说明

原始会话含重复导出（`(1)` 后缀为同一会话的更新导出）。本目录仅保留**去重后 13 场**，每场一个文件：
`f493d545`、`e3c34439`（取超集）、`276eacb8`、`ed7cf360`、`d0ceba7a`、`14c7863f`、
`3936153c`、`5b72c8cd`、`d570afe2`、`edab04fa`、`51b507be`、`8d5f648e`、`314c2c99`。

## 如何溯源到原始全量

原始全量 `session.jsonl` 位于本机（未进仓，因体量 ~114MB）：
- `/home/xpqh/下载/dsh-session-*.zip`（每场一个 zip，内含 `session.jsonl`）
- `/home/xpqh/.dsh/sessions/`（压缩态 `.zstd`）

需要时可用我方脚本重建：`filter 保留 user/message|assistant/message|tool/call|todo/write|goal/change|session`。

## 人类可读概览

逐场的中文摘要（日期 / 体量 / 工具使用 / 任务清单 / 用户指令 / 助手要点）见
[`../DSH_SESSIONS_SUMMARY.md`](../DSH_SESSIONS_SUMMARY.md)。两者配套：**摘要概览 → 原始子集逐行核实**。

> ⚠️ DSH 非官方支持日志工具（官方为 claude-code/opencode/codex/kiro），
> **不计入官方 `logs/` 的 AI 工时统计**；本目录及摘要仅作开发过程/工作量佐证。
