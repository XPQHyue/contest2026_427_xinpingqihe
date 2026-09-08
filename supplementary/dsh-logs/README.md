# supplementary/dsh-logs/ — DeepSeek Harness 开发会话补充佐证

> ⚠️ **诚实声明**：本目录为 **DeepSeek Harness (DSH)** 开发会话的**补充佐证材料**。
> DSH **不在官方支持的 4 种 AI 日志工具之列**（官方为 `claude-code` / `opencode` / `codex` /
> `kiro`），其会话**无法导入官方 `event.schema.json`**，因此 **不计入 `logs/` 的 AI 工时统计**。
> 这里仅作为开发过程/工作量的辅助说明，供评委参考。

## 内容

- **`DSH_SESSIONS_SUMMARY.md`** — 去重后 **13 个会话**的逐场摘要（日期 / 体量 / 工具使用 /
  任务清单 / 用户关键指令 / 助手要点），汇总自各会话原始 `session.jsonl`。

## 为什么用可读摘要而非原始 jsonl

- 评审可读、可核实：摘要保留每场的**真实用户指令、工具调用、todo 拆解**，直接反映开发内容与工作量。
- 仓库不臃肿：原始会话共 ~46MB（13 场 JSONL），且含大量系统/授权切换噪音；摘要仅 52KB，信息密度更高。
- 原始 `session.jsonl` 在本机 `/home/xpqh/下载/`（`dsh-session-*.zip`）与 `/home/xpqh/.dsh/sessions/` 可回溯。

## 覆盖的开发内容（对应官方日志覆盖之外的通道）

传感器驱动（MMC5603/LTR-303/麦克风）、LVGL UI 屏管理与 EPIC blit 曲线渲染、CJK 字体管线、
i18n EN/ZH、EPIC/LVGL 性能优化（3→43 FPS）、真机摆测 g、phyphox 源码分析、硬件 bring-up 调试等。

> 本目录内容为**补充说明**，不参与 AI 官方评分；官方 AI 日志见 `/logs/XPQHyue/`。
