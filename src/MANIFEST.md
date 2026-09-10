# src/ — 源码快照来源清单（MANIFEST）

本目录是把分散在 openvela 公共仓的改动集中快照进参赛仓，供评委直接审阅源码。
每个文件下方注明：来源路径（在 openvela 工作区）、所属仓库分支、最后一次相关提交。

> 说明：`app/phywear/`（参赛仓内原创 LVGL 应用）不在本目录，单独位于仓库根 `app/`。
> 本目录收**公共仓改动**（nuttx / vendor_sifli / vendor_openvela / lvgl）的源码快照：
> 其中 **nuttx 传感器驱动、麦克风驱动、触控修复** 为**本队原创**；
> **EPIC 硬件加速（vendor_sifli #31 / lvgl #41）为官方 PR 提供、非本队原创**（下文逐项标注）。

---

## nuttx/ — 新传感器驱动（自制） `branch: phywear-dev`

| 文件 | 说明 | 关键 commit |
|---|---|---|
| `drivers/sensors/mmc5603.c` | 新增 MMC5603 地磁驱动（含 auto-SR 偏置修复） | `a46637d110`(新增) / `b6a3c96fce`(修复) |
| `drivers/sensors/ltr303.c` | 新增 LTR-303 环境光驱动 | `a46637d110` |
| `drivers/sensors/lsm6dsl.c` | IMU 驱动（适配/SPDX） | `75a2b5a5f7` |
| `include/nuttx/sensors/mmc5603.h` | MMC5603 头文件（新增） | `a46637d110` / `b6a3c96fce` |
| `include/nuttx/sensors/ltr303.h` | LTR-303 头文件（新增） | `a46637d110` |
| `include/nuttx/sensors/lsm6dsl.h` | IMU 头文件（适配） | `75a2b5a5f7` |

## vendor/sifli/ — 黄山派 BSP + SF32LB52 芯片层 `branch: official-with-pr31`

> **归属总则**：本仓 `src/` 中 vendor_sifli 的 **EPIC 硬件加速（LCD/FB 驱动、HAL、DMA 拷贝）**
> 来自**官方 PR #31**（非本队原创）；**本队原创**为：3 个传感器驱动的板级 bringup/注册、麦克风
> 驱动、触控修复、PhyWear defconfig 使能。下表逐项标注。

### board BSP：lckfb_huangshan_pi（黄山派板级适配）
| 文件 | 说明 / 归属 |
|---|---|
| `boards/sf32lb52/lckfb_huangshan_pi/configs/nsh/defconfig` | 黄山派 NSH 板级配置（官方基线 + 本队使能） |
| `boards/sf32lb52/lckfb_huangshan_pi/include/board.h` 等 include | 板级引脚/板定义（官方基线） |
| `boards/sf32lb52/lckfb_huangshan_pi/scripts/ld.script` + `Make.defs` | 链接脚本（XIP Flash @0x12010000，官方基线） |
| `boards/sf32lb52/lckfb_huangshan_pi/src/bsp_*.c` + `sifli_*.c` | BSP 初始化 / LCD+TP / pinmux / 电源 / GPIO / 按键 / sifli_ap |
| `boards/sf32lb52/lckfb_huangshan_pi/src/etc/` | 板载数据/字体/init.d（含演示 app 数据，官方基线） |

### 驱动：麦克风（**本队原创**）
| 文件 | 关键 commit |
|---|---|
| `boards/sf32lb52/drivers/audio/sf32lb52_mic.c` | `56197b1`(本队新增) / `acf759c`(本队 re-arm/DC offset 修复) |
| `boards/sf32lb52/drivers/audio/sf32lb52_mic.h` | 同上 |

### 芯片层：SF32LB52 EPIC / 启动 / 内存 / Flash
> ⚠️ **归属**：本节 EPIC 相关文件由**官方 PR #31（vendor_sifli，作者 yunlonguu 等）提供**，
> **非本队原创**；本队为集成而纳入。`sf32lb52_lchspi_ulp` defconfig 的 PhyWear 使能项为本队添加。

| 文件 | 关键 commit / 归属 |
|---|---|
| `boards/sf32lb52/sf32lb52_lchspi_ulp/configs/nsh/defconfig` | **本队**：真机 EPIC 使能项（`CONFIG_BSP_USING_EPIC=y` + `CONFIG_LV_USE_SIFLI_EPIC=y` + `CONFIG_EXAMPLES_PHYWEAR=y`） |
| `chips/sf32lb52/sf32lb52_epic.c` | **官方 PR #31**（EPIC LCD/FB + 硬件加速），非本队原创 |
| `chips/sf32lb52/include/sf32lb52_epic.h` | **官方 PR #31** |
| `chips/drivers/hal/bf0_hal_epic.c` | **官方 PR #31**（EPIC HAL） |
| `chips/drivers/Include/bf0_hal_epic.h` | **官方 PR #31** |
| `chips/sf32lb52/sifli_start.c` | 官方基线/适配 |
| `chips/sf32lb52/sifli_allocateheap.c` | 官方基线/适配 |
| `chips/sf32lb52/sf32lb_flash.c` | 官方基线/适配 |
| `chips/sf32lb52/sfconfig.h` | 配置 |
| `chips/sf32lb52/CMakeLists.txt` + `Kconfig` | 芯片层构建/配置 |

## vendor/openvela/ — 模拟器板级配置 `branch: phywear/emulator-goldfish-phywear`
| 文件 | 说明 |
|---|---|
| `boards/vela/configs/goldfish-arm64-v8a-ap-phywear/defconfig` | **goldfish-phywear 模拟器配置**（本队自建，无实物验收用） |

## lvgl/ — EPIC 硬件加速后端 `branch: official-with-pr41`
> ⚠️ **归属**：本节 EPIC 后端由**官方 PR #41（apps_graphics_lvgl，作者 yunlonguu 等）提供**，
> **非本队原创**；本队为集成而纳入，并补一处构建修复（`lv_os_private.h`）。

| 文件 | 关键 commit / 归属 |
|---|---|
| `src/draw/sifli/epic/lv_draw_sifli_epic*.c` / `.h` | LVGL SiFli EPIC 绘制后端 — **官方 PR #41** |
| `src/draw/sifli/epic/lv_sifli_epic_*.c` / `.h` | EPIC 配置/OSA/工具 — **官方 PR #41** |
| `src/osal/lv_os_private.h` | OS 抽象 — **本队构建修复**（`07f608bd3`，PR #41 构建所需） |
| `src/lv_conf_internal.h` + `src/lv_init.c` | LVGL 配置/初始化（启用 EPIC）— **官方 PR #41** |

---

## 合规说明（重要）

- **app/phywear/ 是参赛仓内原创**，属于本队作品，评委 clone 主分支即可审阅。
- **归属如实**：本仓 `src/` 中的 **EPIC 硬件加速**（vendor_sifli `sf32lb52_epic.c`/HAL、
  lvgl `src/draw/sifli/epic/`）来自**官方 PR #31 / #41**（作者 yunlonguu 等），**非本队原创**，
  本队为集成而纳入；`src/nuttx/drivers/sensors/{mmc5603,ltr303}.c`、麦克风驱动、触控修复、
  defconfig 使能项为**本队原创**。**绝无将官方成果称为"本队自研"之意。**
- 本队原创的 nuttx 传感器驱动已提 **nuttx PR #378**；官方 EPIC PR（#31/#41/#121）此前长期无人
  review，等待官方合入后评委 `repo sync` 即可编译并复现 EPIC 加速。
- 全部源码遵循 Apache-2.0（openvela 基线协议）。
