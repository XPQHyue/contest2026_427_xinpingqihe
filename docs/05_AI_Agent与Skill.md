# 05 AI Agent 与 Skill

> 本项目把 openvela 官方 `ai_agent`（`packages/ai_agent`）接到 PhyWear 手表应用上：Agent 能**驱动手表界面**、**读传感器**、**按自定义 Skill 的流程做物理实验并解释结果**。
> 本文说明：用了哪些 ai_agent 能力、4 个自定义工具、Skill 的落盘与格式约束、离线降级、以及"主动+执行"场景的现状与后续方向。

---

## 1. 用了 ai_agent 的哪些能力

| 能力（官方模块） | 本项目用法 |
|---|---|
| 工具注册（`tool_registry`） | 新增并注册 4 个 PhyWear 工具（`src/tools/tool_phywear.c`），Agent 通过 JSON 调用 |
| Skill 加载（`skill_loader`） | 自定义 Markdown Skill 落在 `AGENT_SKILLS_DIR = /data/agent/skills/`；Agent 把技能摘要写进系统提示词，模型据此决定"先列页面再开页面" |
| 消息总线 + 本地客户端（`message_bus` / `velaclaw_client`） | PhyWear GUI 线程 ⇄ Agent 任务的异步通道（事件上报、回执），避免在 Agent 线程直接碰 LVGL |
| 离线意图快路径（`agent_loop.c` 意图表） | 没有 LLM / 断网时按关键词直接执行工具（切页、读数、跑实验），保证真机可用 |
| 会话与记忆（`session_mgr` / `memory_store`） | 会话历史、`SOUL.md` / `USER.md` / `MEMORY.md`（后续用于沉淀实验记录） |
| 调度（`cron_service` / `heartbeat`） | Agent 启动即拉起（30 min 心跳，读 `HEARTBEAT.md`），为后续"定时巡检"预留 |
| 命令行通道（`nsh_commands`） | `vela>` CLI：`help` / `config_show` / `set_llm` / `router_set` / `ask` / `heartbeat_trigger` / `install_skill` 等 |

---

## 2. 4 个 PhyWear 工具

| 工具 | 用途 | 关键参数 |
|---|---|---|
| `phywear_list_experiments` | 列出可用页面与说明（16 主页面 + 声学/原始传感器子页） | 无 |
| `phywear_open_screen` | 把手表界面切到指定页 | `screen`（页名，来自上一个工具） |
| `phywear_read_sensor` | 读一次传感器瞬时值 | `sensor` = `accel` / `mag` / `light` |
| `phywear_run_experiment` | 打开实验页并采样若干秒，返回 min/max/mean | `screen`、`seconds`（1–30） |

设计约束（重要）：

- **LVGL 非线程安全**：工具**不直接**调用 LVGL，而是把"打开某页"写进邮箱（`pw_ai_request_open`），由 GUI 主循环下一轮执行；传感器读取是普通字符设备 `read()`，理论上可跨线程，但见 §5 的已知限制。
- **优雅降级**：PhyWear 没跑或页名非法时返回可读 JSON 错误，不影响 Agent 回合。

---

## 3. 自定义 Skill

**正本**：`app/phywear/skills/phywear-physics-coach.md`（《PhyWear 腕上物理实验教练》，3779 B）

内容：何时使用 / 4 个工具及其参数 / 标准流程 / **16 个页面名对照** / 结果解释要用的物理关系（`g = 4π²L/T²`、静止时合模长≈1 g、lux 量级、频谱主频范围）/ 边界条件（页名只能来自工具、采样 ≤30 s、模拟器无真实传感器、真机端侧 AI 状态）。

### 3.1 格式约束（踩过的坑）

| 规则 | 说明 |
|---|---|
| 第 1 行 | `# 标题`，≤64 字符，作为摘要里的粗体名 |
| **第 2 行必须紧跟描述** | `extract_description()` 读到第一个空行或 `##` 就停；第 2 行留空 ⇒ 描述为空 |
| 摘要行 | `- **标题**: 描述 (read with: read_file <path>)` —— 模型靠摘要决定要不要 `read_file` 打开 |
| 文件名 | `*.md`、非隐藏文件，目录固定 `/data/agent/skills/` |

### 3.2 两条安装路径

- **路径 A（本项目采用）**：PhyWear 开机自动安装。真机 `/data` 是 tmpfs，重启即空，因此由 `pw_skill.c` 在启动时把 Skill 写进 `/data/agent/skills/`；内容一致时不重写（不触发 Agent 热加载）。Skill 正本经 `tools/phywear/gen_skill_blob.py` 生成 C 字符串随固件编译，`--check` 可做漂移自检。
- **路径 B（官方 CLI，对照）**：`install_skill <name> <https-url|->`（支持从控制台粘贴正文）；真机无网络栈，故仅在有网环境（模拟器）可用。

### 3.3 实测（2026-09-13）

| 项 | 值 |
|---|---|
| 阶段 A：只有 Agent（10 个内置 Skill） | 技能摘要 **784 B** |
| 阶段 B：先起 PhyWear 再起 Agent（11 个） | 技能摘要 **1135 B**（**+351 B**，正好一条摘要） |
| 真机日志 | `[phywear] installed skill /data/agent/skills/phywear-physics-coach.md (3779 bytes)` |
| 设备副本 vs 仓内正本 | md5 均 `7097e9fe8d0a496fca2a81dcbaa45e66`，逐字节一致 |

证据：`docs/evidence/a1-skill-20260913/`（`RESULT.md`、`console-A.log`、`console-B.log`、`pulled-phywear-physics-coach.md`）；复现命令 `python3 tools/phywear/a1_skill_evidence.py`。

---

## 4. 离线降级（真机没有网络时的行为）

真机镜像**没有网络栈**，因此：

- Agent loop 现在**开机即启动**（官方原逻辑是"网络连通后才启动"，真机永远等不到 → Agent 起来了但"听不见"消息）；
- 未命中 LLM 时，`agent_loop.c` 的离线意图表按关键词直接执行工具：
  `PhyWear 巡检` / `检测到持续摆动` → `phywear_run_experiment(pendulum, 10s)`；`打开单摆` → `phywear_open_screen(pendulum)`；`读加速度`、`跑单摆实验`、`回到主屏` 同理；
- LLM 讲解能力在 **goldfish 模拟器**（有 virtio-net + LLM Key）演示，见下面第 4.1 节。

### 4.1 LLM 后端实测（2026-09-13，模拟器）

| 项 | 值 |
|---|---|
| 提供方 / 端点 | 小米 MiMo **Token Plan**（`tp-` 开头密钥）→ `https://token-plan-cn.xiaomimimo.com/v1/chat/completions` |
| 模型 | `mimo-v2.5-pro` |
| 配置方式 | 设备内 `set_llm token-plan-cn.xiaomimimo.com mimo-v2.5-pro <key>`（写入 `config_store` → `/data/agent/config/config.json`，`config_show` 里密钥显示为 `tp-c****`） |
| 实测结果 | 一轮提问：`trace … llm=ok backend=0`，LLM **自主发起 4 次工具调用**（`phywear_list_experiments` → `phywear_open_screen{pendulum}` → `launch_quickapp` → `phywear_run_experiment{pendulum,5s}`），5 轮迭代、端到端 20 s，最后用中文作答 |
| 证据 | `docs/evidence/llm-20260913/sim-llm-conversation.log`（已脱敏）、`bug-request-invalid-utf8.txt` |

> ⚠️ **注意：MiMo 的 `tp-` 密钥属于 Token Plan，与按量付费的 `sk-` 密钥不通用**，
> 且必须配 `token-plan-<集群>.xiaomimimo.com` 端点；用 `api.xiaomimimo.com` + `Bearer` 会直接 401。
> `packages/ai_agent` 自带的 `mimo` 预设指向按量付费端点，Token Plan 需要按上面的自定义 host 方式配置。

### 4.2 顺带修掉的一个真实 Bug：中文内容被按字节截断 → 云端 400

现象：LLM 调用稳定返回 `400 {"message":"Invalid JSON in request body"}`，而同样内容用 curl 从电脑发是 200。

定位：本地起代理抓设备真实请求体，发现**请求体不是合法 UTF-8**——第 3736 字节是 `0xe8`（三字节汉字的
首字节），后面直接跟了 JSON 的 `\n`。根因是 `skill_loader.c` 组装技能摘要/标题/描述时用
`snprintf`/`memcpy` **按固定字节数截断**，把中文字符切成了半截；该摘要进入 system prompt 后被
cJSON 原样写入请求体。（`nginx` 的 `client_body` 之类网关解析失败，于是回 "Invalid JSON"。）

修复：新增 `utf8_safe_len()`，在三处截断点回退到最后一个完整 UTF-8 字符（`src/packages/ai_agent/src/tools/skill_loader.c`）。
修复后 LLM 路径一次通过。**任何写中文 Skill 的队伍都可能踩到这个坑**，已在我们仓内修复并记录。

> **顺带发现的限制（如实）**：技能摘要缓冲只有 `skills_buf[1024]`（`context_builder.c`）。技能一多，
> 摘要尾部会被截掉 —— 实测放进 2 个中文 Skill 时摘要就顶到上限，LLM 只能看到前一部分 Skill，
> 甚至可能猜错文件名（实测它去读不存在的 `/data/agent/skills/phywear.md`，回了 `(no files found)`）。
> 这不影响功能（工具仍可调用、离线意图不受影响），但**属于已知限制**；后续可把缓冲调大或改成
> 「只列名字 + 一行摘要」的紧凑格式。

---

## 5. "主动+执行"场景：现状与后续方向

### 5.1 原设计（已实现并验证）

PhyWear 在 GUI 循环里以 10 Hz 读 IMU 合模长，滑动窗口 4 s：

| 项 | 取值 |
|---|---|
| 判定 | 窗口峰峰值 ≥ **250 mg** 且持续 ≥ **2.5 s** |
| 冷却 | 触发后 **60 s** 内不再触发 |
| 动作（原） | 把事件文本推给 Agent → Agent 调 `phywear_run_experiment` 打开单摆页采样 10 s 并回报 |

### 5.2 为什么**默认关闭**（2026-09-13 真机实测）

- 现象：启动 `phywear` 后十几秒整机卡死，日志停在摆动巡检那一行。
- 根因：事件推给 Agent 后，Agent 侧 `phywear_run_experiment` 在 **agent 任务**里长时间读传感器**并**请求 GUI 切到单摆页 —— 两条线程抢同一颗 IMU、同时切页；
  且 NuttX 的 fd 按任务隔离，Agent 任务用不了 PhyWear 打开的传感器 fd，采样恒为 `samples:0`。
- 处置：新增开关 `PW_WATCH_PROACTIVE`，**默认 0** —— 摆动检测保留（只打日志 `proactive push disabled: event only logged`），不再联系 Agent、不再自动切页。
- 验证：150 s 泡机，心跳连续（`[phywear] alive t=…s fps=…`），期间摆动事件触发 2 次均只记日志、界面不卡。

### 5.3 后续方向（与用户确认过，暂搁置）

原则：**主动能力要有，但不与传感器/界面线程抢资源**。

1. **无障碍播报**：界面元素/测量结果主动语音播报（视障或不便操作屏幕的用户）；
2. **首次开机引导**：第一次启动自动进入中文语音介绍，可关闭、可"不再提示"——设备自己决定做什么并执行；
3. **结果语音播报**：实验结束后主动念出结论（配合官方 voice 通道）。

以上都不需要 Agent 长时间占用传感器（最多单次读数），实现风险低。

---

## 6. 工具与脚本清单

| 脚本 | 用途 |
|---|---|
| `tools/phywear/a1_skill_evidence.py` | Skill 安装/摘要证据自动采集（两次开机对比摘要字节数） |
| `tools/phywear/a2_proactive_evidence.py` | 主动场景三段证据采集（检测→工具调用→回执） |
| `tools/phywear/sim_console.py` | goldfish 模拟器串口控制台（pty + FIFO），可脚本化演示 |
| `tools/phywear/gen_skill_blob.py` | 由 `.md` 生成 C 字符串（`--check` 漂移自检） |
| `tools/phywear/boardsh.py` | 真机 NSH 命令发送/回显（守 RTS/DTR 铁律） |
| `tools/phywear/pwshot.py` | 真机截图驱动（校验 FNV-1a 与逐行 crc，输出 PNG/rgb565） |
