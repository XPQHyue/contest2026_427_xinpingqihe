# PhyWear — phyphox 手表版（腕上智慧物理工坊）

> **定位**：把 phyphox 的手机端功能移植到手表端（立创黄山派 SF32LB52 + LVGL）。
> **UI 板块导航对齐 phyphox**（见 §0 板块图），功能定义与算法源自 phyphox 源码
> （参考 `phyphox_reference.md`，源码克隆 `/home/xpqh/phyphox-ref/`，GNU GPL RWTH Aachen）。
> **差异化**：AI 主动交互（phyphox 没有）+ 腕上随身。

> **文档层级（本目录）**：
> - 权威：主线任务 memory（恢复用）、本文件（功能目标）
> - 算法/实验蓝本：`phyphox_reference.md`
> - ⭐ 架构/UI/导出/远程/BLE/许可全面分析（2026-09-04 纳入，外部参考归档）：
>   `phyphox_source_analysis.md`
> - GUI 规范（图表 pw_scope/pw_graph、视图模板）：`phywear_ui.md`
> - 适配与性能优化加分项归档：`phywear_optimizations.md`
> - 操作手册：`phywear_knowledge_base.md`；早期可行性研究：`phywear_feasibility.md`

## §0 板块导航（对齐 phyphox，剔除无硬件项）

```
PhyWear 手表版导航：
├─ 原始传感器   加速度 / 陀螺仪 / 磁力计 / 环境光（独立大字体实时页）
├─ 力学         摆测g / 弹簧 / 向心加速度 / 碰撞
├─ 声学         频谱 / 历史频率 / 音频发生器 / 多普勒（声呐可选，均需喇叭）
├─ 工具         加速度频谱 / 斜面倾角 / 磁性标尺 / 磁场频谱
├─ 计时器       运动秒表 / 光学秒表 / 声学秒表
├─ 生活         掌声计（麦克风响度）
├─ 自定义实验   迷你实验描述表（简化版 phyphox 编辑器）
└─ AI 交互      运动识别 / 主动建议 / 手势启动   ← 差异化亮点
```

> phyphox 有而我们做不了的：Camera(7)/GPS/气压/接近——无对应硬件。已剔除。

## 已完成基线（2026-09-03/04，全部实测 + git 提交）

- ✅ 驱动：MMC5603 地磁（含 auto-SR 偏置修复）/ LTR-303 环境光 / 模拟麦克风 / IMU。
- ✅ 算法库：radix-2 FFT + 自相关周期检测；摆测 g 实板 1.8% 误差。
- ✅ GUI UI-1/UI-2/UI-3：主菜单宫格、Raw Sensors 4 页、Pendulum 4 页（phyphox 式连续流式）上板验收。
- ✅ GUI UI-4/T5（2026-09-04）：频谱页×3（accel/mic/mag）+ 弹簧 + 向心 + 倾角 +
  磁性标尺 + 秒表×3 + 掌声计 —— 除声学（需喇叭/48k）与 AI/自定义外全部板块点亮。
  ⚠️ 本批为纯代码 + 主机单测 + bench 注入；**上板目检/实测待用户统一验收**。
- ✅ 图表渲染自研（加分项）：pw_scope（实时曲线）+ pw_graph（可缩放/平移），EPIC blit。
- ✅ phyphox 调研：算法蓝本 `phyphox_reference.md` + 全面源码分析 `phyphox_source_analysis.md`。
- git（最新）：apps `334b977bb` / nuttx `b6a3c96fce` / vendor-sifli `4a291e9` / phywear-docs `（本批）`。

## 各板块功能清单（含状态与算法）

### 1) 原始传感器（Raw Sensors，4 页大字体）
| 页 | 设备 | 状态 |
|---|---|---|
| 加速度/陀螺仪 | `/dev/lsm6dsl0` | ✅ 独立页（g/dps，实时） |
| 磁力计 | `/dev/mag0` | ✅ 独立页（mG） |
| 环境光 | `/dev/light0` | ✅ 独立页（lux/ch0/ch1） |
> 4 页横滑 + 圆点/箭头已上板验收；单位换算已做。

### 2) 力学（Mechanics）
| 实验 | phyphox 参考 | 算法 | 状态 |
|---|---|---|---|
| 摆测 g | pendulum | 陀螺仪50Hz→自相关→g=4π²L/T² | ✅ 实测 + GUI Pendulum 4 页（g/L/自相关/共振）上板验收 |
| 弹簧振子 | spring | \|a\|50Hz EMA去重力→自相关测频 | ✅ GUI 3 页 + CLI，springbench 上板通过；真实橡皮筋实测待用户 |
| 向心加速度 | centripetal | a_c=√(\|a\|²−g²)、r=过原点最小二乘 | ✅ GUI 3 页（散点+拟合线）+ CLI，centribench 上板通过；转圈实测待用户 |
| 碰撞损耗 | inelastic_collision | 麦克风48kHz弹跳计时 h=g/8·dt² | ⏸ 已评估：需 mic 48k 时钟分频改造，排到声学批次 |

### 3) 声学（Acoustics，前置=喇叭音频输出）
| 实验 | phyphox 参考 | 算法 | 状态 |
|---|---|---|---|
| 声音频谱 | audio_spectrum | radix-2 FFT | ✅ GUI Spectrum 页（mic 16k，pthread 连续采集，0-8kHz） |
| 历史频率 | frequency_history | 滑窗自相关 | 待 |
| 音频发生器 | tone_generator | CODEC DAC 音调 | 需喇叭 |
| 多普勒 | doppler | 前后频差 | 需喇叭 |
| （声呐） | sonar | 喇叭+麦 回声测距 | 需喇叭（进阶） |

### 4) 工具（Tools）
| 实验 | phyphox 参考 | 算法 | 状态 |
|---|---|---|---|
| 加速度频谱 | acc_spectrum | FFT | ✅ GUI Accel spectrum 页（3 轴多序列，0-25Hz） |
| 斜面倾角 | inclination | atan2(ax,√(ay²+az²)) | ✅ GUI Incline 页（大号角度 + 历史曲线） |
| 磁性标尺 | magnetic_ruler | \|B\|平滑→阈值峰计数 | ✅ GUI Magnet ruler 页（阈值步进 + 曲线 + Reset） |
| 磁场频谱 | mag_spectrum | FFT | ✅ GUI Mag spectrum 页（3 轴 @25Hz，0-12.5Hz） |

### 5) 计时器（Timers）
| 实验 | phyphox 参考 | 算法 | 状态 |
|---|---|---|---|
| 运动秒表 | motion_stopwatch | \|a\|-g 冲击阈值（上升沿） | ✅ GUI Motion stopwatch（事件对计时 dt） |
| 光学秒表 | optical_stopwatch | LTR-303 光暗跳变（下降沿） | ✅ GUI Light gate（阈值可调） |
| 声学秒表 | acoustic_stopwatch | 麦克风 RMS 响亮触发 | ✅ GUI Acoustic gate（dB 阈值可调；与碰撞同核心，48k 仍需改） |

### 6) 生活（Everyday life）
| 实验 | phyphox 参考 | 算法 | 状态 |
|---|---|---|---|
| 掌声计 | applause_meter | 麦克风响度(RMS/dB) | ✅ GUI Applause meter（dBFS 大号值 + 阈值计数 + 历史） |

### 7) 自定义实验（简化版，差异化卖点之一）
- **迷你实验描述表**：C 结构体数组 = {传感器, 分析函数指针, 视图缓冲, 参数元数据}。
- 用户可选「预设实验模板 + 调参数」（手表端无软键盘，用旋钮/预设）。
- 借鉴 phyphox「Simple custom experiments」，不做 XML 编辑器，做模板参数化。

### 8) AI 主动交互（差异化）
| 功能 | 方案 |
|---|---|
| 运动模式识别 | IMU 特征 + 轻量分类（摆臂/跑步/久坐） |
| 主动实验建议 | 识别运动→LVGL 弹窗推荐对应实验 |
| 手势快捷启动 | 甩腕/双击 |

## 待办 backlog（2026-09-04 起排期，P0=本批即做，P1=下周，P2=收尾/远期）

> 来源：phyphox_source_analysis.md 增补 + 本会话排期。每项含状态/依赖。
> 当前批次（2026-09-04 会话，用户已选 A+B+C）：
> **A** UI-4 频谱页（pw_graph 多序列 + FFT）→ **B** Phase 2 弹簧/向心（碰撞排后）→ 同步文档。

### P0 — 本批（UI-4 频谱 + 力学弹簧/向心）✅ 2026-09-04 完成（B3 评估后排后）

- [x] **A1** pw_graph 多序列叠加（每序列独立颜色，roadmap §7.3 第 1 条）
      → apps `9dd5907c6`；单序列 API 兼容，Pendulum 页回归无影响。
- [x] **A2** Tools → Accel spectrum（3 轴多序列 pw_graph + FFT + 主频值卡 + 波形页 + 说明页）
      → `phywear specbench` 上板通过（合成 2.3/6.1/11.7Hz 多音，103-169 loops/2s）。
- [x] **A3** Acoustics → Spectrum（mic 16kHz → FFT，复用 A2 引擎；后台 pthread 采集）
      → `phywear micspecbench` 上板通过（合成 440Hz+2kHz，119-148 loops/2s）。
- [x] **B1** 弹簧振子：|a|@50Hz EMA 去重力 → 自相关测频 f + amplitude=stddev/f²
      → GUI 3 页 + CLI `phywear spring [秒]`；`springbench` 上板通过（94-123 loops/2s）。
      → ⚠️ 真实橡皮筋实测 f 待用户物理配合（指令见 Help 页）。
- [x] **B2** 向心加速度：acc+gyr@2Hz → a_c=√(|a|²−g²) vs ω² 散点 + 拟合线（多序列）→ 过原点最小二乘 r
      → GUI 3 页 + CLI `phywear centripetal [秒]`；`centribench` 上板通过（146-165 loops/2s）。
      → ⚠️ 转盘/转圈实测 r 待用户物理配合。
- [x] **B3** 碰撞：**已评估，排后**。phyphox 需 mic 48kHz 弹跳声计时（h=g/8·dt²）；
      现 mic 16kHz，48k 需改 sf32lb52_mic.c 的 `g_mic_adc_clk` 时钟分频表
      （对照 SDK codec_adc_clk_config_xtal 48k 配置项）+ MIC_SAMPLERATE/MIC_SAMPLES
      + 频谱页采样率参数化。改动面=1 个 vendor 驱动文件，但需查 SDK 手册验证
      分频参数，风险中等 → 放到声学批次（9/6-9/10）与喇叭一起做。

### P1 — 下周（9/6-9/14）〔部分已提前完成 ✅〕

- [ ] 声学：喇叭通路验证（AUDCODEC DAC + PA42）→ 音频发生器 / 多普勒
- [x] 分析模块扩充：threshold/事件流（✅ 秒表地基，运动/光学/声学秒表共用）、
      滑动平均/峰计数（✅ 磁性标尺用）；butterworth/统计聚合/formula 待
- [ ] pw_graph：实时跟随模式（followX，roadmap §7.3 第 2 条）
- [x] 工具：斜面倾角 ✅ / 磁性标尺 ✅ / 磁场频谱 ✅（全部 GUI 落地，bench 待上板）
- [x] 计时器：运动秒表 ✅ / 光学秒表 ✅ / 声学秒表 ✅（GUI 落地，bench 待上板）
- [x] 掌声计 ✅（mic RMS/dB + 阈值计数，GUI 落地）

### P2 — 收尾/远期（9/14-9/20 及以后）

- [ ] 视图模板：T3 参数控件对接数据缓冲（edit/slider/toggle 写 buffer 闭环，来源 §7.4）
- [ ] 单位 token 系统 + i18n（`[[unit_…]]` 思路，来源 §13）
- [ ] 导出规范：CSV 科学计数法/防注入/文件名模板（来源 §9）
- [ ] pw_graph：双点取差 Marker / 对数轴（roadmap §7.3 第 3/4 条）
- [ ] BLE 伴侣模式（手表 buffer ↔ BLE GATT ↔ 手机），评估 SF32LB52 BLE 栈后定（来源 §10/§12，远期）
- [ ] 合规自检（提交/商用前）：商标避开 phyphox/RWTH；许可证声明页；若商用走干净室重写（来源 §15/§16.5）

## 里程碑（2026-09-03 起，9/20 截止）
| 日期 | 目标 | 状态 |
|---|---|---|
| 9/3-9/6 | Phase 2 力学：摆测 g ✅；弹簧 ✅；向心 ✅；碰撞已评估排后（48k 改造 → 声学批次） | ✅ 代码+bench 完成；弹簧/向心真实物理实测待用户配合 |
| 9/4-9/8 | UI-4 频谱页（A1 多序列 → A2 加速度频谱 → A3 麦克风频谱，均为 pw_graph+FFT） | ✅ 全部上板 bench 通过 |
| 9/6-9/10 | 声学：喇叭通路验证 + 音频发生器/多普勒；（掌声计已提前 ✅） | ⏳ |
| 9/10-9/14 | 工具 + 计时器（大量复用算法库） | ✅ 已提前完成（2026-09-04，bench 待上板统一验收） |
| 9/14-9/16 | 自定义实验框架(UI-5 描述表) + AI(简单版) | ⏳ |
| 9/16-9/20 | UI 打磨 + 数据导出 + 提交（文档/视频/logs） | ⏳ |
