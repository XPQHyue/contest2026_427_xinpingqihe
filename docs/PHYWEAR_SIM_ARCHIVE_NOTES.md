# PhyWear 无实物（模拟器）中文界面仿真成果归档

> 归档时间：2026-09-08
> 来源：openvela goldfish 模拟器（vela goldfish-arm64-v8a-ap-phywear），无实物开发线。
> 文件原始位于 `~/mimo-work/2026-09-07`、`~/mimo-work/2026-09-08-final`，此处为集中副本。

---

## 一、仿真环境
- 模拟器：openvela（vela）goldfish-arm64-v8a-ap **phywear** 专用配置
- 运行方式：`./emulator.sh cmake_out/vela_goldfish-arm64-v8a-ap-phywear/`
- UI 真实来源：**`/dev/fb0`**（NuttX framebuffer）。⚠️ 模拟器**窗口显示的是 GPU 合成层（黑屏）**，
  LVGL 实际绘制在 `/dev/fb0`，因此截图必须**读 /dev/fb0 → 转 PNG**（e3c34439 已验证，本归档即此产物）。
- 语言：全中文界面（i18n 双表 + CJK 字体，通过 `CONFIG_EXAMPLES_PHYWEAR_SIM_ZH_DEMO` 强制中文渲染，
  或 `phywear lang zh`）。

## 二、目录结构
```
phywear-sim-archive/
├── screenshots/   # 中文界面核心截图（f1=有效帧 / 读 /dev/fb0 所得）
├── demo/          # 自动导航 demo 帧序列（root→raw/mech/tool/time/every）
├── fonts/         # CJK 合并字体源码（Montserrat+Droid 子集，14/16/20/24/28）
└── reports/       # 中文 UI 验证报告 + 帧率时序图
```

## 三、关键交付物

### 中文界面核心截图（screenshots/）—— 已人工核验
| 文件 | 内容 | 尺寸 | 有效 |
|---|---|---|---|
| **`phy_core_root_zh.png`** | **中文态根屏（板块宫格：原始传感器/力学/声学/工具/计时器/生活/自定义/AI 教练，全中文无乱码）** | 1280×800 | ✅ 核心 |
| **`phy_core_raw_zh.png`** | **中文态原始传感器页（加速度计 X/Y/Z + 4 页圆点，全中文）** | 1280×800 | ✅ 核心 |
| `phy_core_root_zh_watch.png` | 中文根屏 390×450 腕表裁剪（补充视角） | 390×450 | ✅ |
| `demo_flow.gif` | 自动导航 demo 动图 | 390×450 | ✅ |

> ⚠️ 说明：根屏与原始传感器页为**核心中文证据**（用户确认为全中文、无英文、无乱码）。
> 其余子页因模拟器帧缓冲冻结（见"已知限制"）未能逐一稳定截取，已按用户要求列为**已知限制**。

### 报告（reports/）
- **`zh_ui_verification_report.md`** — 中文 UI 全流程验证：中文无乱码/切换/无卡顿/22–23FPS/内存稳定/存储余量 9MB。
  ⚠️ 该报告由开发助手生成、**未人工复核**（原文档自注）。
- **`fps_timeline.png`** — 帧率时序图（min 22 / target 43 线）。

### 字体（fonts/）
- `pw_font_{14,16,20,24,28}.c` — CJK 合并字体源码，已在 apps 分支 `phywear/i18n-cjk-redo-20260904` 落地。

## 四、代码落点
- apps 分支：`phywear/i18n-cjk-redo-20260904` @ `258644f6c`（含 i18n 双表 + CJK 字体 + UI 打磨 C 系列 + 模拟器验证入口）
- 模拟器截图入口：`apps/examples/phywear/phywear.c` 新增 `cap <name>` 子命令（打开指定屏并驻留，便于逐页截取）；
  `CONFIG_EXAMPLES_PHYWEAR_SIM_ZH_DEMO` 强制中文界面。
- 保障 tag：`regression-save-20260907-215746`、`phywear-safe-20260908-211848`

## 五、已知限制
1. **模拟器窗口黑屏**：GPU 合成层与 `/dev/fb0` 不互通——这是 qemu vela 显示架构限制，非代码问题；
   无实物视觉验证以"读 /dev/fb0 → 截图"为准（本归档即证据）。
2. **帧缓冲冻结**：模拟器每次启动后，**仅第一个 `phywear` 进程能渲染到 `/dev/fb0`**，后续进程显示同一冻结帧。
   因此**"全部子页"的中文截图未能逐一稳定获取**；当前交付以根屏 + 原始传感器页为**核心中文证据**，
   其余子页（力学/声学/工具/计时器/生活各实验页）列为**待补全**（需单进程顺序渲染或修复 fb 多次初始化）。
3. **演示卡在"掌声计"**：自动 demo 走到"生活→掌声计"时 `open(/dev/mic0)` 失败（模拟器无音频设备），进程退出。
4. **中文验证范围**：当前人工核验覆盖**根屏 + 原始传感器页**；其余子页中文未逐一核验。
