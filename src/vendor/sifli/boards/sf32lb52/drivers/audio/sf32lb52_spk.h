/****************************************************************************
 * vendor/sifli/boards/sf32lb52/drivers/audio/sf32lb52_spk.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SF32LB52 板载扬声器通路（片内 AUDCODEC DAC0 + 外部功放 AUDIO_PA_CTRL）。
 *
 * 设备以字符设备形式暴露（例如 "/dev/spk0"）：
 *   - ioctl(PW_SPK_IOC_TONE)   播放指定频率的连续正弦（DMA 循环，直到 STOP）
 *   - ioctl(PW_SPK_IOC_STOP)   停止播放并关闭功放
 *   - ioctl(PW_SPK_IOC_VOLUME) 设置 DAC 音量（dB，-36..54）
 *   - ioctl(PW_SPK_IOC_PA)     单独开关功放（调试用）
 ****************************************************************************/

#ifndef __BOARDS_SF32LB52_DRIVERS_AUDIO_SF32LB52_SPK_H
#define __BOARDS_SF32LB52_DRIVERS_AUDIO_SF32LB52_SPK_H

#include <nuttx/config.h>

#include <stdint.h>
#include <sys/ioctl.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* 播放参数（与驱动内部 DMA 缓冲配套） */

#define PW_SPK_RATE        16000   /* 采样率 Hz */
#define PW_SPK_FRAMES      800     /* DMA 缓冲帧数（50 ms） */
#define PW_SPK_FREQ_STEP   20      /* 频率只能取 20 Hz 的整数倍（整周期循环） */
#define PW_SPK_FREQ_MIN    40
#define PW_SPK_FREQ_MAX    4000
#define PW_SPK_VOL_MIN     -36     /* dB，淡入淡出的静音档 */
#define PW_SPK_VOL_DEFAULT 12      /* dB（真机听测：数字幅度比 DAC 音量更有效，故抬幅度、音量取 +12） */

struct pw_spk_tone_s
{
  uint32_t freq_hz;    /* 正弦频率（自动对齐到 PW_SPK_FREQ_STEP） */
  int32_t  volume_db;  /* DAC 音量（dB，-36..54；INT32_MIN 表示用默认值） */
};

/* NuttX 没有 Linux 的 _IOW/_IO：按 nuttx/fs/ioctl.h 的约定，用私有 base
 * 加 _IOC(base, nr) 编码（0x4700 之后当前未被占用）。参数一律用指针/整数
 * 通过 ioctl 的第三个参数传入。 */

#define _PWSPKIOCBASE     (0x4700)
#define _PWSPKIOC(nr)     _IOC(_PWSPKIOCBASE, nr)

#define PW_SPK_IOC_TONE   _PWSPKIOC(0)   /* arg: struct pw_spk_tone_s * */
#define PW_SPK_IOC_STOP   _PWSPKIOC(1)   /* arg: 忽略 */
#define PW_SPK_IOC_VOLUME _PWSPKIOC(2)   /* arg: int（dB） */
#define PW_SPK_IOC_PA     _PWSPKIOC(3)   /* arg: int（0/1） */
#define PW_SPK_IOC_AMP    _PWSPKIOC(4)   /* arg: int 幅度百分比 5..100 */

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: sf32lb52_spk_register
 *
 * Description:
 *   注册扬声器字符设备（如 "/dev/spk0"）。初始化 AUDCODEC DAC0 通路、
 *   功放控制脚（PA42），并保持静音（不播放）。
 *
 * Input Parameters:
 *   devpath - 设备路径，例如 "/dev/spk0"
 *
 * Returned Value:
 *   成功返回 OK(0)，失败返回负的 errno。
 *
 ****************************************************************************/

int sf32lb52_spk_register(FAR const char *devpath);

#endif /* __BOARDS_SF32LB52_DRIVERS_AUDIO_SF32LB52_SPK_H */
