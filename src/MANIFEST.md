# src/ — 源码快照来源清单（MANIFEST）

本目录是把分散在 openvela 公共仓的**本队技术改动**集中快照进参赛仓，供评委直接审阅源码。
每个文件下方注明：来源路径（在 openvela 工作区）、所属仓库分支、最后一次相关提交。

> 说明：`app/phywear/`（参赛仓内原创 LVGL 应用）不在本目录，单独位于仓库根 `app/`。
> 本目录只收**公共仓改动**（nuttx / vendor_sifli / vendor_openvela / lvgl）的源码快照。

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

### board BSP：lckfb_huangshan_pi（黄山派板级适配）
| 文件 | 说明 |
|---|---|
| `boards/sf32lb52/lckfb_huangshan_pi/configs/nsh/defconfig` | 黄山派 NSH 板级配置 |
| `boards/sf32lb52/lckfb_huangshan_pi/include/board.h` 等 include | 板级引脚/板定义 |
| `boards/sf32lb52/lckfb_huangshan_pi/scripts/ld.script` + `Make.defs` | 链接脚本（XIP Flash @0x12010000） |
| `boards/sf32lb52/lckfb_huangshan_pi/src/bsp_*.c` + `sifli_*.c` | BSP 初始化 / LCD+TP / pinmux / 电源 / GPIO / 按键 / sifli_ap |
| `boards/sf32lb52/lckfb_huangshan_pi/src/etc/` | 板载数据/字体/init.d（含演示 app 数据） |

### 驱动：麦克风
| 文件 | 关键 commit |
|---|---|
| `boards/sf32lb52/drivers/audio/sf32lb52_mic.c` | `56197b1`(新增) / `acf759c`(re-arm/DC offset 修复) |
| `boards/sf32lb52/drivers/audio/sf32lb52_mic.h` | 同上 |

### 芯片层：SF32LB52 EPIC / 启动 / 内存 / Flash
| 文件 | 关键 commit |
|---|---|
| `boards/sf32lb52/sf32lb52_lchspi_ulp/configs/nsh/defconfig` | **真机 EPIC 使能配置**：`CONFIG_BSP_USING_EPIC=y` + `CONFIG_LV_USE_SIFLI_EPIC=y` + `CONFIG_EXAMPLES_PHYWEAR=y`（43 FPS 的构建配置） |
| `chips/sf32lb52/sf32lb52_epic.c` | `1814868`（官方 EPIC LCD/FB + 硬件加速 PR #31） |
| `chips/sf32lb52/include/sf32lb52_epic.h` | 同上 |
| `chips/drivers/hal/bf0_hal_epic.c` | EPIC HAL（适配） |
| `chips/drivers/Include/bf0_hal_epic.h` | 同上 |
| `chips/sf32lb52/sifli_start.c` | 启动（适配） |
| `chips/sf32lb52/sifli_allocateheap.c` | 堆分配（适配） |
| `chips/sf32lb52/sf32lb_flash.c` | Flash 驱动（适配） |
| `chips/sf32lb52/sfconfig.h` | 配置 |
| `chips/sf32lb52/CMakeLists.txt` + `Kconfig` | 芯片层构建/配置 |

## vendor/openvela/ — 模拟器板级配置 `branch: phywear/emulator-goldfish-phywear`
| 文件 | 说明 |
|---|---|
| `boards/vela/configs/goldfish-arm64-v8a-ap-phywear/defconfig` | **goldfish-phywear 模拟器配置**（本队自建，无实物验收用） |

## lvgl/ — EPIC 硬件加速后端 `branch: official-with-pr41`
| 文件 | 关键 commit |
|---|---|
| `src/draw/sifli/epic/lv_draw_sifli_epic*.c` / `.h` | LVGL SiFli EPIC 绘制后端（硬件加速） | `d829a0e89`（EPIC 加速 PR #41） |
| `src/draw/sifli/epic/lv_sifli_epic_*.c` / `.h` | EPIC 配置/OSA/工具 | 同上 |
| `src/osal/lv_os_private.h` | OS 抽象（PR #41 构建修复） | `07f608bd3` |
| `src/lv_conf_internal.h` + `src/lv_init.c` | LVGL 配置/初始化（启用 EPIC） | `d829a0e89` |

---

## 合规说明（重要）

- **app/phywear/ 是参赛仓内原创**，属于本队作品，评委 clone 主分支即可审阅。
- 上述公共仓改动（nuttx / vendor_sifli / lvgl）**本应**以 PR 提交到对应 openvela 仓库的
  `dev-ai-contest-2026` 分支（官方模型）。本仓以「源码快照 + 此 MANIFEST」方式纳入，确保
  **评委 clone 本仓即可看到全部技术源码**，并把各改动对应的 openvela 分支/commit 一并载明，
  便于组委会按官方流程 review 合入上游。
- 全部源码遵循 Apache-2.0（openvela 基线协议）。
