# 声学功能真机证据（2026-09-13）

板子：立创·黄山派 SF32LB52-MOD-1-N16R8 ｜ 固件：`f47004010338c22dbe1d1d5f5235c0ad`（2,023,644 B）

## 1. 界面（串口截图，非屏幕翻拍）

| 文件 | 内容 | md5 |
|---|---|---|
| `tone.png` | 声学 → 音频发生器：440 Hz / +12 dB / 四个频率预设 / 播放按钮 / 已停止 | `05e378bf9e5033827d81b849d951980f` |
| `mic.png` | 原始传感器 → 麦克风：-34 dBFS + 32 段电平柱（截图时喇叭正在放 1 kHz） | `d0ca6cc9d09873fc22cbbc50bc425391` |
| `spk.png` | 原始传感器 → 扬声器：状态"已停止" + 播放 440 Hz / 停止 | `b4a54428b0db9ca3ae2ad6072fcf3c22` |

截图方式：`python3 tools/phywear/pwshot.py shot tone|mic|spk` —— 板子读 LCD PSRAM 双缓冲，base64 逐行带 crc16 输出，整帧两遍，主机侧校验 FNV-1a。

## 2. 声学实测数据

| 项 | 方法 | 结果 |
|---|---|---|
| 扬声器发声 | `phywear tone <Hz> <ms> <dB>` 连续 5 段（440/660/880/1760/220 Hz） | 5/5 全部 `ok`；用户听测确认有声 |
| 麦克风增益 | `phywear micread 10`（喇叭放 1 kHz 正弦） | 0 dB：静音/大声都只有 15~40 LSB；**+30 dB**：环境噪声 ~570 LSB，放声峰值 **12000 LSB（-8.4 dBFS）** |
| 闭环 | 喇叭放声 + 麦克风读值 | 成立（喇叭 → 空气 → 麦克风 → 界面电平） |

复现命令（真机 NSH）：

```text
phywear tone 1000 3000 30     # 1 kHz、最大音量、3 秒
phywear micread 10            # 打印 peak/dBFS/rms
phywear cap tone              # 打开音频发生器页
```

> 说明：`phywear tone` 只发正弦，不写文件；`micread` 读数含环境噪声，用于对比"有声/无声"，
> 不作为声压级定标数据。
