---
name: phywear-sf32lb52-devloop
description: Build, flash, and validate the PhyWear wrist physics-lab app on the 立创·黄山派 (SF32LB52) openvela board, and run the benchmark/FPS-tuning loop. Use when the user wants to compile the PhyWear firmware, flash it to the board or the goldfish emulator, capture the on-device GUI for acceptance, or tune the LVGL/EPIC rendering FPS. Triggered by phrases like "编译phywear", "烧录", "上板", "跑bench", "看帧率", "截图中文界面", "测FPS", "EPIC加速验证", "make/clean phywear".
---

# PhyWear on SF32LB52 — Build / Flash / Validate Loop

Reproducible development loop for the openvela-based PhyWear wrist physics-lab app
(team `contest2026_427_xinpingqihe`, board `立创·黄山派 LCKFB Huangshan Pi`, chip `SiFli SF32LB52`).
Use this skill to build firmware, deploy to the real board or the goldfish emulator,
capture GUI evidence, and tune rendering performance.

## Prerequisites
- openvela multi-repo workspace at `/home/xpqh/openvela` (`.repo/` present).
- Build cross toolchain on PATH (aarch64-none-elf, arm-none-eabi), plus `aide`, `mcopy`, `genromfs`.
- Real board: CH340N USB-UART (`/dev/ttyUSB0`) at **UART1, 1,000,000 baud**, RTS→RST active-low reset.
- Emulator: `./emulator.sh cmake_out/<config>/` launched on a display.

## Step 1 — Clean build the firmware
```bash
cd /home/xpqh/openvela
export PATH="$PWD/prebuilts/build-tools/linux-x86_64/bin:$PWD/prebuilts/gcc/linux-x86_64/aarch64-none-elf/bin:$PWD/prebuilts/gcc/linux-x86_64/arm-none-eabi/bin:$PWD/prebuilts/tools/linux/x86_64:$PWD/prebuilts/tools/bin:$PATH"
# Real board (lchspi_ulp NSH) — pre-created cmake_out dir, else use ./build.sh <board-config>
cmake --build cmake_out/vela_goldfish-arm64-v8a-ap-phywear        # emulator config
# or, for the real board:
# ./build.sh vendor/sifli/boards/sf32lb52/sf32lb52_lchspi_ulp/configs/nsh [-j8]
```
- If the build fails on user sensor drivers with `-Werror`, add
  `-DEXTRA_FLAGS="-Wno-error -Wno-cpp -Wno-deprecated-declarations"` to the cmake configure.

## Step 2 — Flash the real board (SF32LB52)
The board runs a preloaded SFBL ROM-stage bootloader; use the CH340N UART with RTS-hold reset:
```bash
cd /home/xpqh/openvela 2>/dev/null
# 1) hold RTS active (enter download mode)
# 2) use the SiFli/vela flash tool against the built fw (nuttx.bin + System.map)
# 3) after flash, release RTS and set baud 1000000 to the NSH console
```
Consult `phywear-docs/huangshan_pi_readme.md` for the exact board console/flash tool
invocation. Confirm a clean NSH prompt (`nsh>` / `openvela-ap>`) after reset.

## Step 3 — Run the app on the emulator and capture the GUI
The emulator **window is always black** (qemu composites the GPU layer); the real LVGL UI
lives in `/dev/fb0`. Capture via:
```bash
cd /home/xpqh/openvela && DISPLAY=:0 ./emulator.sh cmake_out/vela_goldfish-arm64-v8a-ap-phywear/ &
# wait for NSH prompt, then drive the app over the console stdin FIFO (e.g. /tmp/emu_in)
printf 'phywear cap root\n' > /tmp/emu_in        # or: cap raw / pendulum / spring / ...
adb pull /dev/fb0 /tmp/frame.raw
python3 - <<'PY'
from PIL import Image
d = open('/tmp/frame.raw','rb').read(); fs = 1280*800*4
Image.frombytes('RGBA', (1280,800), d[fs:2*fs]).save('/tmp/frame.png')  # frame1 = active
PY
```
- `phywear cap <name>` opens one screen and dwells (no auto-nav), for clean per-page shots.
- `phywear raw` / `phywear demo` also work; the auto-demo runs ~38s.
- Forces Chinese via `CONFIG_EXAMPLES_PHYWEAR_SIM_ZH_DEMO` (i18n) or `phywear lang zh`.

## Step 4 — Benchmark / FPS tuning
```bash
# perf counters (idle heap + rendered FPS) print every 2s from the main loop:
printf 'phywear pendbench 0 0\n' > /tmp/emu_in   # pendulum bench, no auto-page-flip
# other benches: specbench / micspecbench / magspecbench / springbench / centribench
#                 inclinebench / rulerbench / timebench light|acoustic / applausebench
```
- Track `fps=` in the console log; target 43 FPS via EPIC GPU acceleration (LVGL `draw/sifli`
  back-end). If `fps` is low, verify `CONFIG_LV_USE_EPIC` / the EPIC flush path and label
  throttle (e.g. `incline 25Hz`, `stopwatch 10Hz`) to cut value-label churn.

## Outputs
- `nuttx.bin` + `System.map` (built firmware) — always point local-check-in to these, not `.rpk`.
- `screenshots/phy_core_*.png` (Chinese GUI frames read from `/dev/fb0`).
- Console log with WARN (sim, missing sensors) / FPS probe / perf lines for evidence.

## Known limits (be honest in reports)
- Emulator window black = qemu goldfish GPU-layer vs `/dev/fb0` split (architecture, not a bug).
- After boot, only the first `phywear` process renders to `/dev/fb0`; later processes show a
  stale frame. Capture the page you want first, or render all pages in one demo run.
- The auto-demo stalls at 掌声计 (no `/dev/mic0` on the emulator).
- DeepSeek Harness sessions can't be imported into the official AI-log schema (tool enum
  is `opencode/claude-code/codex/kiro` only); use the official tools for the `logs/` export.
