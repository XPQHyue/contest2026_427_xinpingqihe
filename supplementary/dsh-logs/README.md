# supplementary/dsh-logs/ — DeepSeek Harness 开发会话补充佐证

> ⚠️ **诚实声明**：本目录为 **DeepSeek Harness (DSH)** 开发会话的**补充佐证材料**。
> DSH **不在官方支持的 4 种 AI 日志工具之列**（官方为 `claude-code` / `opencode` / `codex` /
> `kiro`），其会话**无法导入官方 `event.schema.json`**，因此 **不计入 `logs/` 的 AI 工时统计**。
> 这里仅作为开发过程/工作量的辅助说明，供评委参考。

## 内容

- **`raw/`** — 去重后 **13 场** DSH 会话的**原始事件子集**（`<session-id>.jsonl`，每行原始 JSON，
  含 user/message、assistant/message、tool/call、todo/write、goal/change、session；剔除了 ~95%
  chunk/reasoning 回显以控体积，但每行仍是原始事件，可逐行核实、可对原始全量溯源）。见 `raw/README.md`。
- **`DSH_SESSIONS_SUMMARY.md`** — 去重后 **13 个会话**的逐场摘要（日期 / 体量 / 工具使用 /
  任务清单 / 用户关键指令 / 助手要点），供快速概览。

## 为什么这样组织

- **坚实证据**：`raw/*.jsonl` 是原始事件的忠实子集，评委可逐行核实（摘要只是概览）。
- **可读与可核查兼顾**：摘要看流程，原始子集看实据。
- **仓库可控**：全量 13 场 `session.jsonl` 共 ~114MB（含大量 chunk/reasoning 噪音），子集后 ~40MB，
  兼顾确证力与仓库体积。

## 覆盖的开发内容（对应官方日志覆盖之外的通道）

传感器驱动（MMC5603/LTR-303/麦克风）、LVGL UI 屏管理与 EPIC blit 曲线渲染、CJK 字体管线、
i18n EN/ZH、EPIC/LVGL 性能优化（3→43 FPS）、真机摆测 g、phyphox 源码分析、硬件 bring-up 调试等。

> 本目录内容为**补充说明**，不参与 AI 官方评分；官方 AI 日志见 `/logs/XPQHyue/`。
