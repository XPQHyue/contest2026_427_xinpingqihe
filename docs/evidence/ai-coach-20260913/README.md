# AI 教练页（手表 UI 上"看得见 AI"）—— 2026-09-13

## 做了什么

在此之前，AI 能力只存在于**串口控制台**（`vela>` CLI + 40 个工具 + Skill），手表界面上只有一个
**灰字"规划中"的占位入口**（点了没反应）。本轮把它做成了活的页面：

| 项 | 说明 |
|---|---|
| 入口 | 主菜单「AI 教练」（`phywear_ui.c` 的 `ui_ai_open`，`live` 由 false 改 true） |
| 页面内容 | 端侧 Agent 状态（已就绪 / 未连接）+ 最近 4 条消息（最新一条大字）+ 4 个快捷请求按钮 + 一行如实说明 |
| 4 个按钮 | 有哪些实验 / 打开单摆 / 读加速度 / 跑单摆实验 |
| 交互链路 | 按按钮 → `pw_ai_ask()` 向端侧 Agent 发自然语言请求 → Agent 走**离线意图表**（无网络也可用）或 LLM → 调工具（切页/读数/跑实验）→ 回执写回 `pw_ai` 消息日志 → 页面每 700 ms 刷新显示 |
| 线程安全 | Agent 回复回调运行在其它任务，只写加锁的环形日志；所有 LVGL 操作都在 GUI 线程的定时器里 |
| 回执可读化 | Agent 的 JSON 回执被压成一行：`{"accepted":true,"screen":"pendulum",…}` → `OK - pendulum`；实验/页面列表 → `list: N screens` |

## 实测证据（模拟器，2026-09-13）

```
nsh> phywear cap ai &              # 打开 AI 教练页（后台）
nsh> ai_agent &                    # 起端侧 Agent
nsh> phywear coach 有哪些实验       # 与"按按钮"完全相同的代码路径（无头测试入口）
      → AI: list: 20 screens
nsh> phywear coach 打开单摆
      → AI: OK - pendulum          # 且手表界面真的从 AI 页切到了单摆页
```

| 文件 | 内容 |
|---|---|
| `ai-coach-page.png` | AI 教练页：状态「已就绪」+ 最近回复 `AI: list: 20 screens` + 4 个按钮 + 如实说明 |
| `ai-request-switched-pendulum.png` | 同一串口会话里，`phywear coach 打开单摆` 之后**手表界面已切到单摆页**（`g = 19.80 m/s²`，模拟器合成数据） |

## 如实说明

- 截图为**模拟器**（goldfish，390×450）画面；真机为同款 390×450 CO5300 AMOLED，页面布局一致。
- 模拟器 IMU 为**合成波形**，图中 `g`、`T` 数值**不得当作测量结果**。
- **真机没有网络栈**：真机上这条链路走端侧离线意图 + 工具（一样可用）；**LLM 对话只在模拟器演示**。
- Agent 未启动时页面显示「未连接」并提示先起 `ai_agent`，不会假报就绪。
