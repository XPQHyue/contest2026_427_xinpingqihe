# PhyWear 硬件适配与性能优化（比赛加分项归档）

> 关联：GUI 图表规范见 `phywear_ui.md` §7；phyphox 源码全分析见 `phyphox_source_analysis.md`。

> 更新：2026-09-03。本文件汇总在 SF32LB52（黄山派）上做的**平台适配**与
> **性能优化**成果，作为「新硬件平台适配 + 深度优化」评分项的依据。
> 对应代码：`openvela/apps/examples/phywear/` + `openvela/nuttx/drivers/sensors/` +
> `openvela/vendor/sifli/`（defconfig）。

## 1. GPU / 渲染适配与性能优化（核心加分项）

### 1.1 摸清 EPIC 加速边界
- 已合入的 LVGL SiFli EPIC draw unit 只实现 `FILL / BORDER / IMAGE / LABEL / LAYER`
  五类任务，**不含 LINE**；SiFli EPIC 硬件 HAL（`bf0_hal_epic.h`）也只有
  Blend/Rotate/Fill/Copy/FillGrad，**无画线原语**。
- 结论：`lv_chart` 折线 100% 落回 LVGL 软件渲染器，是性能黑洞。

### 1.2 性能瓶颈实测（自建 `phywear pendbench` 回归工具，全自动复现）
| 场景 | 结果 |
|---|---|
| 空闲主菜单（LVGL 100Hz 主循环） | 168 loops/2s，空闲堆 7.7MB 恒定 |
| lv_chart 300 点波形（SHIFT 25/10/5Hz、批量 4Hz 全部试过） | **崩到 2 loops/2s（每帧 ~1s）** |
| lv_chart 96 点 + 240ms 批量刷新 | ~85–100 loops/2s（勉强可用，仅 ~4fps） |
| 自研 pw_scope 300 点 @25fps | ~90 loops/2s，**丝滑** |
| 自研 pw_scope 自相关 251 点 @10Hz | ~160 loops/2s |

### 1.3 解决方案：CPU 光栅 + EPIC 硬件 blit
- `pw_scope`（实时滚动曲线）与 `pw_graph`（可缩放/平移图）：
  CPU 把曲线画进一小块 RGB565 缓冲 → 挂 `lv_image` → 交 EPIC `IMAGE` 任务硬件
  blit（43fps 快速路）。缩放/平移只改视图窗口重画，成本与静止帧相当。
- 收益：把"300 点图表不可用"变成"300 点 @25fps + 251 点 @10Hz 同时稳定"，
  且缩放/平移/散点/坐标轴全部流畅。

### 1.4 单点触摸的交互替代
- 本板 FT6146 触控 `TSIOC_GETMAXPOINTS = 1`，**无法双指捏合**。
- 替代方案（phyphox graph 体验的手表版）：**图上拖动 = 平移 X/Y**；
  图下 `X- X+ Y- Y+` 独立缩放、`Fit` 全览、`Auto` 跟随数据；缩放后状态行回显
  当前窗口 `Range X .. .. Y .. ..`。
- 手势不冲突的关键：图对象声明 `SCROLLABLE + scroll_dir ALL + 关滚动链`，
  使图内拖动被图"认领"，不触发外层横滑翻页。

## 2. 传感器驱动适配（新硬件平台适配项）

| 驱动 | 设备节点 | 说明 |
|---|---|---|
| MMC5603NJ 地磁 | `/dev/mag0` | 字符设备 read()；20bit counts ×0.0625 = mG |
| LTR-303ALS 环境光 | `/dev/light0` | CH0=可见+红外、CH1=红外；lux=(CH0−CH1)×0.6 |
| 模拟 MEMS 麦克风 | `/dev/mic0` | 片内 AUDCODEC 单端 ADC + DMA，16kHz/16bit/1024 样本 |
| LSM6DS3TR-C IMU | `/dev/lsm6dsl0` | ioctl；单位核实为 acc mg / gyro mdps |

- MMC5603 偏置修复：连续模式开启 `CTRL0.AUTO_SR(0x20)`（对照 Zephyr 主线），
  静止模长从 ~0.9–1.2 G（轴号不随翻转变，偏置主导）降到稳定 ~0.62 G，
  转表后三轴均随翻转正常变号。
- 寄存器语义按数据手册/Zephyr 校准：0x08=SET、0x10=RESET、CTRL0 0x80=CMM_FREQ、
  CTRL2 0x10=CMM_EN（修复 SDK 样例的错误命名）。

## 3. 算法适配（phyphox 同款、M33 单精度 FPU）
- `pw_analysis`：radix-2 FFT + 自相关周期检测（首峰粗估 + 末谐波峰精化）。
- Pendulum 摆测 g：50Hz 陀螺三轴和 → 自相关 → g=4π²L/T²；实板 12cm 摆误差 1.8%。
- 宽容度：自相关显著门 0.12 + FFT 主频兜底 + g∈[0.5,25] 物理门（静态拒检不误报）。

## 4. 踩坑与经验（可复现价值）
1. **图表性能**：见 §1，实时曲线禁用 lv_chart，用 pw_scope/pw_graph，新图先 bench。
2. **驱动体积敏感（未解）**：向 `mmc5603.c` 新增 ioctl+函数会使固件启动卡死在
   ROM 阶段（SFBL/AB）；纯改立即数（AUTO_SR）正常。改该驱动需"最小变更+上板验证"。
3. **串口**：NSH 行尾须 `\n`（单 `\r` 只回显不执行）；open 串口会经 RTS 触发复位，
   脚本需"脉冲复位→等提示符→再发命令"。
4. **供电**：USB 供电不稳/AMOLED 负载会使启动卡 SFBL/AB，换 5V/2A 或后置口/短粗线。
5. **boot**：整链 >~1.28MB 读回会超时，属 sftool 工具限制，与镜像无关。

## 5. 回归与提交
- 回归：`phywear pendbench [秒 [起始页]]`（合成摆动 + 自动翻页 + 帧率/空闲堆打印）。
- 代码仓库提交（apps）：见 `git log`（关键 commit：`b2820d670` pw_scope、
  `bd54ce453` pw_graph、`334b977bb` 手势收尾）。
