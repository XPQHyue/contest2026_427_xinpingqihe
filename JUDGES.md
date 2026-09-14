# 评委复现指南（PhyWear · contest2026_427_xinpingqihe）

> **先说结论（如实）**：本仓**不能单独编译**。它是一个**作品仓**——放着本队原创的应用代码（`app/phywear/`）、
> 对 openvela 公共仓的**改动快照**（`src/`）、板级配置（`board/`）、文档与证据（`docs/`）、AI 编码日志（`logs/`）。
> 要编译运行，需要先有一个 **openvela 基线工作区**（多仓检出），再把本仓的内容落到工作区里。
> **本页给出三条路径**，按"想花多少时间"挑一条即可；完整细节见 [`docs/07_构建烧录与复现指南.md`](docs/07_构建烧录与复现指南.md)。

---

## 路径 A：只看代码与文档（约 5 分钟，不用编译）

| 先看 | 为什么 |
|---|---|
| [`docs/01_项目描述_如实版.md`](docs/01_项目描述_如实版.md) | 逐条"有就是有、没有就是没有"的事实清单（含 **未实现项**） |
| [`app/phywear/`](app/phywear) | **本队原创**作品源码（LVGL 手表应用，约 1.4 万行 C） |
| [`src/MANIFEST.md`](src/MANIFEST.md) | 公共仓改动**逐文件来源 + 归属**（哪些是官方 PR、哪些是本队原创） |
| [`docs/03_技术报告.md`](docs/03_技术报告.md) | 架构 + **10 个关键问题的"现象→定位→修复→验证"** |
| [`docs/evidence/`](docs/evidence) | 真机截图、声学/技能/主动场景/LLM 联调证据 |
| [`logs/XPQHyue/`](logs/XPQHyue) | AI Coding 日志（官方格式，`validate-log.py` 校验通过） |

---

## 路径 B：真机复现（推荐；有板子，约 1–2 小时）

**前置**：一块立创·黄山派（SF32LB52-MOD-1-N16R8）、CH340N USB 串口线、Linux 主机。

```bash
# ① 取 openvela 基线工作区（官方 manifest，分支 dev-ai-contest-2026）
mkdir -p ~/openvela && cd ~/openvela
repo init -u https://gitee.com/open-vela/manifests.git -b dev-ai-contest-2026 -m openvela.xml
#   本队工作区实际用的是 SSH：ssh://git@gitee.com/open-vela/manifests.git（两种都可）
repo sync -c -j8

# ② 把本仓的改动落到工作区（脚本在本仓内，先演练后执行；--all 含官方基线快照）
cd <本仓路径>
python3 .claude/skills/phywear-reproduce/check_progress.py     # 看当前进度（应为大量 ❌，属正常）
python3 .claude/skills/phywear-reproduce/restore_code.py       # 演练：只打印要写哪些文件
python3 .claude/skills/phywear-reproduce/restore_code.py --execute --all
python3 .claude/skills/phywear-reproduce/check_progress.py --brief   # 复检

# ③ 编译真机固件（AI Agent + PhyWear + 声学）
cd ~/openvela
export PATH="$PWD/prebuilts/tools/linux/x86_64:$PWD/prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin:$PWD/vendor/artinchip/tools/scripts:$PATH"
rm -f cmake_out/sf32lb52_lchspi_ulp_nsh_ai/.config
cmake -B cmake_out/sf32lb52_lchspi_ulp_nsh_ai -S nuttx -GNinja \
  -DBOARD_CONFIG=../vendor/sifli/boards/sf32lb52/sf32lb52_lchspi_ulp/configs/nsh-ai \
  -DEXTRA_FLAGS="-Wno-cpp -Wno-deprecated-declarations"
cmake --build cmake_out/sf32lb52_lchspi_ulp_nsh_ai -j16

# ④ 烧录（本仓 board/ 里就有脚本与 ftab，不依赖本队电脑路径）
cd <本仓路径> && ./board/flash_with_ftab.sh ~/openvela/cmake_out/sf32lb52_lchspi_ulp_nsh_ai/nuttx.bin /dev/ttyUSB0

# ⑤ 串口验收
picocom -b 1000000 --noreset --lower-rts --lower-dtr /dev/ttyUSB0
```

串口里输入：

```text
nsh> ai_agent &          # 起 AI Agent（真机无网络，走端侧离线意图 + 工具）
nsh> phywear lang zh     # 起中文界面（GUI 会占住控制台，之后用触摸操作）
```

**5 分钟验收清单**：主菜单 8 个板块有图标 → 进「AI 教练」显示"已就绪" → 点「有哪些实验」出现
`AI: list: 20 screens` → 点「打开单摆」**手表自己跳到单摆页** → 进「声学 → 音频发生器」能出声 →
「原始传感器 → 麦克风」说话柱状图跟着动。

> ⚠️ 串口铁律：`/dev/ttyUSB0` **独占**；`picocom` 必须带 `--noreset --lower-rts --lower-dtr`
> （CH340N 的 RTS 直连 SoC 复位）；**一个 boot 只能跑一个 `phywear` GUI**（第二个会挂在 LCD 驱动里）；
> 打开串口即复位一次，属正常现象。**烧录前先关掉 picocom**，否则 sftool 抢不到串口。

---

## 路径 C：模拟器跑起来（没有板子也能看界面；约 30 分钟）

同路径 B 的 ①②，然后把目标换成模拟器配置：

```bash
cd ~/openvela
cmake -B cmake_out/vela_goldfish-arm64-v8a-ap-phywear -S nuttx -GNinja \
  -DBOARD_CONFIG=../vendor/openvela/boards/vela/configs/goldfish-arm64-v8a-ap-phywear/ \
  -DEXTRA_FLAGS="-Wno-error -Wno-cpp -Wno-deprecated-declarations"
cmake --build cmake_out/vela_goldfish-arm64-v8a-ap-phywear -j16
./emulator.sh cmake_out/vela_goldfish-arm64-v8a-ap-phywear          # 需要图形界面时；也可加 -no-window
```

模拟器里 `phywear cap root|raw|incline|tone|mic|ai ...` 可逐页打开。**模拟器 IMU 是合成波形，不可当测量结果。**

---

## 已知限制与"做不到"的地方（如实）

1. **本仓不能单独编译**：改动分散在 `apps / nuttx / vendor_sifli / vendor_openvela / packages_ai_agent`
   五个子仓里，必须叠在 openvela 基线上（这正是路径 B 第 ② 步的作用）。
2. **团队 manifest 的局限**：本仓根目录的 `contest2026_427_xinpingqihe.xml` 只把 `app/phywear`
   链接进工作区，**不含** `src/**` 的公共仓改动 —— 所以仍要跑第 ② 步的恢复脚本（它已覆盖全部 169 个文件）。
3. **真机没有网络栈**：真机上 LLM 对话不可用，走端侧离线意图 + 工具；**端云 LLM 对话在模拟器演示**
   （小米 MiMo Token Plan；实测记录见 `docs/evidence/llm-20260913/`）。
4. **"主动+执行"场景默认关闭**（`PW_WATCH_PROACTIVE 0`）：代码保留、只记录事件日志，原因与后续方向见
   `docs/05_AI_Agent与Skill.md` §5。
5. **EPIC 硬件加速来自官方 PR**（vendor_sifli #31 / lvgl #41 / nuttx-apps #121），**非本队原创**；
   本队做的是集成 + UI/算法层优化 + 驱动/应用。
6. **模拟器数据是合成的**；模拟器帧率（22–23）与真机（约 41）不可混用。
7. 若某步在您的环境失败，请把 `check_progress.py` 的输出发给我们（它会指出缺哪一项）。
