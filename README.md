# PhyWear — 腕上智慧物理工坊（openvela 黄山派 SF32LB52）

> **队伍**：contest2026_427_xinpingqihe · **赛道**：新硬件平台适配（主业） + AI 硬件产品创新（辅助）
> **硬件**：立创·黄山派（LCKFB Huangshan Pi）· 思澈 **SF32LB52** 芯片（AMOLED + FT6146 触控）
> **提交分支**：`dev-ai-contest-2026` · 截止：2026-09-20

---

## 一、作品简介

**PhyWear 把手机端 phyphox 物理实验搬到腕上**：把 openvela 移植/适配到立创·黄山派
SF32LB52（新硬件平台适配），再在其上叠加一个**腕上智慧物理工坊**应用（产品层），
可实时采集板载 IMU / 地磁 / 环境光 / 麦克风，以 LVGL 呈现 phyphox 式的物理实验页
（加速度频谱、摆测重力加速度、弹簧振子、向心加速度、斜面倾角、磁性标尺、秒表、
掌声计等），并预留 AI 主动交互（运动识别 / 主动建议 / 手势启动）作为差异化亮点。

**亮点**

- **新硬件适配（技术主线）**：SF32LB52 全链路 openvela 适配——黄山派板级 BSP、新驱动
  （MMC5603 地磁、LTR-303 环境光、LSM6DSL IMU、模拟麦克风）、LVGL/EPIC GPU 深度优化
  把渲染从 ~3 FPS 提到 **43 FPS**。
- **产品层**：16 个物理实验功能页面（phyphox 式 UI），全中文界面（i18n 双表 + CJK 字体）。
- **工程化**：模拟器 + 真机双验证，bench 注入 + FPS/空闲堆统计。

---

## 二、选题方向

**新硬件平台适配（主业，官方重点鼓励） + AI 硬件产品创新（辅助）**，符合大赛「多方向
组合开发」的引导。

- **③ 新硬件平台适配（主线）**：把 openvela 移植到黄山派 SF32LB52 —— 板级 BSP、新驱动
  （磁力计 / 环境光 / IMU / 麦克风）、LVGL **EPIC GPU 加速**（3 → 43 FPS）。命中该
  方向评分里「技术难度 30 分」的加分项。
- **① AI 硬件产品创新（叠加在产品层）**：在适配好的板子上做 PhyWear 腕上物理工坊 +
  AI 主动交互（运动识别 / 主动建议 / 手势启动）。
- 未采用快应用方向（用 LVGL 原生应用，非快应用框架）。

> 实交作品与报名作品（黄山派 + PhyWear）一致。

---

## 三、目录结构

```
contest2026_427_xinpingqihe/
├── app/phywear/              # ⭐ PhyWear 应用源码（LVGL，42 文件，含 CJK 字体/i18n）
├── src/                      # ⭐ 全量源码快照（含来源清单 src/MANIFEST.md）
│   ├── nuttx/                #   新传感器驱动（mmc5603/ltr303/lsm6dsl）+ 头文件
│   ├── vendor/sifli/         #   黄山派 BSP（lckfb_huangshan_pi）+ SF32LB52 芯片层 EPIC 适配
│   ├── vendor/openvela/      #   goldfish-phywear 模拟器板级配置
│   ├── lvgl/                 #   LVGL EPIC 硬件加速后端（draw/sifli）
│   └── MANIFEST.md           #   逐文件来源/分支/commit 说明（证明工作量）
├── board/goldfish-phywear.defconfig   # 模拟器 defconfig（亦可读 src/ 下完整版）
├── .claude/skills/           # 自建 Skill（phywear-sf32lb52-devloop）—— 见第六节
├── docs/
│   ├── evidence/             # 中文界面核心截图（根屏 + Raw Sensors 页）
│   ├── project/              # 项目文档（黄山派 readme / 目标 / 优化 / UI 规范 / FPS 时序）
│   ├── PHYWEAR_SIM_ARCHIVE_NOTES.md  # 模拟器成果归档说明
│   └── TEAM_INFO.md          # 队伍信息表（队伍/分工/选题/进度）
├── logs/XPQHyue/             # AI Coding 日志（claude-code + codex，官方格式校验通过）
├── README.md                 # 本文件（作品说明）
├── openvela.xml              # openvela 基线清单
└── contest2026_427_xinpingqihe.xml   # 队伍清单（linkfile 映射）
```

> 说明：PhyWear 应用源码（`app/phywear`）是**本队原创作品**，位于本专属仓，评委 clone 主分支
> 即可直接审阅。新驱动 / 板级 BSP / EPIC 加速等技术改动分散在公共仓（nuttx / vendor_sifli /
> lvgl），以「源码快照 + MANIFEST 来源清单」形式纳入本仓，并按其 openvela 仓库的
> `dev-ai-contest-2026` 分支走官方 PR 提交（见四/五节）。

---

## 四、运行方式

### 0) 拉取工程（官方 repo 模型）
```bash
repo init -u https://github.com/open-vela/contest2026_427_xinpingqihe \
  -b dev-ai-contest-2026 -m contest2026_427_xinpingqihe.xml
repo sync -c -j8
```

### 1) 编译
```bash
cd /home/xpqh/openvela
export PATH="$PWD/prebuilts/build-tools/linux-x86_64/bin:$PWD/prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin:$PWD/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:$PWD/prebuilts/tools/linux/x86_64:$PWD/prebuilts/tools/bin:$PATH"
# 模拟器（goldfish-arm64-v8a-ap-phywear）：
./build.sh vendor/openvela/boards/vela/configs/goldfish-arm64-v8a-ap-phywear -j8
# 真机（黄山派 NSH）：
./build.sh vendor/sifli/boards/sf32lb52/sf32lb52_lchspi_ulp/configs/nsh -j8
```
> 若真机编译在自研传感器驱动上因 `-Werror` 失败，configure 加
> `-DEXTRA_FLAGS="-Wno-error -Wno-cpp -Wno-deprecated-declarations"`。

### 2) 部署 & 运行
- **模拟器**：`DISPLAY=:0 ./emulator.sh cmake_out/vela_goldfish-arm64-v8a-ap-phywear/`，
  等待 NSH `goldfish-armv8a-ap>` 提示后执行 `phywear`（自动演示）或逐页
  `phywear cap root|raw|pendulum|spring|centri|incline|ruler|spec_accel|stopwatch|...`。
- **真机**：CH340N USB-UART（UART1，1,000,000 baud，RTS→RST 复位），`sftool -c SF32LB52
  -p /dev/ttyUSB0 -b 1000000` 烧录，释放 RTS 后 `picocom -b 1000000 --noreset
  --lower-rts --lower-dtr /dev/ttyUSB0` 进控制台。详见 `docs/project/huangshan_pi_readme.md`。

### 3) 截图（中文界面，读 /dev/fb0）
模拟器窗口为黑（qemu 只合成 GPU 层），真实 UI 在 `/dev/fb0`：
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
- **编码**：LVGL 页面、传感驱动、FFT/自相关算法、EPIC 加速后端均由 AI 辅助编写并复核。
- **调试**：FPS 探针、bench 注入、崩溃 triage（SFBL 卡死、触控、帧缓冲冻结定位）均借助 AI。
- **文档**：README / 报告 / 本说明由 AI 协助整理。

**完整对话日志**见 `logs/XPQHyue/`（claude-code + codex，已用官方 `validate-log.py` 校验通过）。

> **如实声明**：我们另通过 **DeepSeek Harness** 进行过规划与长周期任务。它**不在官方
> 支持的 4 种工具（claude-code / opencode / codex / kiro）之列**，其会话**无法导入官方
> 事件 schema**（`event.schema.json` 的 `tool` 枚举不含 harness），因此**未计入 `logs/`**，
> 不计入 AI 工时统计。本仓 AI Coding 日志仅含官方支持工具（claude-code / codex）的实际会话。

---

## 六、自建 Skill

为把「PhyWear 开发流程」沉淀为可复用能力，自建了 Skill：**`phywear-sf32lb52-devloop`**
（见 `.claude/skills/phywear-sf32lb52-devloop/SKILL.md`）。

- **触发词**：编译 phywear / 烧录 / 上板 / 跑 bench / 看帧率 / 截图中文界面 / 测 FPS / EPIC 加速验证。
- **操作步骤**：① 清理构建固件（模拟器 / 真机两套 config）→ ② 烧录真机（sftool + RTS 复位）
  或启动模拟器 → ③ 运行应用并读 `/dev/fb0` 截中文图 → ④ 跑 bench + FPS/空闲堆统计调优。
- **输出规范**：始终给出 `nuttx.bin` + `System.map`（源码而非 .rpk）、中文界面截图、
  控制台 WARN/FPS/perf 日志，并在报告里如实写清已知限制（模拟器黑屏、帧缓冲冻结、
  掌声计缺 mic0、Harness 日志无法入官方格式等）。

---

## 七、真机 / 模拟器证据说明

- **已领硬件**：黄山派 SF32LB52 已立项适配并有真机日志（UART1 1Mbps 控制台、sftool 烧录、
  `SFBL` 引导、`openvela-ap>`/`nsh>` 提示符）。
- **模拟器证据**：goldfish-arm64-v8a-ap-phywear 模拟器上已取得**全中文界面**（根屏 +
  Raw Sensors 页）截图（`docs/evidence/`）。模拟器窗口为黑是 qemu goldfish 显示架构限制
  （GPU 合成层与 `/dev/fb0` 不互通），**非功能缺陷**——真实 UI 在 `/dev/fb0`，本仓证据即
  读 `/dev/fb0` 所得。
- **性能证据**：LVGL + EPIC GPU 加速把渲染从 ~3 FPS 提到 **43 FPS**（`docs/project/phywear_optimizations.md`
  有 bench 数据；`docs/project/fps_timeline.png` 时序图）。
