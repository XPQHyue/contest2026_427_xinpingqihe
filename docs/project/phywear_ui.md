# PhyWear LVGL GUI 界面规划（phyphox 板块导航 + 手表约束）

> 版本：2026-09-03（随 phyphox 板块重构对齐）。屏幕：1.85" AMOLED 390×450，触摸。
> 铁律：操作 ≤3 步、大字体卡片、深色主题（OLED 省电）。

## 1. 设计约束与原则

| 约束 | 处理 |
|---|---|
| 390×450 小屏 | 内容区卡片化，一屏一主题 |
| 触摸 | 触控目标 ≥80px，间距充足 |
| 操作 ≤3 步 | 主菜单 → 实验列表 → 实验界面 |
| 大字体 | 数值 montserrat 28/48，标题 24，正文 14/16 |
| OLED | 深色底（#101418），高亮色区分板块 |

## 2. 导航架构（3 层）

```
┌ 启动 → 主菜单(板块宫格)
│         ├─ 原始传感器 ─ 4 页滑动（加速度/陀螺仪/磁力计/光）
│         ├─ 力学 ── 摆测g / 弹簧 / 向心 / 碰撞
│         ├─ 声学 ── 频谱 / 历史频率 / 发声器 / 多普勒
│         ├─ 工具 ── 加速度频谱 / 倾角 / 磁性标尺 / 磁场频谱
│         ├─ 计时器 ─ 运动秒表 / 光学秒表 / 声学秒表
│         ├─ 生活 ── 掌声计
│         ├─ 自定义实验 ─ 模板选择 + 参数
│         └─ AI 交互 ─ 模式识别页 / 建议
```

### 主菜单（板块宫格）
- 布局：2 列 × 4 行大瓷砖（约 185×100），每块 = 图标色块 + 板块名 + 实验数。
- 顶部小标题 "PhyWear"，底部留 AI 状态行（可选）。
- 触摸：点击进入实验列表。

### 实验列表
- 板块下实验列成大行按钮（高 ~80px，实验名 + 一句话）。
- 返回主菜单用左上返回箭头。

### 实验界面（模板按实验类型，见 §4）

## 3. 通用 UI 骨架（LVGL 结构）

```c
/* 每屏 = 一个 build 函数 + lv_scr_load，用「迷你实验描述表」驱动渲染 */
struct ui_template {
  const char *title;
  lv_obj_t *build_screen(struct experiment *exp);  /* 返回新屏 */
  void on_tick(struct experiment *exp);            /* 周期刷新 */
};

/* 顶栏 + 内容区 + 底部操作的标准布局函数 */
static lv_obj_t *ui_screen_new(const char *title) {
  lv_obj_t *scr = lv_scr_load(lv_obj_create(NULL));
  ui_topbar(scr, title);          /* 返回箭头 + 标题 */
  return scr;                     /* 内容区由模板填充 */
}
```

### 统一实验页骨架（UI 规范，2026-09-04 增补，参考 phyphox ExpView §7.3）

> 所有采集/实验页统一按下列骨架组织，保证"每个实验 ≤3 步到结果 + 观感一致"。
> 该骨架是**规划规范**：当前 Pendulum 已实现其主体，其余实验页照此实现。

```
┌────────────── 顶栏：‹ 标题（+ 主实验值/计时 小字可选）──────────┐
├────────────── 工具行（常驻，后续统一提供）────────────────────┤
│   [开始] [停止] [清空]   （+计时显示；喇叭后加 [定时+蜂鸣]）    │
├────────────── 多视图区（横滑 + 圆点/箭头，与 Raw/Pendulum 同款）┤
│   视图 = 值卡 / 图卡 / 参数卡 / 说明卡 按模板自由组合          │
├────────────── 状态行（Live / 提示 / 范围反馈等）──────────────┤
```

- **值卡（value 元素对标）**：大号数值需声明元数据：{单位 unit, 小数位 precision,
  换算 mapping(可选, 如 mG→µT)}——数值显示层统一实现，避免各实验手拼字符串。
- **图卡**：默认用 `pw_scope`（实时滚动）或 `pw_graph`（可缩放/平移/散点），
  见 §7.1/§7.3；新增实验用图先对照路线图再定能力。仅低频/静态结果图
  可按 §7.1「lv_chart 适用边界」用 lv_chart。
- **参数卡（edit 元素对标）**：步进/预设/滑块/开关等输入控件目前改界面参数；
  规划上控件应写"实验参数缓冲"，为 UI-5 引擎（缓冲=全局总线）铺路——见 goals backlog。
- **工具行**随实验类型启用（采集类=开始/停止/清空；秒表类=开始/计圈/清零；
  声学发生器类=频率参数+开关）。无喇叭前"定时+蜂鸣"不启用（honest planned）。

## 4. 实验界面模板（按类型复用）

> 模板即「统一实验页骨架（§3）」的内容区组成。**采集/分析类实验至少两视图**：
> ① 结果值/大数值视图；② 图（或自相关）视图 —— 与 phyphox「Results + Graph +
> Raw」多页签的规律一致（详见 phyphox_source_analysis.md §7.3/§14 内容规律）。

### T1 原始传感器页（大数值实时）
```
[← 加速度]           ← 顶栏(可左右滑动切页)
┌────────────────────┐
│   ax   +0.12 g      │  大字体 48 实时刷新
│   ay   -0.03 g      │
│   az   +0.99 g      │
│   [单位切换]         │
└────────────────────┘
```
- 实时读 /dev 设备，lv_label_set_text_fmt 每 100ms 刷。
- 4 传感器页 lv_obj 平铺，滑动切换（lv_roller/手动 swipe）或点 tab。

### T2 采集-分析实验（摆测 g / 弹簧 / 向心 / 碰撞）
```
[← 摆测 g]
┌────────────────────┐
│   ▶ START          │  大字大按钮
│   状态行：请摆动…   │
│ ┌────────────────┐ │
│ │  T=1.421s       │ │  结果卡片(大字体)
│ │  f=0.704Hz      │ │
│ │  g=9.72 m/s²    │ │
│ └────────────────┘ │
│   [重试] [参数:L=0.50m]│
└────────────────────┘
```
- START → 采集 10s（进度条/计数）→ 分析 → 结果卡片。
- 参数行点击进 T3。
- **落地现状（2026-09-04）**：Pendulum（4 页）/ Spring（3 页：Measure f /
  Autocorr / Help）/ Centripetal（3 页：Measure r / Plot(散点+拟合线) / Help）。
  三者均为「进页即连续流式采集」（无 START 按钮，与 phyphox 一致）；回归命令
  pendbench / springbench / centribench。碰撞排后（需 mic 48k）。

### T3 参数输入（无软键盘 → 旋钮/预设）
```
[← 摆测 g]  参数
┌────────────────────┐
│ 摆长 L (0.05–5m)    │
│   [0.50] [-] [+]    │  步进 0.01 / 长按加速
│ 预设：0.10 0.30 0.50│  快捷值瓷砖
│  [确定]              │
└────────────────────┘
```

### T4 频谱/示波器（声学·工具）
```
[← 声音频谱]
┌────────────────────┐
│        ▂▄█▄▂▅█▂    │  lv_chart 实时频谱
│  频率轴标注          │
│ 主导频: 440Hz        │  数值行
└────────────────────┘
```

### T5 计时器（秒表）
```
[← 光学秒表]
┌────────────────────┐
│      03.247s       │  大字体 48
│  [开始/停止] [清零]  │  大按钮
└────────────────────┘
```
- **落地现状（2026-09-04）**：Motion stopwatch / Light gate / Acoustic gate
  （Timers 3 页全 live）。语义=phyphox motion_stopwatch 事件对：第 1 次触发开始、
  第 2 次停止并显示 dt，下次触发自动开始新测量；Clear 清零。阈值状态机
  pw_thresh（滞回+re-arm）在 pw_analysis，主机单测覆盖；光/声阈值可调。

## 5. 触摸交互与 3 步校验

| 路径 | 步数 |
|---|---|
| 看摆测 g 结果 | 主菜单(1) → 力学(2) → 摆测g(3) → START(在实验内) ✅ |
| 调摆长再测 | 同上 + 参数页 = 4 步？→ 优化：参数直接放实验页(点即改) 保持 ≤3 |
| 看磁力计数值 | 主菜单(1) → 原始传感器(2) → 滑到磁力计页(3) ✅ |

> 规则：参数输入**嵌入实验页**（不单独成层），保证任何实验 ≤3 步到结果。

## 6. 实现里程碑（LVGL）

| 步骤 | 内容 | 状态 |
|---|---|---|
| UI-1 | 屏幕管理器 + 主菜单宫格 + 顶栏/返回（骨架） | ✅ 2026-09-03 已实现并编译通过（apps `6f7af0259`） |
| UI-2 | T1 原始传感器 4 页（已有卡片改造 + 滑动） | ✅ 2026-09-03 已实现并编译通过（同上） |
| UI-3 | T2 采集-分析模板 + T3 参数控件（接摆测 g） | ✅ 2026-09-03：Pendulum 实验（4 页横滑，phyphox 式连续流式） |
| UI-4 | T4 频谱页（pw_graph 多序列 + FFT，禁 lv_chart）+ T5 计时器 | ✅ 2026-09-04：频谱×3（accel/mic/mag，specbench 系列上板通过）+ T5 秒表×3（运动/光学/声学，timebench 待上板统一验收） |
| UI-5 | 迷你实验描述表驱动渲染（实验只需声明，UI 自动） | ⏳ 待做 |
| UI-6 | 打磨：主题色、动画、AI 状态行 | ⏳ 待做 |

### 已落地细节（2026-09-03 第一版）

- 代码模块化：`phywear_ui.c`（屏幕管理/主菜单/列表）、`phywear_raw.c`（原始
  传感器 4 页横滑）、`phywear_sensors.c`（/dev 访问 + 物理单位换算）、
  `phywear.c`（入口 + pendulum CLI 保留）。
- 板块宫格 8 块（Raw Sensors / Mechanics / Acoustics / Tools / Timers /
  Everyday / Custom / AI Coach）；未实现板块灰字 "planned"，如实标注不冒充。
- 原始传感器页：横滑 + 底部圆点 + 左右箭头；仅刷新当前可见页省 I2C。
- 单位换算（驱动实测）：加速度 g（mg/1000）、陀螺 dps（mdps/1000）、
  地磁 mG（counts×0.0625）、环境光 lux（(CH0−CH1)×0.6）。
- Pendulum 实验页（Mechanics 首例，phyphox 对齐）：连续流式采集 + 实时
  T/f/g/L + 4 页横滑（Measure g / Measure L / Autocorrelation / Resonance）。

### 目检记录

- UI-1/UI-2/UI-3 布局/字体/手势/触控已上板验收通过（2026-09-03）；后续新增页照此标准目检。
- 中文文案（CJK 字体）未做——先英文，汉化走 i18n 字符串表 + CJK 子集字体。

## 7. 说明（GUI 规范与关键约束）

- 复用现有 `phywear.c`（深色卡片 + 大字体 + 10Hz 刷新）作为 T1 起点。
- 每屏 UI 用「描述表 → 模板渲染」避免逐实验写死（phyphox 声明式思路）。

### 7.1 图表/曲线绘制规范（重要，性能实测结论）

> 本板 LVGL 的 SiFli EPIC draw unit 只加速 `FILL/BORDER/IMAGE/LABEL/LAYER`，
> **不含 LINE**；`lv_chart` 折线走软件渲染，逐帧重绘在本板是灾难级开销
> （300 点任何刷新方式都使 UI 从 ~168 loops/2s 崩到 2 loops/2s；96 点也只 ~4fps）。
> **实时/可交互曲线用自研控件（pw_scope/pw_graph），默认禁用 lv_chart。**
>
> **lv_chart 适用边界（2026-09-04 复盘补充，替代"一律禁用"的绝对化表述）**：
> - ❌ 不适合 = 实时连续刷新（数据流 ≳5Hz、逐帧/滚动重绘）、需要缩放/平移/多序列
>   交互的图 —— 一律 pw_scope/pw_graph，新图先 bench。
> - ✅ 可接受 = **低频/静态结果图**：一次性数据、几十点，仅在结果更新或用户交互时
>   才重绘、间隔 ≳几百 ms（实测 lv_chart 96 点 + 240ms 批量也仅 ~4fps，说明
>   "重绘很稀疏"才是它的真实舒适区；一旦持续重绘即性能回归，以 bench 为准）。
> - 决策习惯：新图默认走 pw_scope/pw_graph（一套引擎、统一交互、天然可升级实时）；
>   确需 lv_chart 时先 bench 留数据并在 commit 写明理由。勿被 demo 表象误导——
>   LVGL widgets demo 折线仅 12 点且只在触摸时重绘（lv_demo_widgets.c），
>   与 PhyWear 实时曲线（300 点 @25fps ≈ 7500 段/s）负载差数百倍。

| 控件 | 用途 | 用法 |
|---|---|---|
| `pw_scope` | 实时滚动波形（无缩放） | `pw_scope_create(parent,w,h,line,bg)` → `pw_scope_set_data(obj,y,n)`（y∈[-1,1]）；`pw_scope_axes(obj,color)` 加零线/边框 |
| `pw_graph` | 可缩放/平移的折线-散点图 | `pw_graph_create(parent,w,h,line,bg,dots)` → `pw_graph_set_data(g,x,y,n)`（数据单位）；`pw_graph_zoom(g,axis,f)` / `pw_graph_pan(g,dx,dy)` / `pw_graph_fit(g)` / `pw_graph_set_auto(g,1)`；`pw_graph_get_range` 取窗口反馈 |

- 原理：CPU 把曲线光栅化到一块 RGB565 缓冲 → 挂 `lv_image` → 交 EPIC 的
  IMAGE 任务硬件 blit（43fps 快速路）。缩放/平移只改视图窗口重画，成本低。
- 交互（单点触摸，FT6146 `maxpoint 1`，**无法双指捏合**）：图上拖动=平移；
  图下按钮 `X- X+ Y- Y+` 独立缩放、`Fit` 回全览、`Auto` 跟随数据；图对象需
  声明 `SCROLLABLE + scroll_dir ALL + 关滚动链`，以免横向拖动被外层翻页容器抢走。
- 性能回归工具：`phywear pendbench [秒 [起始页]]`（合成摆动 + 自动翻页 + 帧率/
  空闲堆打印），新图上线前先 bench。

### 7.2 视图元素对标（phyphox 12 种 → 我们的模板）

> 来源：phyphox_source_analysis.md §7.4（ExpView）。我们用「T-模板」收敛为手表版子集。

| phyphox 元素 | 用途 | 我们对应/规划 |
|---|---|---|
| graph | 主图（多序列） | pw_graph（折线/散点）已落地；多序列待做 |
| value | 大号实时数值（size/precision/unit/mapping） | T1 数值标签（单位换算已做；precision/映射待做） |
| edit / button / toggle / slider / dropdown | 参数写入数据缓冲 | 内嵌步进/预设（T3 雏形）；toggle/slider 待做 |
| info / separator / image | 说明/分组/插图 | info 文字已用；插图待做 |
| camera-gui / depth-gui | 相机/深度 | 无硬件，剔除 |

> 关键设计（可借鉴）：**输入型控件直接写数据缓冲 → 实时改变分析参数**。
> 我们参数控件目前只改界面值，未接入分析缓冲；做 UI-5 描述表引擎时对齐。

### 7.3 图表能力路线（pw_graph roadmap，源自 phyphox §7.5）

已落地：折线/散点、X/Y 独立缩放、拖动平移、Fit/Auto、坐标轴、单点手势（无捏合）、
**多序列叠加**（pw_graph_begin/add_series/end，每序列独立颜色；单序列 API 保留）。
待做（按价值排序）：
1. ~~多序列叠加~~ ✅ 2026-09-04 落地（Accel spectrum 3 轴同图在用）；
2. 实时跟随模式（followX：只画新数据 + 自动滚窗）；
3. 图上双点取差 Marker（测峰间距）待做；~~线性拟合叠加~~ 已用多序列实现
   （Centripetal 页：散点 + 拟合线两序列同图）；
4. 对数 X/Y 轴；
5. 时间轴"暂停段"标记（我们连续流式无暂停概念，暂低优先）。
> 实现约束：实时/交互曲线一律 pw_scope/pw_graph（CPU 光栅+EPIC blit），先 bench
> 再上线；仅低频/静态结果图可按 §7.1「lv_chart 适用边界」例外。

### 7.6 全套 bench 回归结果（2026-09-04，apps `5ecc00bdb` 上板实测）

> 12 项 bench 全部通过：无崩溃/断言/硬错误，空闲堆全程稳定（min≈max）。
> avg loops/2s（越大越空闲；lv_chart 灾难线 ≈ 2-4）：pendbench 84、specbench 145、
> micspecbench 140、magspecbench 145、springbench 97、centribench 156、
> inclinebench 56、rulerbench 100、timebench motion 141 / light 140 / acoustic 153、
> applausebench 149。inclinebench 偏低源于 12.5Hz 历史曲线重绘，属预期（UI 仍流畅）。
> 优化记录：大号数值标签节流（incline 25Hz、秒表当前值 10Hz）使 timebench
> motion/light 从 ~56 → ~141 loops/2s。

### 7.5 频谱页落地要点（UI-4，2026-09-04）

- 模块 phywear_spec.c：统一 FFT 引擎，三个实例（Accel spectrum / mic Spectrum / Mag spectrum）。
- 分析链：去均值 → Hann 窗 → radix-2 FFT（pw_analysis）→ 按全局最大幅度归一化
  （多序列共用 Y 0..1）→ 主频=抛物线插值（跳过直流）。
- Accel：3 轴 @50Hz，256 点窗（5.12s），df=0.195Hz，0..25Hz，分析节流 400ms；
  mic：@16kHz，1024 点窗（64ms），df=15.6Hz，0..8kHz，后台 pthread 采集
  （read 阻塞 64ms 不进 LVGL 定时器），UI 节流 5Hz。
- 页：Spectrum（多序列图+主频值卡）/ Waveform（pw_scope）/ Help，统一骨架。
- 回归：phywear specbench [秒 [起始页]]（合成 2.3/6.1/11.7Hz 多音）、
  phywear micspecbench（合成 440Hz+2kHz）；实测 103-169 loops/2s、堆 7.49MB 稳定。

### 7.4 其他

- **i18n 决策（2026-09-03）**：第一版 UI 用英文（montserrat 无 CJK 字形，
  中文会渲染占位方框）；文案集中在 `phywear_ui.c` 的常量数组，汉化时替换
  字符串表并启用 `lv_font_simsun_16_cjk` 或自建 CJK 子集字体。
- **字体**：需 `CONFIG_LV_FONT_MONTSERRAT_28=y` + `_48=y`（defconfig 已加，
  大号数字页用）。
