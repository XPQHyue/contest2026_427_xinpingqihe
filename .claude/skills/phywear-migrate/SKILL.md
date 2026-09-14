---
name: phywear-migrate
description: 把 PhyWear（openvela 黄山派手表物理实验室）的开发工作完整迁移到**另一台电脑**上用 Claude(+小米 MiMo) 继续做：新机器环境检测、仓库克隆、代码落位、构建烧录、进度核对、会话日志导出，一步不缺。Use when the user says 换电脑 / 迁移到新电脑 / 新机器继续开发 / 环境重建 / handoff / migrate to another machine / setup on a new laptop, or when Claude starts on a machine where the PhyWear workspace may be missing or stale.
---

# PhyWear 迁移 SKILL（换电脑继续开发 · Claude + 小米 MiMo）

> 面向场景：**换一台电脑**（不是本机重建）。目标是把"当前进度（代码 + 文档 + 证据 + AI 日志 + 下一步规划）"
> 完整搬过去，并在新机器上把环境、代码、固件、验证链路全部跑通。
> **任何一步都不要凭记忆跳过 —— 先检测，再动手。**

## 0. 硬规则（MiMo / 任何模型都必须严格遵循）

| # | 规则 |
|---|---|
| R1 | **先检测后动手**：任何开发动作前先跑 `python3 .claude/skills/phywear-migrate/check_env.py`；改动前后各跑一次 `python3 .claude/skills/phywear-reproduce/check_progress.py` |
| R2 | **数据必须可追溯**：不许编造数字。每条技术声明都要能指到代码/日志/证据文件；测不出来的写"未测" |
| R3 | **模拟器数据≠测量结果**：goldfish 的 IMU 是合成波形、FPS≈22，**不得**当作真机数据引用 |
| R4 | **密钥不入库**：MiMo / GitHub token 只放在本机（如 `~/.config/phywear/mimo.key`，权限 600），**绝不**写进仓库、文档、截图、日志 |
| R5 | **真机串口铁律**：`pyserial` 打开后立刻 `dtr=False; rts=False`；`picocom` 必须 `--noreset --lower-rts --lower-dtr`；`/dev/ttyUSB0` **独占**；**禁止 `erase_flash`**；一个 boot 只跑一个 `phywear` GUI |
| R6 | **改完必须回仓**：工作区（`~/openvela`）里的改动要 rsync 回参赛仓，并重生成清单 `gen_manifest.py --write`，否则换电脑就丢 |
| R7 | **会员身份**：EPIC 硬件加速来自官方 PR #31/#41/#121，**非本队原创**；phyphox 是灵感来源，代码为独立 C 重写（见 `app/phywear/NOTICE.md`） |
| R8 | **如实口径**：主动场景默认关闭（`PW_WATCH_PROACTIVE 0`）；真机无网络栈、LLM 只在模拟器演示；板载喇叭响度是硬件上限 |
| R9 | **每次会话结束必须导出 AI 日志**：跑 `.claude/skills/phywear-migrate/finish_session.sh`（见 §6），否则该时段不计工时 |
| R10 | **只做被要求的范围**：不擅自改官方包（`packages/ai_agent` 等）之外的架构，改动集中、可回滚 |

## 1. 新电脑上的迁移顺序（照抄即可）

```bash
# ① 环境检测（先看这张表，缺什么补什么）
python3 <参赛仓>/.claude/skills/phywear-migrate/check_env.py

# ② openvela 工作区（多仓检出，必须联网）
repo init -u <openvela manifest> -b <分支> && repo sync -c -j8
#    若 `nuttx/arch/arm/src/ipc_queue` 缺失且构建报错，补符号链接：
#    ln -s "$HOME/openvela/vendor/sifli/chips/sf32lb52/ipc_queue" \
#          "$HOME/openvela/nuttx/arch/arm/src/ipc_queue"

# ③ 参赛仓（含全部代码快照/文档/证据/日志/复现清单）
git clone https://github.com/open-vela/contest2026_427_xinpingqihe.git
#    或先 clone 我们自己的 fork、切到 PR 分支（见 §3）

# ④ 代码落位：参赛仓快照 → openvela 工作区（默认演练，--execute 才落盘）
python3 <参赛仓>/.claude/skills/phywear-reproduce/restore_code.py            # 演练
python3 <参赛仓>/.claude/skills/phywear-reproduce/restore_code.py --execute  # 恢复

# ⑤ 复检（P0–P7 应全绿）
python3 <参赛仓>/.claude/skills/phywear-reproduce/check_progress.py --brief

# ⑥ 构建（真机唯一验收目标）
cd ~/openvela && rm -f cmake_out/sf32lb52_lchspi_ulp_nsh_ai/.config
cmake -B cmake_out/sf32lb52_lchspi_ulp_nsh_ai -S nuttx -GNinja \
  -DBOARD_CONFIG=../vendor/sifli/boards/sf32lb52/sf32lb52_lchspi_ulp/configs/nsh-ai \
  -DEXTRA_FLAGS="-Wno-cpp -Wno-deprecated-declarations"
cmake --build cmake_out/sf32lb52_lchspi_ulp_nsh_ai -j16

# ⑦ 烧录（ftab 与脚本都在参赛仓 board/ 里，拷到工作区根目录即可）
cp <参赛仓>/board/{ftab_openvela.bin,flash_with_ftab.sh} ~/openvela/
~/openvela/flash_with_ftab.sh cmake_out/sf32lb52_lchspi_ulp_nsh_ai/nuttx.bin /dev/ttyUSB0

# ⑧ 真机自检（docs/07 §5）+ 进度复检 + 收尾日志
```

**联网/代理**：`repo sync`、`git push`、`pip` 需要外网。专属仓若直连 GitHub 不通，可单独指定解析 IP：
`git -C <参赛仓> config http.curloptResolve github.com:443:<可用IP>`（在本机实测 `140.82.116.4`、`20.26.156.215` 可用）。

## 2. 环境检测（`check_env.py` 查什么）

主机工具（cmake/ninja/python3+pyserial/picocom/sftool/node/arm-none-eabi/adb）、openvela 工作区五处子仓、
prebuilts 小工具（aidl/mcopy/mformat/genromfs）、aarch64 交叉工具链、中文字体生成依赖
（`lv_font_conv` + `DroidSansFallbackFull.ttf` + Montserrat TTF）、磁盘余量、串口 `/dev/ttyUSB0`、
参赛仓与 ftab、MiMo key 是否就位。每一项都会给出**修复命令**。

## 3. 进度检测（两件不同的事）

| 检测 | 命令 | 回答的问题 |
|---|---|---|
| 环境 | `check_env.py`（本 SKILL） | 这台新机器**具备干活的条件**了吗 |
| 进度 | `.claude/skills/phywear-reproduce/check_progress.py` | 代码/修复/固件/文档/证据/日志**做到哪一步了**（P0–P7，含 93 个关键文件 md5 比对） |

`check_progress.py` 的 P4 会报固件 md5 与构建时间；`--quick` 只查代码+固件，适合日常。
**权威口径**：`manifest.json`（参赛仓快照 ↔ 工作区 169 个文件映射）—— 它一致，迁移就没丢东西。

## 4. 迁移完整性清单（换电脑必须核对）

| 项 | 在哪 | 说明 |
|---|---|---|
| 应用源码（本队原创） | 参赛仓 `app/phywear/` | 含 5 个字体文件、`NOTICE.md`、`skills/` |
| 公共仓改动快照 | 参赛仓 `src/`（nuttx/vendor_sifli/vendor_openvela/lvgl/ai_agent） | 官方基线参考文件也在内 |
| 板级配置 | 参赛仓 `board/*.defconfig` | 真机 nsh-ai / nsh(EPIC) / 模拟器 goldfish |
| **ftab + 烧录脚本** | 参赛仓 `board/ftab_openvela.bin`、`board/flash_with_ftab.sh` | **没有 ftab 板子只会打印 `SFBL`** |
| 主机侧脚本 | 参赛仓 `tools/` → 工作区 `tools/phywear/` | 截图/串口/证据脚本 |
| 文档与证据 | 参赛仓 `docs/01–08`、`docs/evidence/` | 截图、串口日志、实测记录 |
| AI Coding 日志 | 参赛仓 `logs/XPQHyue/` | 白名单工具（claude-code 等），DSH 不计 |
| 复现/迁移 SKILL | 参赛仓 `.claude/skills/` | 本包 + `phywear-reproduce` |
| **MiMo API Key** | 本机 `~/.config/phywear/mimo.key`（600） | **不在仓库**，需用私密渠道传；新机器重新 `set_llm`（见 §5） |
| 不迁移 | `cmake_out/`、`.config`、`/data` 镜像 | 可重建，不要拷 |

## 5. 小米 MiMo 配置（端云对话，仅模拟器有意义）

```bash
# 模拟器里（真机没有网络栈，配了也用不了）
nsh> ai_agent &
nsh> sleep 900                      # 挂起 NSH，让输入落到 vela> CLI
vela> set_llm token-plan-cn.xiaomimimo.com mimo-v2.5-pro <KEY>
vela> ask 你好，介绍一下你能做什么物理实验
```
- MiMo 的 `tp-` 开头密钥是 **Token Plan**，必须配 `token-plan-cn.xiaomimimo.com`；用 `api.xiaomimimo.com` 会 401。
- 不要把 Key 露在截图/录屏里；用 `config_show` 展示时是掩码（`tp-c****`）。
- 复盘：中文技能摘要曾因"按字节截断"变成非法 UTF-8 导致云端 400，已修（`docs/03 §6.8`）。

## 6. 会话收尾：必须输出 AI 日志（独立 10 分维度）

```bash
bash <参赛仓>/.claude/skills/phywear-migrate/finish_session.sh
```

它做四件事：① 确认采集器已安装（缺则跑 `install.sh`）；② `export-session.py --today --confirm` 把当天
Claude Code 会话导出到 `logs/XPQHyue/`；③ `validate-log.py` 校验；④ 提交 `logs/`（加 `--push` 才推送）。
**每个工作时段结束都跑一次**；白名单工具只有 claude-code / codex / opencode / kiro，DeepSeek Harness 不计。
