---
name: phywear-reproduce
description: 从零环境把 PhyWear（立创·黄山派 SF32LB52 真机手表物理实验室）复现到当前代码状态，并检测本机已经做到哪一步。Use when the user wants to rebuild/reproduce the PhyWear dev environment on this or another machine, restore the code snapshot into the openvela workspace, check what is already done here, or find out the next step. Triggered by phrases like "复现 PhyWear", "换电脑怎么搭", "重建环境", "现在做到哪一步了", "环境自检", "恢复快照", "restore snapshot", "check progress", "从0到能烧录".
---

# PhyWear 复现 SKILL（从 0 环境 → 当前代码状态，带进度检测）

面向「换一台电脑、或时隔很久回到这个项目」的场景：**先测出这台机器做到哪一步了，再只补缺的那几步。**
参赛仓 `~/work/contest2026_427_xinpingqihe` 里的快照是**权威代码状态**；
本 SKILL 负责把它落位到 openvela 工作区 `~/openvela`，并核对每一项关键修复是否真的接线。

> 详细原理、完整命令与踩坑记录见 `docs/07_构建烧录与复现指南.md`；本 SKILL 是它的**可执行 + 可自检**版本。
> 复现结论的**如实口径**见第 8 节，复现时不要改写成更漂亮的说法。

## 0. 一条命令看进度（每次先跑这个）

```bash
cd ~/work/contest2026_427_xinpingqihe
python3 .claude/skills/phywear-reproduce/check_progress.py           # 完整报告
python3 .claude/skills/phywear-reproduce/check_progress.py --brief   # 只看需要处理的
python3 .claude/skills/phywear-reproduce/check_progress.py --phase P3
python3 .claude/skills/phywear-reproduce/check_progress.py --quick   # 日常快检：只跑 P2 代码 + P4 构建
python3 .claude/skills/phywear-reproduce/check_progress.py --json    # 给 Agent/CI 用
```

脚本可以任意 cwd 运行（用 `--workspace` / `--repo` 指定两端根目录；默认就是你本机的两个路径）。

报告分 8 个阶段，末尾给出「下一步」：

| 阶段 | 含义 | 全绿的标准 |
|---|---|---|
| P0 主机环境 | cmake/ninja/python3/pyserial/sftool/picocom/arm-none-eabi + prebuilts 小工具 | 11/11 |
| P1 工作区 | `~/openvela` 的 `.repo`、nuttx、apps、vendor/sifli、packages/ai_agent、tools/phywear | 8/8 |
| P2 代码状态 | 参赛仓快照 ↔ 工作区逐文件 md5（关键 92 个文件必须一致） | 13/13 |
| P3 关键修复标记 | 语义检查（不是"文件在不在"，而是"修复有没有接线"） | 9/9 |
| P4 构建产物 | `nuttx.bin` 是否存在、是否比源码新 | 5/5 |
| P5 烧录链路 | `/dev/ttyUSB0` 是否在、是否被占用、`flash_with_ftab.sh` | 4/4 |
| P6 验证工具 | boardsh/pwshot/sim_console/证据脚本 + 本 SKILL 是否已挂进工作区 | 3/3 |
| P7 交付与日志 | docs/01–08、docs/evidence、logs/、采集器 | 5/5 |

状态图例：`✅ OK` / `⚠️ 需要动手` / `❌ 缺失（会挡住下一步）` / `ℹ️ 仅提示` / `➖ 跳过`。
判断「做到哪一步」：**从 P0 往上，第一个非 ✅ 的阶段就是当前卡点**。

## 1. P0 主机环境

```bash
sudo apt install -y cmake ninja-build python3 python3-pip picocom        # 基础
python3 -m pip install pyserial                                          # 串口脚本依赖
# sftool（SiFli 烧录工具）需按官方说明安装；本机在 /usr/local/bin/sftool
# 真机工具链：arm-none-eabi-gcc（apt: gcc-arm-none-eabi）；模拟器用 prebuilts 里的 aarch64-none-elf
export PATH="$HOME/openvela/prebuilts/tools/linux/x86_64:\
$HOME/openvela/prebuilts/gcc/linux/x86_64/aarch64-none-elf/bin:\
$HOME/openvela/vendor/artinchip/tools/scripts:$PATH"
```

- `mcopy` / `mformat` / `aidl` / `genromfs` 缺一个，模拟器构建会直接失败（真机构建不需要）。
- 联网步骤（repo sync / git push / apt）需要代理；本机 `127.0.0.1:7897` 曾是 Clash 端口，
  代理不稳时专属仓可直连并单独指定解析 IP：
  `git -C ~/work/contest2026_427_xinpingqihe config http.curloptResolve github.com:443:<可用IP>`。

## 2. P1 openvela 工作区

```bash
# 官方流程（需要网络，耗时较长）
repo init -u <openvela manifest> -b <分支> && repo sync -c -j8
```

工作区是**多仓检出**：改动分散在 `nuttx/`、`apps/`、`vendor/sifli/`、`vendor/openvela/`、`packages/ai_agent/`。
`check_progress.py` 的 P1 会逐个核对这五处是否都在（缺 `packages/ai_agent` 时 Agent 相关功能无法编译）。

## 3. P2 代码落位：把参赛仓快照恢复进工作区（本 SKILL 的核心）

```bash
cd ~/work/contest2026_427_xinpingqihe
# ① 快照本身是否被人改过（CI 式自检）
python3 .claude/skills/phywear-reproduce/gen_manifest.py --check
# ② 先演练：只打印「要新增/要覆盖哪些文件」，不落盘
python3 .claude/skills/phywear-reproduce/restore_code.py
# ③ 确认后真恢复：自动把被覆盖的旧文件备份到 ~/openvela/backup/reproduce-<时间戳>/
python3 .claude/skills/phywear-reproduce/restore_code.py --execute
# ④ 复检
python3 .claude/skills/phywear-reproduce/check_progress.py --brief
```

映射关系（`manifest.json` 里是唯一权威，共 168 个文件，其中 **92 个关键**）：

| 参赛仓快照 | openvela 工作区 | 性质 |
|---|---|---|
| `app/phywear/`（55 个文件） | `apps/examples/phywear/` | **本队原创**（LVGL 手表端） |
| `tools/` | `tools/phywear/` | **本队原创**（真机自检/截图/证据脚本） |
| `src/packages/ai_agent/` | `packages/ai_agent/` | 本队新增 4 个 phywear 工具 + 关键词意图 |
| `src/nuttx/`（6 个） | `nuttx/` | **本队原创**传感器驱动（MMC5603 / LTR-303） |
| `src/vendor/sifli/boards/sf32lb52/drivers/audio/` | 同路径 | **本队原创**麦克风/扬声器驱动 |
| `src/vendor/sifli/boards/sf32lb52/sf32lb52_lchspi_ulp/src/` | 同路径 | 启动时序等待、`/dev/spk0` 注册 |
| `src/vendor/sifli/chips/sf32lb52/sifli_allocateheap.c` | 同路径 | 1.8V LDO 等待修复 |
| `src/lvgl/src/osal/lv_os_private.h` | `apps/graphics/lvgl/lvgl/src/osal/` | 本队构建修复（官方 PR #41 需要） |
| `board/*.defconfig`（3 份） | `configs/nsh-ai/defconfig`、`configs/nsh/defconfig`、goldfish 配置 | 板级使能项 |
| `src/lvgl/`、`src/vendor/` 其余 | 同路径 | **官方基线/官方 PR 参考快照**（`--all` 才恢复） |

- `restore_code.py` **只覆盖、不删除**；默认只碰关键文件，官方基线要显式 `--all`。
- 官方基线快照（`vendor/sifli` 板级、EPIC 等）在新机器上通常比快照**更新**，默认不覆盖是对的。
- 快照里 6 个 nuttx 驱动的中文注释与工作区英文注释不同，脚本按「去注释后代码等价」判定，不算差异。

## 4. P3 关键修复标记：为什么不能只看文件在不在

这些修复都曾真实导致过故障；**文件一致 ≠ 功能正常**，所以单独做语义检查：

| 检查 | 缺失时的现象 | 修复位置 |
|---|---|---|
| `HAL_Delay_us(2000)` × 3 | 启动只打印 `AB`/`ABCD` 后卡死（晶振/DLL 建立时序） | `bsp_init.c` + `sifli_allocateheap.c` |
| `MIC_VOLUME 30` | 麦克风峰值只有 15–40 LSB，频谱几乎是平的 | `sf32lb52_mic.c` |
| 扬声器驱动含 `SPK_FADE_*` | 开 PA / 改频率时"哒"的爆音 | `sf32lb52_spk.c` |
| `/dev/spk0` 注册 | `phywear tone` 报设备不存在 | `sifli_ap.c` |
| defconfig 4 项使能 | 页面/Agent/音频功能直接消失 | `configs/nsh-ai/defconfig` |
| i18n EN/ZH 各 ≥200 条且等长 | 中文界面缺字/串行错位 | `phywear_i18n_tables.inc` |
| Skill blob 内嵌 + 正本 3.7 KB | 真机 `/data/agent/skills/` 里没有 Skill | `pw_skill_blob.c` + `skills/*.md` |
| `PW_WATCH_PROACTIVE` = 0 | 主动场景默认开启（**口径错误**，见第 8 节） | `pw_watch.c` |

## 5. P4/P5 构建与烧录

```bash
cd ~/openvela
# 真机（唯一验收目标）：首次或改过 defconfig 后先删 .config
rm -f cmake_out/sf32lb52_lchspi_ulp_nsh_ai/.config
cmake -B cmake_out/sf32lb52_lchspi_ulp_nsh_ai -S nuttx -GNinja \
  -DBOARD_CONFIG=../vendor/sifli/boards/sf32lb52/sf32lb52_lchspi_ulp/configs/nsh-ai \
  -DEXTRA_FLAGS="-Wno-cpp -Wno-deprecated-declarations"
cmake --build cmake_out/sf32lb52_lchspi_ulp_nsh_ai -j16     # 产物 nuttx.bin ≈ 2.02 MB

./flash_with_ftab.sh cmake_out/sf32lb52_lchspi_ulp_nsh_ai/nuttx.bin /dev/ttyUSB0
```

铁律（违反会"看起来像坏了"）：**先 ftab 后固件**；**禁止 `erase_flash`**；
串口 `1000000 8N1`，`pyserial` 打开后立刻 `dtr=False; rts=False`，
`picocom` 必须 `--noreset --lower-rts --lower-dtr`；`/dev/ttyUSB0` **独占**；
一个 boot 只跑一个 GUI 实例（第二个 `phywear` 会挂在 LCD 驱动里，先复位）。
启动判据：`SFBL → ABCD → ADC calibration → NuttShell (NSH)`。

## 6. P6/P7 验收与交付

```bash
# 真机自检（逐条，详见 docs/07 §5）
python3 ~/openvela/tools/phywear/boardsh.py --run "phywear tone 440 2000 12" --wait 4
python3 ~/openvela/tools/phywear/boardsh.py --run "phywear micread 10"      --wait 4
python3 ~/openvela/tools/phywear/pwshot.py shot tone        # 整帧截图（base64 over UART）
python3 ~/openvela/tools/phywear/pwshot.py sweep            # 16 个主页面
# AI 日志（评审要求）：采集器只在含 .repo/ 的工作区内生效
bash ~/openvela/.claude/skills/contest-log-collector/onboarding/verify-setup.sh
```

把本 SKILL 挂进工作区，方便在 `~/openvela` 里直接调用（也让 AI 会话被采集器记录）：

```bash
bash ~/work/contest2026_427_xinpingqihe/.claude/skills/phywear-reproduce/install_into_workspace.sh
# 之后在 ~/openvela 下：python3 .claude/skills/phywear-reproduce/check_progress.py
```

挂载方式默认是**软链**（`~/openvela/.claude/skills/phywear-reproduce -> 参赛仓`），
所以 `check_progress.py --brief` 里 P6-skillinst 显示为 ✅ 即表示已挂好；
`repo sync` 重置工作区后重跑一次该脚本即可。

## 7. 复现验收判据（全绿才算复现成功）

1. `check_progress.py` 的 P0–P7 全 ✅；
2. `restore_code.py --all` 演练输出「全部一致」；
3. 真机能启动到 NSH，`phywear lang zh` 出中文界面，`phywear tone 440 2000 12` 出声；
4. `phywear micread 10` 说话时峰值上千 LSB；
5. 心跳 `[phywear] alive t=…s loops/s=…` 连续出现（≥5 分钟不重启）；
6. 真机启动日志里出现 Skill 安装行 `[phywear] installed skill …`。

## 8. 如实口径（复现时不许"美化"）

- 模拟器 IMU 是**合成波形**，只能演示链路，**不得当测量证据**；模拟器 ≈22–23 FPS ≠ 真机 ≈41 FPS。
- **EPIC 硬件加速来自官方 PR #31 / #41 / #121，非本队原创**；phyphox 是灵感来源，代码是独立 C 重写。
- **主动场景默认关闭**（`PW_WATCH_PROACTIVE 0`），只记录事件日志，不能说成"默认开启"。
- 扬声器响度受**板载喇叭物理上限**限制（PA 开关/音量拉到 100% 只差 1–3 dB）。
- 真机**没有网络栈**，LLM 端云演示只在模拟器上做；真机走离线关键词意图直通。
- 90 Hz 测到 180 Hz 是弱低频 + 同板声耦合导致的二次谐波，不是坐标轴刻度错误。

## 9. 本 SKILL 的文件

| 文件 | 作用 |
|---|---|
| `SKILL.md` | 本手册 |
| `gen_manifest.py` | 生成/校验 `manifest.json`（快照 ↔ 工作区映射 + md5） |
| `manifest.json` | 状态清单：168 个文件（92 关键）的目标路径与 md5 |
| `check_progress.py` | 进度检测（P0–P7 + 下一步建议 + `--json`） |
| `restore_code.py` | 快照 → 工作区恢复（默认演练，`--execute` 落盘并备份） |
| `install_into_workspace.sh` | 把本 SKILL 挂进 `~/openvela/.claude/skills/` |

改动过参赛仓快照后，记得 `python3 gen_manifest.py --write` 重新生成清单，否则 `--check` 会报差异。
