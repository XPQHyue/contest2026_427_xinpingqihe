# PhyWear 真机中文界面截图（2026-09-12 深夜）

> **这是真机截图，不是模拟器截图。**
> 板子：立创·黄山派 SF32LB52-MOD-1-N16R8（16 MB NOR + 8 MB OPI-PSRAM + 1.85" AMOLED + FT6146）
> 采集时间：2026-09-12 22:35–22:45
> 固件：`~/openvela/cmake_out/sf32lb52_lchspi_ulp_nsh_v2/nuttx.bin`
> **1,766,480 B，md5 `6dc11860ef027f0e2869749ad497bfd9`**
> （= 当前源码树：PhyWear + 官方 EPIC 硬件加速 + FT6146 触控修复 + 全中文 i18n + pw_ai 桥
> + 本次新增的 `phywear --shot/--sweep/--p2` 真机截图能力）
> 烧录：`~/openvela/flash_with_ftab.sh`（先写 ftab 再写固件；写后读回校验通过）
> 采集工具：`~/openvela/tools/phywear/pwshot.py`（宿主机）+ `apps/examples/phywear/pw_shot.c`（板端）

---

## 〇、先看这两张

| 文件 | 内容 |
|---|---|
| `00_主页面总览.png` | **16 个主页面**拼图（4×4） |
| `00_说明数据页总览.png` | **10 个说明/数据页**拼图（5×2） |

---

## 一、⚠️ 数据来源声明（引用前必读，铁律 4）

| 组 | 页数 | 数值来源 | 能不能当测量结果引用 |
|---|---|---|---|
| `00_root` … `15_about`（16 张） | 16 | **板上实时传感器**（LSM6DS3 / MMC5603 / LTR-303 / 麦克风） | ❌ **不能**。板子当时静止放在桌面上，**不是受控实验**，读数只是"传感器是活的"的证据 |
| `20_pend_p2` … `29_applause_p2`（10 张） | 10 | **bench 注入的合成信号** | ❌ **不能**。这 10 页是先用 `--p2` 打开 bench 注入再截的，图上数值全部是合成测试数据 |

- 真机上**真正可引用的实测数字**仍然只有两处：`lvgldemo benchmark` 的
  **3 → 43 FPS**（见 `~/mimo-work/2026-09-12-realboard/README.md` 第三章），
  以及 `phywear pendulum` 的**摆测 g 误差 1.8%**。
- 本批图的作用是：**证明全中文界面在真机上确实渲染正确**（含 CJK 字体子集、i18n、
  「拟合/自动」按钮、说明页文案），以及证明各页面在真机上都能正常构建与显示。

---

## 二、16 个主页面

| # | 文件 | 页面 | 真机上看到的（示例） |
|---|---|---|---|
| 00 | `00_root` | 根菜单（8 板块宫格） | 腕上物理工坊 / 原始传感器 / 力学 / 声学 / 工具 / 计时器 / 生活 / 自定义 / AI 教练 |
| 01 | `01_raw` | 原始传感器 | X +0.89 g、Y +0.45 g、Z +0.01 g（静止放桌上，合模长≈1 g） |
| 02 | `02_pendulum` | 单摆 | L = 0.50 m；g、T 待测 |
| 03 | `03_spring` | 弹簧 | f、振幅待采集 |
| 04 | `04_centri` | 向心力 | r、当前 w、采样点数 |
| 05 | `05_incline` | 倾角 | +63.2 deg（板子当时斜放）、ax/ay/az |
| 06 | `06_ruler` | 磁性标尺 | 0 块磁铁、剩磁 900 mG（阈值） |
| 07 | `07_spec_accel` | 加速度频谱（FFT） | X/Y/Z 三通道频谱 |
| 08 | `08_spec_mic` | 麦克风频谱（FFT） | 主峰 353.9 Hz |
| 09 | `09_spec_mag` | 磁场频谱（FFT） | X/Y/Z、5.3 Hz |
| 10 | `10_stopwatch` | 运动秒表 | 已就绪·等待事件 |
| 11 | `11_lightgate` | 光学快门 | 阈值 20 lux |
| 12 | `12_acousticgate` | 声学快门 | 阈值 -76 dB |
| 13 | `13_applause` | 掌声计 | 0.3 次/秒 |
| 14 | `14_settings` | 设置 | 界面语言：英文 / 中文 |
| 15 | `15_about` | 关于 | PhyWear v0.1 / Apache-2.0 / 致敬 phyphox |

> `05_incline` 因为截图时板子是斜着放的，显示 +63.2 deg —— 这是**真实读数**，不是错误。

## 三、10 个说明 / 数据页（数值全部为 bench 合成数据）

| # | 文件 | 页面 |
|---|---|---|
| 20 | `20_pend_p2` | 单摆 · 摆长测量页 |
| 21 | `21_spring_p2` | 弹簧 · 自相关页（`T = 0.500 s（首峰）`） |
| 22 | `22_centri_p2` | 向心力 · 图表页（`a vs w^2 – 斜率 = r`） |
| 23 | `23_incline_p2` | 倾角 · 历史页（最近 16 s） |
| 24 | `24_ruler_p2` | 磁性标尺 · 磁场页（`1 个峰`） |
| 25 | `25_spec_p2` | 加速度频谱 · **波形页**（`y: +-0.5 g`） |
| 26 | `26_stopwatch_p2` | 运动秒表 · 帮助页 |
| 27 | `27_lightgate_p2` | 光学快门 · 帮助页 |
| 28 | `28_acousticgate_p2` | 声学快门 · 帮助页 |
| 29 | `29_applause_p2` | 掌声计 · 历史页 |

---

## 四、怎么复现（一条命令）

```bash
# 一次跑完 16 主页面 + 10 说明/数据页（约 12 分钟；中途会自动 RTS 复位换进程）
python3 ~/openvela/tools/phywear/pwshot.py all --settle 2000 --p2-settle 6000 \
        --out ~/mimo-work/2026-09-12-realboard-shots

# 只要主页面 / 只要说明页 / 单页
python3 ~/openvela/tools/phywear/pwshot.py sweep
python3 ~/openvela/tools/phywear/pwshot.py p2
python3 ~/openvela/tools/phywear/pwshot.py shot pendulum

# 生成两张总览拼图
cd ~/mimo-work/2026-09-12-realboard-shots && python3 make_overview.py
```

---

## 五、实现要点与踩坑（如实记录）

### 5.1 板端：为什么必须自己造截图能力

真机固件的 NSH **没有 `dd`/`cat`/`fb`**，板级配置走 `lcd0`、**没有 `/dev/fb0` 节点**
（`WARN: fps probe: open /dev/fb0 failed: 2` 就是这件事），模拟器那套
`adb pull /dev/fb0` 完全用不上。

`pw_shot.c` 的做法：

1. 取**板级 LCD 驱动的 PSRAM 全屏双缓冲**（`CONFIG_LCD_FB_USING_TWO_UNCOMPRESSED`，
   390×450×2 B = **351,000 B/帧**）：调 `get_disp_buf()`
   （定义在 `vendor/sifli/boards/sf32lb52/drivers/lcd/lv_lcd.c`）。
2. **强制两次整屏重绘**（`lv_obj_invalidate(active)` + `lv_refr_now(NULL)`），
   让双缓冲两块都拿到同一张完整画面，这样读到哪一块都对。
3. 读之前 `up_invalidate_dcache()`，避免 CPU 读到陈旧的 cache line（缓冲在 PSRAM，由 DMA/EPIC 写）。
4. 隐藏 `lv_layer_sys()` 上的子对象 —— 否则 `CONFIG_LV_USE_PERF_MONITOR` 的
   "FPS / CPU / ms" 浮层会印进截图里。
5. 以 base64 分块打印到控制台（`SHOT-BEGIN … / SHOT-END …` 帧格式）。
   宿主机 `pwshot.py` 解码成 PNG（RGB565 → RGB）。

### 5.2 串口链路会丢字节 —— 所以加了「逐行校验 + 整帧两遍」

实测（2026-09-12）：**单趟 1829 行会丢 5–11 行**；加 4 ms/行节流反而丢 11 行
（说明不是 CH340 FIFO 溢出，而是 USB/VMware 链路的随机丢字节）。

对策（全部在 `pw_shot.c` / `pwshot.py` 里）：

| 机制 | 说明 |
|---|---|
| 逐行 16 位校验 | 每行 `<seq>:<base64>:<crc16>`，校验和覆盖该行原始字节 |
| **整帧发两遍** | 两个 pass 之间的丢失互不相关，宿主机逐 seq 取**第一个通过校验**的副本 |
| 帧级 FNV-1a | `SHOT-BEGIN` 头里带整帧 32 位哈希，重组后再校验一次 |
| 帧头重复 | 帧头没有自带校验，所以打两遍（第二遍用于第一遍被截断的情况） |
| 页级重试 | 仍失败的页，宿主机复位后单独重截（本次 26 页里有 1 页走了重试） |

### 5.3 一个 boot 只能跑一次 phywear

实测：**同一 boot 内第二次运行 `phywear` 会挂在 LCD 驱动里**
（第二次运行打印到 `WARN: fps probe` 之后就没有任何输出，120 s 无响应）。
所以采集策略是「**一个进程截一批**」：16 个主页面用一次 `--sweep` 跑完，
10 个说明/数据页各自一个进程，进程之间用 **RTS 脉冲复位**
（CH340N 的 RTS 直连 SoC 复位，`pwshot.py` 里实现；**全程不使用 erase_flash**）。

### 5.4 顺带修掉的真问题

1. **`phywear.c` 真机配置下编译不过**：`capsweep` 的参数解析代码在
   `#ifdef CONFIG_EXAMPLES_PHYWEAR_SIM_ZH_DEMO` 之外，而它用的
   `cap_sweep`/`cap_sweep_dwell` 定义在里面 → 真机配置（该宏关闭）报
   `cap_sweep undeclared`。已给该分支补 `#ifdef` 守卫。
   **这也说明 9/11 那批 i18n 改动之后，真机固件其实没有被重新编译过。**
2. **i18n 双重转义缺陷**：`phywear_i18n_tables.inc` 里 **10 条字符串写成了
   `\\xe2\\x86\\x92` / `\\xc2\\xb1` / `\\xc2\\xb0`**，界面上会直接显示
   `\xe2\x86\x92` 这样的字面文本（例：加速度频谱页 y 轴标签）。
   已改为 ASCII `->` / `+-`，角度符号改用字体里确实存在的 `°`。
   > 说明：本套 CJK 字体子集的 Montserrat 段只带 `0xB0/0xB2`，**不含 `0xB1(±)` 和
   > `0x2192(→)`**，所以这两类符号用 ASCII 替代，不必重做字体子集。

---

## 六、文件清单与校验

```
2026-09-12-realboard-shots/
├── 00_主页面总览.png            16 主页面拼图（4×4，1014×1322）
├── 00_说明数据页总览.png        10 说明/数据页拼图（5×2，1265×700）
├── 00_root.png … 15_about.png   16 个主页面，390×450
├── 20_pend_p2.png … 29_applause_p2.png   10 个说明/数据页，390×450
├── raw/*.rgb565                 26 张原始帧（RGB565 小端，390×450×2 = 351,000 B/张）
├── make_overview.py             总览拼图生成脚本
└── README.md                    本文件
```

单张 PNG 的校验值（md5 前 12 位 / 字节数）：

| 文件 | md5(12) | 字节 |
|---|---|---|
| 00_root.png | 7a183b79fd57 | 22485 |
| 01_raw.png | 8c9a808f844b | 16417 |
| 02_pendulum.png | 6141d8ba7512 | 18294 |
| 03_spring.png | 7d7058a53e39 | 12999 |
| 04_centri.png | adbe33c6344d | 16668 |
| 05_incline.png | 3f99ef720c5f | 19449 |
| 06_ruler.png | 23b8ac6584b1 | 16287 |
| 07_spec_accel.png | 65bfe3ad9709 | 16523 |
| 08_spec_mic.png | 67a3fb75ccd2 | 18701 |
| 09_spec_mag.png | 3703c489f629 | 21561 |
| 10_stopwatch.png | cb5b4dbacd66 | 18681 |
| 11_lightgate.png | 2c591b881c03 | 17040 |
| 12_acousticgate.png | 3b8af8eef028 | 17400 |
| 13_applause.png | 5d401011bce7 | 14085 |
| 14_settings.png | 02390aa83490 | 8789 |
| 15_about.png | cec87e23d332 | 10292 |
| 20_pend_p2.png | d07d4d5e420a | 14447 |
| 21_spring_p2.png | 1ab8df51e710 | 17407 |
| 22_centri_p2.png | 8140fec68473 | 17105 |
| 23_incline_p2.png | d95896f8b897 | 15552 |
| 24_ruler_p2.png | fb877889f9f0 | 16109 |
| 25_spec_p2.png | c043420f7094 | 13915 |
| 26_stopwatch_p2.png | 6456af281f50 | 20317 |
| 27_lightgate_p2.png | 0601d1d6ac58 | 18675 |
| 28_acousticgate_p2.png | 6926456844b6 | 17903 |
| 29_applause_p2.png | 63ae82b817b7 | 14078 |

采集日志（宿主机原文，含逐页 `hash=` 与校验通过记录）：
`/tmp/pwshot_all3.log`，关键行：

```
[pwshot] retry 1: missing ['06_ruler']
[pwshot] --- 26/26 page(s) captured ---
```

---

## 七、还剩什么没做（如实）

- 这批图**没有覆盖板块二级列表页**（例如"力学"下三个实验的列表页）——
  实验页是直接按名字打开的；列表页文案里同样有 `->` 文案，已随 i18n 修复一起生效，
  但**尚未截图留证**。
- 板端没有做 RLE/压缩，一帧 351 KB 发两遍 ≈ 15 s/页；若要缩短，
  可以在 `pw_shot.c` 里对 RGB565 做 RLE（界面大片纯色，压缩比会很高）。
- 帧头重复只做了"打两遍取第一个能解析的"，没有做"两遍必须一致"的严格多数表决。
