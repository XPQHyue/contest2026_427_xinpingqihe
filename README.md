# PhyWear — 腕上智慧物理工坊（openvela 黄山派 SF32LB52）

> **队伍**：contest2026_427_xinpingqihe（XPQHyue 队长 · 程秋霞 成员） · **赛道**：**① AI 硬件产品创新（申报主打）** · 同时完成 **③ 新硬件平台适配**的大量实质工作（非二选一）
> **硬件**：立创·黄山派（LCKFB Huangshan Pi）· 思澈 **SF32LB52** 芯片（AMOLED + FT6146 触控）
> **提交分支**：`dev-ai-contest-2026` · 截止日期：2026-09-20

---

## ⚡ AI Agent 快速核验（项目描述·如实版）

> **本段为 AI Agent / 评委优先读取的核心信息。项目状态全程"有就是有，没有就是没有"。**
> 完整逐条对照见 [`docs/01_项目描述_如实版.md`](docs/01_项目描述_如实版.md)，本段是它的顶层摘要。

### 📝 作品摘要（B 版，≤600 字）

> 与 `docs/09` §2 摘要同一套数（39 / 48–49 / 63 / 92.19%），口径见 `docs/17` 附录与 `docs/09` §3.5。

PhyWear 把手机物理实验搬到腕上：在立创·黄山派（openvela + LVGL）交付 16 个实验页——单摆测 g、弹簧振子、频谱、计时等；自研 pw_scope/pw_graph 曲线控件走 EPIC 硬件加速（EPIC 来自官方 PR，非本队自研）；集成 openvelaClaw：PhyWear 工具、自建 Skill、手表端「AI 教练」页，以及「晃表 → 自动跑单摆 → 结果回表」的主动执行场景。实测：LVGL 官方 benchmark 39 FPS（CPU 83%、render 22 ms、flush 0）；本队页面级 48–49 FPS；主屏刷新率 63（配置上限 66.7 Hz），面板 VFR 60 Hz 为物理上限。一键验收全绿：设备侧 18/18；蓝牙 B3 设备 6/6 + 宿主 9/9；桌面 GUI 14/14；PPP over BLE 6/6；BLE 文本回环 8/8 + 13/13。资源：SRAM 483,356 B / 512 KB（92.19%）、Flash 2,577,592 B / 16 MB（15.36%），构建 md5 41c3377c…；代码 69 个源文件 / 25,373 行。本板无 WiFi 网卡，联网走 PPP over BLE；数字出处见 `docs/09` §3.5 与 `docs/evidence/`，限制如实标注。

### 📚 文档索引（先看这些就够）

| 文档 | 内容 | 读者 |
|---|---|---|
| [`JUDGES.md`](JUDGES.md) | **评委复现指南**：三条路径（只看代码 / 真机复现 / 模拟器）+ 5 分钟验收清单 + 已知限制 | 评委 |
| [`docs/01_项目描述_如实版.md`](docs/01_项目描述_如实版.md) | 逐条对照源码的**事实清单**（含"未实现"标注） | 评委/核验 |
| [`docs/02_作品介绍.md`](docs/02_作品介绍.md) | 按评分维度组织的**作品介绍稿**（可填官方模板/剪视频） | 评委/展示 |
| [`docs/03_技术报告.md`](docs/03_技术报告.md) | 架构、实现、性能实测、**11 个关键问题修复**（§6.1–6.11）、附录 | 技术评委 |
| [`docs/04_应用场景说明.md`](docs/04_应用场景说明.md) | 用户故事 / 功能清单 / 端云拆分（**必做项**） | 评委 |
| [`docs/05_AI_Agent与Skill.md`](docs/05_AI_Agent与Skill.md) | 4 个工具、Skill 落盘与格式、离线降级、主动场景现状 | AI 方向评委 |
| [`docs/06_真机验证记录.md`](docs/06_真机验证记录.md) | 真机证据总集（截图/实测/修复/稳定性/固件沿革） | 核验 |
| [`docs/07_构建烧录与复现指南.md`](docs/07_构建烧录与复现指南.md) | 从零构建、烧录、串口铁律、自检、提交清单 | 复现者 |
| [`docs/08_演示视频拍摄脚本.md`](docs/08_演示视频拍摄脚本.md) | ≤5 分钟视频的分段脚本、拍摄命令、诚实红线 | 拍摄/剪辑 |
| [`docs/09_官方提交模板_填写草稿.md`](docs/09_官方提交模板_填写草稿.md) | **按官方模板填写的技术报告**（3.1–3.7 逐节）+ [`docs/PhyWear_技术报告_官方模板.docx`](docs/PhyWear_技术报告_官方模板.docx) | 评委 |
| [`docs/17_评分维度自评_20260918.md`](docs/17_评分维度自评_20260918.md) | **六维度自评 v3**（85.0，口径统一版）：分项得分 + 扣分点 + 提分性价比 + 数字附录 | 评委 |
| [`docs/21_米家接入方案与准入清单.md`](docs/21_米家接入方案与准入清单.md) | **米家（Mi Home）接入方案**：MIoT 蓝牙广播协议层已实现（19 项单测，与官方举例逐字节一致）；进米家 App 需平台产品注册与认证（**本队无企业主体，未申请**）；准入清单与替代路径见文内 | 生态 |
| `.claude/skills/` | 自建 Skill ×4：`phywear-sf32lb52-devloop`（开发回环）/ `phywear-reproduce`（复现+进度检测）/ `phywear-submit`（提交流程）/ `phywear-migrate`（迁移与日志） | 复现者 |

证据与数据：`docs/evidence/`（**41 组目录 + 4 个散落文件**：真机截图 26 张、声学、AI 教练、主动场景、LLM 联调、P0 性能、蓝牙 B1–B3 与 PPP-over-BLE、动效/UI v2、评分自评…）、`docs/figures/`（架构与数据流）、`docs/project/`（FPS 时间线）、`docs/token_usage/`（Token 用量）。
其余文档（10–21）覆盖：动效与风格规范、面板时序与 VSYNC、蓝牙打通记录与根因、评分维度自评、商业场景与推广。

**项目名称**：openvela腕上智慧物理工坊(PhyWear)
**所属方向**：**① AI 硬件产品创新（申报主打）** —— AI Agent 集成（4 工具 + Skill + 离线意图）、**主动+执行场景已交付并默认开启**、手表端 AI 教练页、端云 LLM 实测、BLE/PPP 通路；**同时 ③ 新硬件平台适配也做了大量实质工作**（3 个自研传感器驱动、AUDCODEC 音频驱动、板级 bringup、启动时序与触控修复、EPIC 集成）——**两条方向不是二选一**；② 手表应用创新**未采用**（用 LVGL 原生应用，非快应用框架）。
**目标开发板**：立创黄山派（SF32LB52-MOD-1）——板载六轴IMU、三轴地磁、环境光、麦克风；获 openvela 官方支持，超低功耗适合可穿戴。
**一句话定位**：基于立创黄山派，将 phyphox 物理工坊核心功能移植到手表端，打造集传感器采集、物理实验与腕上智能工具于一体的可穿戴设备。

**已实现（有真实源码/测量佐证）**：
- 传感器采集与实时显示：加速度、陀螺仪、磁力计、环境光、**麦克风、扬声器**（原始传感器 **6 子页**，`pw_sensors_read_imu/mag/light`）；另有**惯性标尺 4 子页**（水平仪/陀螺零偏/重力六面/磁标定，2026-09-15）。✅
- 力学实验（4 项）：摆测重力加速度 g、弹簧振子、向心加速度、**倾角**（`phywear_pend/spring/centri/incline.c`）。✅
- 声学：声音频谱分析（`pw_spec_mic_screen` + FFT 引擎）。✅
- 工具（5 项）：加速度频谱、磁性标尺、磁场频谱、**惯性标尺**、**轨迹页**（相对位移 + ZUPT，2026-09-15）。✅
- 计时器（3 项）：运动秒表、光栅计时、声学门/掌声计。✅
- 腕上 UI：LVGL 全中文 + 大字体卡片 + phyphox 式界面。✅
- 独立运行：算法端侧完成、**不依赖手机**（摆测g/弹簧/向心结果经 UART 串口打印）。✅
- 蓝牙（2026-09-18 新增，**B1/B2/B3 全部真机实测通过**）：片内 LCPU 控制器 + zblue host 栈跑通 ——
  `phywear cap bt` 打印 `[bt] bt_enable rc=0`，注册 GATT 服务（`e0f1a000-…`，含传感器读数 16 B
  通知特征 + 命令写入特征）并开可被发现广播（名字 `PhyWear`）；服务端自检 `SELFTEST PASS`。
  **B3（连接 / 读 / 订阅 / 写）已打通**：宿主 BLE 中心设备实测 设备侧 6/6 + 宿主侧 9/9
  （读 16 B、|a|≈1014 mg、收到通知包、写 'ping' 成功），一条命令无人化复现
  `python3 tools/phywear/pw_bt_b3.py --central --out <dir>`。
  **主界面有连接标识**（标题右侧圆点：未连接=灰、已连接=蓝，点一下进「蓝牙」页）；
  **手表版蓝牙串口**新增文本特征 `e0f1a003`（手机写自由文本 → 手表 `[RX]` 并回
  `echo: …`；手表「发送测试」→ notify 给手机），空口连通性测试
  设备侧 8/8 + 宿主侧 13/13（`pw_bt_b3.py --central --text`）。
  **桌面上还有一个图形界面版**：`bash tools/phywear/install_desktop_shortcut.sh` 在桌面
  放一个「PhyWear 蓝牙测试」图标，双击即自动跑完同一条序列（14 项打勾 + 大字结论）。
  ⚠️ **网络：PPP over BLE 已端到端打通（2026-09-18 深夜，N1/N2 达成）** —— 设备侧 `ppp0 … at RUNNING mtu 296`、`inet addr:192.168.7.2 DRaddr:192.168.7.1`，设备上 `ping -c 3 192.168.7.1` → **3/3 收到**；一键判定 `python3 tools/phywear/pw_bt_ppp_e2e.py --out <dir>` **6/6**（证据 `docs/evidence/bt-ppp-e2e-20260918/`）；宿主侧为**用户态 PPP 对端**（无需 root）。
   **桌面还有图形界面版**：`bash tools/phywear/install_desktop_shortcut.sh` 在桌面放「PhyWear 蓝牙测试」图标，双击自动跑完 14 项并给大字结论（`docs/evidence/bt-gui-20260918/`）。
   ⚠️ 本板**没有 WiFi 网卡**（`ifconfig: open failed: 2`，厂商树无 WiFi 驱动）：`set_wifi`/DHCP 不可达；联网只能走 **BLE → PPP over BLE**（已验到 IP 层）或 USB CDC-ACM + SLIP（需插 USB 数据线，软件侧已就绪）。
  详见 `docs/16_蓝牙B1根因与修复.md`（含两个根因：NuttX fd 表按 `task_group`；
  zblue 通知帧句柄 0x0000 被对端静默丢弃）。

**性能与平台（口径分层，勿混用「benchmark / 页面帧率 / 刷新率 / loops」）**：
- **EPIC 硬件加速来自官方 PR #31/#41/#121（非本队自研）**；本队在其基础上集成优化（见下方"工作基础与归属"）。
- **真机（黄山派 SF32LB52）最新实测（2026-09-16 定案）**：
  - **LVGL 官方 benchmark（入口 `phywear lvbench`，全场景平均）**：**39 FPS / CPU 83% / render 22 ms / flush 0**；轻场景天花板 **56–63 FPS**；重场景（满屏文字）15 FPS（`docs/evidence/lvbench-20260916/`）。
  - **本队页面帧率**：**48–49 FPS（约 82%）**；**面板硬上限 60 Hz** —— CO5300AF-01 手册核实 VFR = 60 Hz（Typ.）、命令表**无帧率控制寄存器**，故 60 以上**物理不可达**（`docs/11`）。
  - **第二次优化（09-16，主循环自适应休眠）**：静置态实时页重绘 26/28 → **34/36（+30.8%）**、主屏显示刷新率 41 Hz → **63 Hz**；同轮**实测否决 6 个假设**（全屏推屏 / 防撕等待 / flush 次数 / 刷新周期 / 脏区并集 / 圆角），结论：主机侧已无可挖项（`docs/evidence/p0-20260916/`）。
  - 历史口径（引用须带日期）：43 FPS（09-12 固件 benchmark）、41 FPS（加入 AI Agent/声学后）；未优化前约 3 FPS。
- **模拟器（goldfish-arm64-v8a，无 EPIC 硬件、纯软件渲染）**：经本队 **UI/算法层优化**（弃用 lv_chart、自研 pw_scope/pw_graph 的 CPU 光栅 + lv_image 路径）后由"原本非常卡"提升到 **22–23 FPS** 稳定。
- 即：**40+ FPS 需真机硬件 + 官方 EPIC 后端**；评委无板时以**代码 + 实测证据**（`docs/03_技术报告.md`、`docs/project/fps_timeline.png`、`docs/evidence/lvbench-20260916/`）核验。
- EPIC 使能配置：`sf32lb52_lchspi_ulp/configs/nsh/defconfig`（`CONFIG_BSP_USING_EPIC=y` + `CONFIG_LV_USE_SIFLI_EPIC=y`）。

**未实现（规划中，如实标注，不冒充）**：
- 端侧运动识别（运动模式识别 / 语音快捷启动）——**未实现**；但**「摆动 → 自动跑实验」的主动+执行场景已交付并默认开启**（`PW_WATCH_PROACTIVE 1`，`ai_agent` 开机自启）。
  另：**openvelaClaw（官方 `packages/ai_agent`）已集成并在真机验证**：4 个 PhyWear 工具注册成功、
  自定义 Skill 自动装到 `/data/agent/skills/`、Agent loop 开机即启动（详见 [`docs/05_AI_Agent与Skill.md`](docs/05_AI_Agent与Skill.md)）。
  「摆动 → 自动跑实验」的**主动 + 执行场景已交付并真机验证**（`PW_WATCH_PROACTIVE 1`，`ai_agent` 开机自启）：
  晃表 → Agent 自动打开单摆页 → 测出 g → 结果进手表 AI 日志；期间修掉两个真崩溃（Agent 不在时 `velaclaw_ask`
  断言复位、工具 cJSON 双重释放），详见 `docs/evidence/proactive-20260915/`。
- 弹性碰撞能量损耗、历史频率追踪、多普勒效应——**未实现**；**音频发生器已于 2026-09-13 实现**（片内 AUDCODEC DAC0 + 功放，40 Hz–4 kHz，见 `docs/06_真机验证记录.md` §3.3）。
- 自定义实验构建器——未实现（标"构建器规划中"）。
- 传感器原始日志流导出——未实现（仅有实验结论串口打印）。

**已知限制**：模拟器窗口黑屏（qemu GPU 层 vs /dev/fb0，架构限制）、帧缓冲冻结、掌声计缺 /dev/mic0、~~SFBL 部分场景重启（电源/板级原因）~~（**已更正**：真因是 flash 起始缺 ftab，已修复，
见 `docs/06_真机验证记录.md` §6）、~~演示视频未录制~~（**已更正**：已出 v1 成片
`docs/evidence/demo-20260915/phywear-demo-v1.mp4`，4:44 / H.264 / 4.84 MiB，字幕 + 入场推近；
画面来自真机截图与真机采集数据 + 宿主实录，**非实拍动态画面** —— 真机无视频输出）。详见 README 第八节。

### 工作基础与归属（重要，如实声明）

> **本作品的 EPIC 硬件加速不是我们自研的。** 我们是在**官方已提供的硬件加速适配**基础上做工作，
> **绝不将官方成果说成"我们自研并适配"**。

- **官方硬件加速适配（非本队成果）** —— 黄山派 SF32LB52 的 EPIC 硬件加速由以下**官方 PR** 提供，
  本队**集成使用**：
  - **vendor_sifli #31** — 底层驱动：NuttX LCD/framebuffer 驱动、双缓冲、EPIC 图形加速 HAL、DMA 拷贝。
  - **apps_graphics_lvgl #41** — LVGL 渲染后端：注册 SiFli EPIC draw unit（fill/border/image/label/layer offload 到 EPIC）。
  - **nuttx-apps #121** — 编译构建支持。
- **本队在此基础上的工作（原创）**：见下方「本队主要贡献（量化）」。
- **AI 全流程辅助开发**（Claude Code + Codex）。

### 本队主要贡献（量化，如实）

在官方 EPIC 硬件加速基础上，本队独立完成 **驱动 → 算法 → 应用 → 优化 → 工程化** 全链路：

| # | 方向 | 产出 | 规模（可核实） |
|---|---|---|---|
| ① | **新传感器/音频驱动（硬件适配核心）** | MMC5603 地磁（含 auto-SR 偏置修复）、LTR-303 环境光、LSM6DSL 适配 3 个 NuttX 传感器驱动 + **AUDCODEC 麦克风（DMA 采集）与扬声器（DAC0+PA）通路**；已提 **nuttx PR #378**（checkpatch/CLA 通过） | 传感器驱动 **3,321 行**（`src/nuttx`）+ 音频驱动 **803 行** |
| ② | **触控适配** | 在官方 EPIC 显示栈上修复 FT6146 电容触控，使 UI 可交互 | `sf32lb52_mic` 之后的 `6073074` 提交 |
| ③ | **PhyWear 应用** | LVGL 全中文腕上物理工坊：主菜单 **8 板块**、`phywear cap` 可截 **17 个主页面**（原始传感器 6 子页、力学 4、声学 4（2 实现 + 2 规划）、工具 5、计时 3、生活 1）+ 惯性标尺/标定 4 子页 + 蓝牙页 + AI 教练页 | **69 个源文件 / 25,373 行**（68 个手写 + 1 个由 Skill markdown 生成的数据块 `pw_skill_blob.c`；另有 5 个生成的 CJK 字体文件） |
| ④ | **物理算法与图表库（自研）** | radix-2 FFT、自相关测周期、向心 a-ω² 最小二乘、Mahony 姿态解算（`pw_ahrs`）、六面法/磁椭球/零偏标定（`pw_calib`）；自研实时曲线控件 `pw_graph`/`pw_scope` + 动效查表（`pw_motion*`） | **约 4,256 行** |
| ⑤ | **性能工程** | 弃用 lv_chart，走通 **EPIC IMAGE 硬件 blit** 路径 → 真机 **3 → 39/48 FPS**（官方 benchmark 39 / 本队页面 48–49，2026-09-16 口径）；**第二次优化**（主循环自适应休眠）静置态实时页再 **+30.8%**、刷新率 41 Hz→**63 Hz**；实测**否决 6 个假设**并定案**面板 60 Hz 硬上限**；同一优化使**模拟器**从“很卡” → **22–23 FPS** | `docs/evidence/p0-20260916/`、`docs/evidence/lvbench-20260916/` |
| ⑥ | **国际化（i18n/CJK）** | EN/ZH 双语言字符串表 **各 282 条（共 564 条）**；Montserrat + Droid 子集生成 **5 档 CJK 字体**（14/16/20/24/28px，**535 个 CJK 码点**，脚本实测 2026-09-19） | 表 + 5 字体（`tools/phywear/gen_fonts.sh` 可重生成） |
| ⑦ | **工程化与验证** | 自建 **goldfish-phywear 模拟器板级配置**；bench 注入 + FPS/空闲堆统计；中文界面截图证据；git 标签回归 | 模拟器配置 + bench 工具 |
| ⑧ | **AI 辅助开发** | Claude Code + Codex **54 个官方会话 / 15,761 事件**（`validate-log.py` ✅ ALL OK）+ 自建 **Skill ×4** | `logs/XPQHyue/`；DSH 摘要见 `supplementary/dsh-logs/`（**仅补充佐证 AI**） |

> **一句话**：官方给了 EPIC "发动机"，本队完成了**传感器驱动、物理算法、完整应用、性能调优、国际化、工程验证**——把黄山派真正做成一台可用的腕上物理工坊。

> ⚠️ 灵感来源：玩法/实验场景源自 **phyphox**（RWTH Aachen，**GPL v3**）；本作以 **C + LVGL 独立重实现**（未复制其代码），源码为 Apache-2.0。详见 `app/phywear/NOTICE.md`。

---

## 一、作品简介

**PhyWear 把手机端 phyphox 物理实验搬到腕上**：在立创·黄山派 SF32LB52 上完成板级适配与驱动
（③ 方向的实质工作），再叠加 **openvela AI Agent（openvelaClaw）** 能力与一个**腕上智慧物理工坊**应用
——**AI 硬件产品创新是本作品申报主打方向**（①）。可实时采集板载 IMU / 地磁 / 环境光 / 麦克风，
以 LVGL 呈现 phyphox 式的物理实验页，并支持**「晃动 → Agent 自动跑实验 → 结果回到手表」**的主动闭环、
手表端「AI 教练」页、以及 **BLE GATT / PPP over BLE** 无线通路。
**未实现的部分**（语音交互、端侧运动模式识别、自定义实验构建器）见第八节"已知限制"。

**亮点（均已实现，有真实代码/测量佐证）**

- **传感器/音频驱动与板级 bringup（本队原创，③ 方向）**：新增 MMC5603 地磁、LTR-303 环境光、
  LSM6DSL 适配 3 个 NuttX 传感器驱动 + **AUDCODEC 麦克风（DMA）与扬声器（DAC0+PA）通路**，
  在黄山派 SF32LB52 上完成 bringup 与实测（已提 **nuttx PR #378**）；另修复启动时序竞争（`AB/ABCD` 卡死）
  与官方 EPIC 显示栈上的 FT6146 触控。
- **AI Agent 集成与「主动+执行」（本队原创，① 方向）**：官方 `ai_agent` 真机跑通，新增 **4 个 PhyWear 工具**、
  **自建 Skill**（开机自动装 `/data/agent/skills/`）、**离线意图快路径**；**晃动 → Agent 自动开单摆实验 →
  测出 g → 结果回手表**（默认开启）；手表端「AI 教练」页；端云 LLM 在模拟器实测（MiMo Token Plan）。
- **PhyWear 全中文应用 + UI/算法层优化（本队原创）**：主菜单 8 板块、`phywear cap` 可截 **17 个主页面**
  + 惯性标尺/标定 4 子页 + 蓝牙页 + AI 教练页，全中文界面（i18n 双表 **282 条/语言** + 5 档 CJK 字体）；
  自研 `pw_scope`/`pw_graph`（CPU 光栅 + `lv_image`）走通 EPIC IMAGE 硬件 blit 路径：真机从 ~3 FPS 提到
  **LVGL 官方 benchmark 全场景平均 39 FPS / 本队页面 48–49 FPS**（模拟器无 EPIC，同优化从"很卡"到 22–23 FPS）。
- **集成官方 EPIC 硬件加速（非本队自研）**：在官方 PR（vendor_sifli #31 / lvgl #41 / nuttx-apps #121）
  基础上完成黄山派构建集成。

### 灵感来源与合规说明（如实声明）

- **灵感来源：phyphox**（手机端物理实验工具，由 **RWTH Aachen 大学**开发，开源 **GPL v3**）。
  本作品把 phyphox 的"物理实验"玩法搬到腕上（黄山派 SF32LB52），**功能板块/实验场景、
  测量流程、部分分析算法流程（自相关测周期、向心 a-ω² 最小二乘、频谱 FFT 等）对齐 phyphox**。
- **实现方式：C + LVGL 独立重实现**。未复制 phyphox 的 Android/Kotlin 代码体；算法为通用
  教科书标准算法（radix-2 FFT 等）用纯 C 单精度浮点自研实现，源码 SPDX 为 **Apache-2.0**。
- **关于 GPL**：phyphox 以 GPL v3 发布。本作品**不复制、不链接**其 GPL 代码，不做衍生泄漏。
  为透明起见，此处**如实注明灵感与算法出处**并致谢 phyphox（RWTH Aachen）。若评审认定某
  算法模块属"紧密移植"，我们会按需将该模块以 GPL v3 单独发布以确保合规。
  （详见 `app/phywear/NOTICE.md`。）
- **配色合规**：6 个传感器曲线色**已整体平移**，避开 phyphox 的品牌主色（原 `#ff7e22`）与其命名色板。
- **工程化**：模拟器 + 真机双验证，bench 注入 + FPS/空闲堆统计；`check_progress.py` P0–P7 一致性自检。

### 团队与线上传播（如实声明）

- **团队**：`contest2026_427_xinpingqihe` — **XPQHyue（队长）**：技术负责人，负责硬件适配、
  驱动/bringup、官方 EPIC 硬件加速集成、PhyWear 应用与算法、性能调优、工程化与提交；
  **程秋霞（成员）**：文档整理与校对、演示视频制作、作品线上传播。
- **线上传播**：作品过程视频已在 **Bilibili 公开发布 2 条**（对应"商业潜力"中"线上人气与
  传播表现"评分项）：
  1. 《小米 openVela 下，适配 EPIC 硬件加速之后，效果极其显著（2 帧电竞变为了高刷）！！》
     — <https://www.bilibili.com/video/BV1cpti6GEQt/>
  2. 《小米自研开源操作系统 OpenVela 下的小项目，出具雏形了！》
     — <https://www.bilibili.com/video/BV1awYx6CESu/>
  这两条为**过程记录/传播视频**，**不等同于提交要求的正式演示视频**。提交用的演示视频见
  `docs/evidence/demo-20260915/phywear-demo-v1.mp4`（脚本由 `tools/phywear/make_demo_video.py`
  按 `docs/08` 自动生成；**若要更强的实拍版**，按 §三 的 10 条拍摄待办补拍动态镜头即可）。
  播放/点赞数据以平台页面为准，本仓库不转录。

---

## 二、选题方向

**申报主打方向：① AI 硬件产品创新**（本次最终提交按此方向）；**同时完成了 ③ 新硬件平台适配的大量实质工作**——
两条方向**不是二选一**，同一个作品在两条方向下都有可核验产出，诚实说明如下。
> 组委会 2026-09-11 口头反馈「黄山派是已有开发板，不算 ③」，因此最终按 **① AI 硬件产品创新** 申报；
> ③ 的适配工作作为「硬件/底层能力佐证」一并列出，不做主打声明。

- **① AI 硬件产品创新（申报主打）**：
  - **AI Agent 集成**：官方 `packages/ai_agent`（openvelaClaw）在 **真机**跑通，本队新增 **4 个 PhyWear 工具**
    （`src/packages/ai_agent/`）、**自建 Skill**（`phywear-physics-coach.md`，开机自动装到 `/data/agent/skills/`）、
    以及**离线意图快路径**（无网络/无 Key 也能执行工具）；
  - **「主动 + 执行」场景已交付并默认开启**（`PW_WATCH_PROACTIVE 1`）：**晃表 → Agent 自动打开单摆实验 →
    测出 g → 结果回到手表 AI 日志**，检测与执行都在端侧完成，真机验证见 `docs/evidence/proactive-20260915/`；
  - **手表端「AI 教练」页**：显示端侧 Agent 状态 + 最近回执 + 4 个快捷请求，按一下即向 Agent 发自然语言请求，
    界面会真的切页（`docs/evidence/ai-coach-20260913/`）；
  - **端云 LLM 实测**：小米 MiMo Token Plan（`mimo-v2.5-pro`），一轮问答 5 轮迭代 / 4 次工具调用；过程中
    定位并修复了「中文 Skill 被按字节截断 → 非法 UTF-8 → 云端 400」的上游 bug（`skill_loader.c`）；
  - **无线通路**：BLE host 栈打通到 **GATT（B1/B2/B3 全过）**，并进一步把 **PPP over BLE 打通到 IP 层**。
  - **未实现（如实）**：语音交互、端侧运动模式识别。
- **③ 新硬件平台适配（同时完成，实质贡献）**：在黄山派 SF32LB52 上完成 **3 个自研 NuttX 传感器驱动**
  （MMC5603 地磁 / LTR-303 环境光 / LSM6DSL 适配）、**AUDCODEC 麦克风与扬声器通路**、板级 I2C bringup、
  EPIC 显示栈上恢复 **FT6146 触控**、**启动时序竞争修复**（`AB/ABCD` 卡死，连续 5/5、6/6 复位通过）、
  官方 **EPIC 硬件加速集成**（PR #31/#41/#121，非本队自研）+ 本队 UI/算法层优化（真机 3 → 41 FPS，
  第二次优化再让实时页 +30.8%、刷新顶到面板 60 Hz 上限）。
- **应用层**：**LVGL 原生腕上物理工坊 App**（非快应用框架），多实验/工具/生活页 + 全中文界面。
- **② 手表应用创新：未采用**（用 LVGL 原生应用，非快应用框架）。

> 实交作品与报名作品一致（黄山派 + PhyWear）；方向上**以最终申报的 ① 为准，③ 作为已完成的适配贡献并列**。

---

## 三、目录结构

```
contest2026_427_xinpingqihe/
├── app/phywear/              # ⭐ PhyWear 应用源码（LVGL：69 源文件 / 25,373 行（68 手写 + 1 生成数据块）+ 5 生成字体 + skills/ + NOTICE.md）
├── src/                      # ⭐ 公共仓改动快照（逐文件来源见 src/MANIFEST.md）
│   ├── nuttx/                #   [本队原创] 传感器驱动 mmc5603 / ltr303（+ lsm6dsl 适配）+ 头文件
│   ├── vendor/sifli/         #   黄山派 BSP + [官方PR#31] EPIC 芯片层 + [本队] defconfig / 音频驱动 / 启动时序
│   ├── vendor/openvela/      #   goldfish-phywear 模拟器板级配置
│   ├── lvgl/                 #   LVGL EPIC 后端（[官方PR#41]）+ [本队] 构建修复
│   └── packages/ai_agent/    #   openvelaClaw Agent 的 PhyWear 工具（本队新增 4 个）与改动文件
├── board/                    # defconfig（nsh-ai / nsh-epic / goldfish）+ flash_with_ftab.sh + ftab_openvela.bin
├── tools/                    # 主机脚本：pwshot 截图 / boardsh 串口 / gen_fonts 字体 / pw_bt_* 蓝牙 / make_demo_video 视频…
├── .claude/skills/           # 自建 Skill ×4：devloop / reproduce / submit / migrate
├── docs/                     # ⭐ 文档 01–18 + 官方模板 docx
│   ├── 01…09                 #   事实清单 / 作品介绍 / 技术报告 / 应用场景 / AI 与 Skill / 真机验证 / 复现指南 / 视频脚本 / 官方模板填写稿
│   ├── 10…18                 #   动效与风格、面板时序与 VSYNC、UI 方案、蓝牙打通记录与根因、评分自评、商业与推广
│   ├── evidence/             #   41 组证据目录 + 4 个散落文件（真机截图 26 张 / 声学 / AI 教练 / 主动场景 / LLM / P0 性能 / 蓝牙 / 动效 / UI v2 / 演示视频…）
│   ├── design/               #   UI 原型稿与渲染图（v3–v8）
│   ├── figures/ project/     #   架构与数据流图 / FPS 时间线
│   ├── token_usage/          #   Token 用量证据（MiMo xlsx + DSH CSV）
│   └── PhyWear_技术报告_官方模板.docx
├── logs/XPQHyue/             # ⭐ AI Coding 日志（claude-code / codex，官方格式，54 会话 / 15,761 事件，validate-log ✅）
├── supplementary/dsh-logs/   # 仅补充佐证 AI：DeepSeek Harness 会话摘要
├── third_party/ui-ux-pro-max/# 上游 UI/UX 数据（MIT，v2.15.0）+ 本队新增 stacks/lvgl.csv
├── CLAUDE.md                 # 项目记忆（角色分工 / 铁律 / 常用命令）—— AI 会话必读
├── JUDGES.md                 # 评委复现指南（三条路径 + 5 分钟验收清单）
├── README.md                 # 本文件（作品说明，四要素）
├── openvela.xml              # openvela 基线清单
└── contest2026_427_xinpingqihe.xml   # 队伍清单（linkfile 映射）
```

> 说明：PhyWear 应用源码（`app/phywear`）是**本队原创作品**，位于本专属仓，评委 clone 主分支
> 即可直接审阅。本队的**新传感器驱动**已提 **nuttx PR #378**；`src/` 另含本队为集成而纳入的
> **官方 EPIC 硬件加速**源码快照（PR #31/#41/#121，**非本队原创**，详见"工作基础与归属"）。

---

## 四、运行方式

### 0) 拉取工程（官方 repo 模型）
```bash
repo init -u https://github.com/open-vela/contest2026_427_xinpingqihe \
  -b dev-ai-contest-2026 -m contest2026_427_xinpingqihe.xml
repo sync -c -j8
```

### 1) 编译
> ⚠️ **前提**：本仓 `app/phywear` 依赖公共仓的驱动/EPIC 改动。在公共仓 PR 合入前，`repo sync`
> 得到的公共仓不含这些改动，会因缺 `nuttx/sensors/mmc5603.h`、`ltr303.h` 及 LVGL EPIC 后端而
> **编译失败**。公共仓 PR 状态见第七节末。

```bash
cd <openvela 工作区根>
export PATH="$PWD/prebuilts/build-tools/linux-x86_64/bin:$PWD/prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin:$PWD/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:$PWD/prebuilts/tools/linux/x86_64:$PWD/prebuilts/tools/bin:$PATH"
# 真机（SF32LB52，EPIC 硬件加速使能）：
./build.sh vendor/sifli/boards/sf32lb52/sf32lb52_lchspi_ulp/configs/nsh -j8
# 模拟器（goldfish-arm64-v8a，无 EPIC）：
./build.sh vendor/openvela/boards/vela/configs/goldfish-arm64-v8a-ap-phywear -j8
```
> 若真机编译在自研传感器驱动上因 `-Werror` 失败，configure 加
> `-DEXTRA_FLAGS="-Wno-error -Wno-cpp -Wno-deprecated-declarations"`。
> EPIC 使能项见 `board/sf32lb52_lchspi_ulp-nsh-epic.defconfig`（`CONFIG_BSP_USING_EPIC=y`、
> `CONFIG_LV_USE_SIFLI_EPIC=y`）。

### 2) 部署 & 运行
- **模拟器**：`DISPLAY=:0 ./emulator.sh cmake_out/vela_goldfish-arm64-v8a-ap-phywear/`，
  等待 NSH `goldfish-armv8a-ap>` 提示后执行 `phywear`（自动演示）或逐页
  `phywear cap root|raw|pendulum|spring|centri|incline|ruler|spec_accel|stopwatch|...`。
- **真机**：CH340N USB-UART（UART1，1,000,000 baud，RTS→RST 复位），`sftool -c SF32LB52
  -p /dev/ttyUSB0 -b 1000000` 烧录，释放 RTS 后 `picocom -b 1000000 --noreset
  --lower-rts --lower-dtr /dev/ttyUSB0` 进控制台。详见 [`docs/07_构建烧录与复现指南.md`](docs/07_构建烧录与复现指南.md)。

### 3) 截图（中文界面）

**真机（2026-09-12 新增，推荐）**：真机没有 `/dev/fb0` 节点、NSH 也没有 dd/cat 工具，因此本队
新增了 `phywear --shot/--sweep/--p2`：板端直接读 LCD 驱动的整屏双缓冲（390×450×2 B），
按行加 crc16 后以 base64 从串口输出（链路会随机丢字节，故整帧发两遍 + 帧级哈希），
宿主机 `tools/phywear/pwshot.py` 一条命令解码成 PNG：

```bash
python3 ~/openvela/tools/phywear/pwshot.py all      # 16 主页面 + 10 说明/数据页
```

26 张真机截图证据见 `docs/evidence/realboard-20260912/`（每张值均标注数据来源：
主页面是**板上实时传感器读数**、说明/数据页是 **bench 注入的合成数据**，都不是受控实验结果）。

**模拟器**：窗口为黑（qemu 只合成 GPU 层），真实 UI 在 `/dev/fb0`：
```bash
adb pull /dev/fb0 /tmp/frame.raw
python3 - <<'PY'
from PIL import Image
d=open('/tmp/frame.raw','rb').read(); fs=1280*800*4
Image.frombytes('RGBA',(1280,800),d[fs:2*fs]).save('/tmp/frame.png')  # frame1=活动帧
PY
```

---

## 五、AI Coding 使用说明

本项目全程借助 AI 辅助开发（Claude Code / Codex 为官方支持工具；另有 DeepSeek Harness
用于长周期规划，因非官方支持工具无法导入官方日志格式，已在下方如实说明）。

- **需求拆解 / 方案设计**：AI 协助把 phyphox 功能拆成板块 / 实验页清单，并对齐 UI 骨架规范。
- **编码**：LVGL 应用页面、本队传感器驱动、FFT/自相关算法、UI/图表优化（`pw_scope`/`pw_graph`）由 AI 辅助编写并复核。（官方 EPIC 后端非本队编写。）
- **调试**：FPS 探针、bench 注入、崩溃 triage（SFBL 卡死、触控、帧缓冲冻结定位）均借助 AI。
- **文档**：README / 报告 / 本说明由 AI 协助整理。

**完整对话日志**见 `logs/XPQHyue/`（claude-code + codex，**54 个会话 / 15,761 条事件 / 12.2 MiB**，官方 `validate-log.py` 输出 **✅ ALL OK**）。

**Token 用量（真实数据）**：

| 工具 | Tokens | 计入官方 logs/ | 证据 |
|---|---|---|---|
| **小米 MiMo**（Claude Code 所用模型 `mimo-v2.5-pro`） | **451,358,504**（5,398 次请求） | ✅ 是（Claude Code 为官方支持工具） | 控制台导出 `docs/token_usage/token_plan_usage_data_202601_202612_2625305870.xlsx` |
| **DeepSeek Harness** | **4,197,874,171**（13,905 次请求；区间 2026-08-21 ~ 09-18） | ❌ 否（非官方支持工具） | `docs/token_usage/dsh/`（用量 CSV）+ `supplementary/dsh-logs/` |
| **合计** | **4,649,232,675（约 46.5 亿）** | — | — |
> ✅ Token 数字**完全由导出文件支撑、逐行解析、无估算**：
> MiMo 451,358,504（5,398 次请求，2026-08/09 月度记录，**导出截至 2026-09-11**）；DSH 4,197,874,171（13,905 次请求，**区间 2026-08-21 ~ 09-18**，费用 327.98 CNY）。
> 详见 `docs/token_usage/README.md`。

> **如实声明**：我们另通过 **DeepSeek Harness** 进行过规划与长周期任务。它**不在官方
> 支持的 4 种工具（claude-code / opencode / codex / kiro）之列**，其会话**无法导入官方
> 事件 schema**（`event.schema.json` 的 `tool` 枚举不含 harness），因此其会话置于
> `supplementary/dsh-logs/`，**仅补充佐证 AI**；其 **4,197,874,171 tokens** 用量已如实单列。
> 本仓 AI Coding 日志仅含官方支持工具（claude-code / codex）的实际会话。为体现该通道的开发过程与
> 工作量，特将 DSH 会话整理为可读摘要置于 `supplementary/dsh-logs/DSH_SESSIONS_SUMMARY.md`。

---

> **队内协作模式（2026-09-14）**：主力为 DeepSeek Harness（实现/构建/烧录/真机验证/文档/提交）；Claude Code + 小米 MiMo 在本机以**辅助**身份参与（独立复核、二次校对、产出可入 `logs/` 的日志）。`logs/` 只收白名单工具的真实会话，**禁止为凑数刷日志**；DSH 记录放 `supplementary/dsh-logs/`，**仅补充佐证 AI**。项目记忆见根目录 `CLAUDE.md`。

## 六、自建 Skill

为把「PhyWear 开发与提交流程」沉淀为可复用能力，共自建 **4 个仓内 Skill**（`.claude/skills/`），
另有 **2 个设备侧 Skill**（`app/phywear/skills/`，开机自动装到真机 `/data/agent/skills/`）。

| Skill | 触发/用途 | 关键内容 |
|---|---|---|
| `phywear-sf32lb52-devloop` | 编译 / 烧录 / 上板 / 跑 bench / 看帧率 / 截图 | 双配置构建 → sftool 烧录（RTS 复位）→ `/dev/fb0` 截中文图 → bench + FPS/空闲堆统计 |
| `phywear-reproduce` | 换机器复现 / 环境自检 / "现在做到哪一步了" | `check_progress.py`（P0–P7 共 59 项，含语义修复标记）+ `restore_code.py`（快照→工作区，默认演练）+ `manifest.json`（228 文件 151 关键） |
| `phywear-submit` | 提交 / push / 提 PR | **唯一提交入口** `submit_427.sh`（S1–S15 铁律：只脚本、不 force 官方仓、密钥/产物红线、真实邮箱、rebase-only、推送后校验远端 SHA） |
| `phywear-migrate` | 迁移 / 日志导出 / 回退点 | `finish_session.sh`（导出官方 `logs/`）、`make_rollback.sh` / `rollback.sh` 回退保障 |

**设备侧 Skill（喂给端侧 Agent，属大赛硬性要求）**：
`phywear-physics-coach.md`（5,165 B，触发"能做哪些实验 / 帮我测重力 / 这个数据什么意思"→ 调 4 个 PhyWear 工具；
开机自动安装，日志 `[phywear] installed skill …`）与 `phywear-lvgl-ui.md`（LVGL 界面规范）。

**输出规范**：始终给 `nuttx.bin` + `System.map`（源码而非 .rpk）、中文界面截图、控制台 WARN/FPS/perf 日志，
并在报告里如实写清已知限制（模拟器黑屏、真机无 WiFi、语音与运动识别未实现、Harness 日志不入官方格式等）。

---

## 七、真机 / 模拟器证据说明

- **已领硬件**：黄山派 SF32LB52 已立项适配并有真机日志（UART1 1Mbps 控制台、sftool 烧录、
  `SFBL` 引导、`openvela-ap>`/`nsh>` 提示符）。
- **模拟器证据**：goldfish-arm64-v8a-ap-phywear 模拟器上已取得**全中文界面**（根屏 +
  Raw Sensors 页）截图（`docs/evidence/`）。模拟器窗口为黑是 qemu goldfish 显示架构限制
  （GPU 合成层与 `/dev/fb0` 不互通），**非功能缺陷**——真实 UI 在 `/dev/fb0`，本仓证据即
  读 `/dev/fb0` 所得。
- **性能证据（分层说明，勿混淆平台与归属）**：
  - **真机（SF32LB52）**：官方 **EPIC 硬件加速**（PR #31/#41/#121）+ 本队 UI/算法层优化 →
    **LVGL 官方 benchmark 全场景平均 39 FPS / CPU 83% / render 22 ms / flush 0**（轻场景 56–63 FPS）；
    **本队页面 48–49 FPS（约 82%）**；**面板 60 Hz 为硬上限**（CO5300AF-01 手册 VFR=60 Hz）。
    依据：[`docs/03_技术报告.md`](docs/03_技术报告.md)、`docs/evidence/lvbench-20260916/`、`docs/project/fps_timeline.png`。
  - **模拟器（goldfish，无 EPIC 硬件）**：经本队 UI/算法层优化从"原本非常卡"到 **22–23 FPS** 稳定
    （弃用 lv_chart、自研 `pw_scope`/`pw_graph` 的 CPU 光栅 + `lv_image` 路径）。
  - **结论**：40+ FPS 需**真机硬件 + 官方 EPIC 后端**；模拟器上限约 22–23 FPS。评委无板时以
    **代码 + 上述实测证据**核验，而非在模拟器上复现真机帧率。
- **EPIC 使能方式**：真机构建配置 `sf32lb52_lchspi_ulp/configs/nsh/defconfig`
  （`CONFIG_BSP_USING_EPIC=y`、`CONFIG_LV_USE_SIFLI_EPIC=y`、`CONFIG_EXAMPLES_PHYWEAR=y`）；
  亦见 `board/sf32lb52_lchspi_ulp-nsh-epic.defconfig`（同内容，便于速览）。

### 公共仓改动提交状态（重要，影响"评委能否编译"）

- **本仓（专属仓）**：全部作品代码与文档**已在官方仓** `open-vela/contest2026_427_xinpingqihe`
  的 **`dev-ai-contest-2026`** 分支上（PR **#11 / #14 / #15 均已 rebase 合并**；提交一律走
  `submit_427.sh`，推送后校验远端 SHA）。
- **本队原创**：**nuttx** 的 `mmc5603`/`ltr303` 驱动与头文件 —— 已提 **nuttx PR #378**
  （checkpatch ✅ / CLA ✅，待组委会 review）。否则 `phywear_sensors.c` 缺头编译失败。
- **官方提供（非本队提交，等待官方 review/合入）**：
  - **vendor_sifli PR #31** —— EPIC LCD/framebuffer 驱动 / HAL / DMA 拷贝。
  - **apps_graphics_lvgl PR #41** —— LVGL EPIC draw unit 后端。
  - **nuttx-apps PR #121** —— 编译构建支持。
  （这三个官方 PR 是黄山派 EPIC 硬件加速的来源，此前长期无人 review；本队是在其基础上做工作。）
- 状态：本队 nuttx 驱动 PR 已就绪待 review；官方 EPIC PR 合入后，评委 `repo sync` 即可编译并
  在真机上复现 EPIC 加速。在此之前 clone 专属仓编译会缺依赖——**如实说明，不做"已完全可编译"的表述**。

---

## 八、已知限制（如实声明，未实现的部分不冒充）

> 以下为**真实状态**，如实列出、不夸大。核心功能（**AI Agent 集成 + 「主动+执行」场景 + 3 个传感器/音频驱动 +
> 17 个可截主页面 + 自研算法与图表 + 全中文 UI + 蓝牙 GATT/PPP**）均已实现并有代码 / 截图 / 日志佐证；
> **EPIC 硬件加速为官方 PR 提供（非本队自研）**，本队负责集成与 UI/算法层优化。下列为**未实现、受硬件限制或已知残余问题**。

### 1. AI 相关：未实现的部分
- **语音交互**（唤醒词 / 语音命令）与**端侧运动模式识别**：**未实现**。端侧 AI 已实现的是
  「工具调用 + 离线意图快路径 + 「主动+执行」场景 + 手表端 AI 教练页」。
- **LLM 对话尚未在真机联调**：真机无 WiFi，端云 LLM 在 goldfish 模拟器实测（`docs/evidence/llm-20260913/`）；
  真机走端侧离线意图 + 工具（`ask 打开单摆` → 界面真的切页）。

### 2. 网络：硬件限制与实验性通路
- 本板**无 WiFi 网卡**（运行时 `ifconfig: open failed: 2`，厂商树无任何 WiFi 驱动）⇒ `set_wifi`/DHCP 不可达。
- 已用 **PPP over BLE 绕过**（2026-09-18 实测 N1/N2 达成：`ppp0` RUNNING / `ping 3/3`，一键判定 6/6），
  属**实验性通路**（宿主侧需跑用户态 PPP 对端，不代表有互联网出口）；USB CDC-ACM + SLIP 软件侧已就绪，**需插 USB 数据线**才能实测。
  原先"SRAM 不够"的障碍已解决（工作队列/PPP 栈改从堆上要，出货 SRAM 反而比加蓝牙前低约 13 KB）。

### 3. 「主动+执行」场景的残余问题
- 晃动 → 自动跑单摆实验**已交付并默认开启**，但**静止/振动桌面上单摆页仍可能给出无效 g**：
  已有「独立数据 + 可复现 + 合理性」三道门，仍有少量漏过（70 s 内 1 条），根治需改单摆页运动判据。

### 4. 其他未实现
- **自定义实验构建器**：未实现（i18n 标"构建器规划中"）。
- **传感器原始日志流导出**：未实现（仅有实验结论的串口打印与蓝牙文本通道）。
- **多普勒 / 声呐**：未实现；**音频发生器已实现**（2026-09-13，片内 AUDCODEC DAC0 + 板载功放，40 Hz–4 kHz）。

### 5. 面板与性能上限（已定案）
- CO5300AF-01 面板 **VFR = 60 Hz（Typ.）**、命令表**无帧率控制寄存器** ⇒ **60 Hz 是物理上限**；
  本队页面 **48–49 FPS**。VSYNC/TE 在本板是死代码且 TE 引脚未走线（`docs/11_面板时序与VSYNC方案_待批.md`）。
- **圆角（`radius≠0`）与阴影会掉出 EPIC 硬件加速**（走 CPU 光栅）——这是 UI 风格取舍的已知代价
  （实测 `radius 14 → 0` 在真机上零帧率增益，故保留观感）。

### 6. 模拟器环境限制（非功能缺陷）
- 模拟器**窗口为黑**（qemu 只合成 GPU 层，真实 UI 在 `/dev/fb0`）——视觉验收以"读 fb 截屏"为准。
- 帧缓冲冻结：每启动仅首个 `phywear` 进程能渲染到 `/dev/fb0`，模拟器上逐页稳定截图困难（真机侧已绕开）。
- 模拟器无 `/dev/mic0`，自动 demo 会在掌声计页停住；**模拟器 IMU 为合成波形，不能当测量结果**。

### 7. 演示视频
- 已出 **v1 成片**：`docs/evidence/demo-20260915/phywear-demo-v1.mp4`（**4:44 / H.264 / 4.84 MiB / 有字幕、无音轨**）。
- **仍缺**：**真人实拍动态画面与讲稿** —— 真机没有视频输出，只能人用手机拍 AMOLED；`docs/08 §3` 的动作清单即实拍版待办。

### 8. 启动引导（历史坑，已修复并更正）
- ~~SFBL 引导在部分场景反复重启（电源/板级原因）~~ **已更正**：真因是 flash 起始 `0x12000000` 缺
  **ftab（FlashTable）**；修复 = 先写改好 `xip_base` 的 ftab 再写固件，已封装为 `board/flash_with_ftab.sh`。

---

> 依据评审规则"**没做完的功能，如实写进『已知限制』不扣分；冒充跑通才扣分**"，以上均为真实状态。
> 本项目**核心完整度**（AI Agent + 主动场景 + 硬件适配 + 驱动 + 性能优化 + 全中文 UI + 蓝牙/网络）已如实交付。
