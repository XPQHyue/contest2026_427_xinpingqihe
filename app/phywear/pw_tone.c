/****************************************************************************
 * apps/examples/phywear/pw_tone.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 扬声器发声小工具。见 pw_tone.h。
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <sys/ioctl.h>
#include <unistd.h>

#include "pw_tone.h"

#ifdef CONFIG_SF32LB52_SPK
#  include "sf32lb52_spk.h"
#endif

/****************************************************************************
 * Private Data
 ****************************************************************************/

static bool g_tone_on;
static uint32_t g_tone_freq;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

bool pw_tone_available(void)
{
#ifdef CONFIG_SF32LB52_SPK
  int fd = open("/dev/spk0", O_RDWR);

  if (fd < 0)
    {
      return false;
    }

  close(fd);
  return true;
#else
  return false;
#endif
}

int pw_tone_play(uint32_t freq_hz, int volume_db)
{
#ifdef CONFIG_SF32LB52_SPK
  struct pw_spk_tone_s tone;
  int fd;
  int rc;

  /* 每次调用重新打开：fd 属于调用它的任务，而本文件里的变量是应用全局
   * （同一份 .data，跨多次运行存活）。缓存 fd 会让第二次运行的 ioctl 拿到
   * 上一个任务已关闭的 fd → EBADF（真机踩过）。关闭 fd 不影响正在播放的
   * 声音：DMA 在驱动里常驻。 */

  fd = open("/dev/spk0", O_RDWR);
  if (fd < 0)
    {
      return -errno;
    }

  tone.freq_hz = freq_hz;
  tone.volume_db = volume_db;

  rc = ioctl(fd, PW_SPK_IOC_TONE, (unsigned long)&tone);
  if (rc < 0)
    {
      int err = errno;

      close(fd);
      return -err;
    }

  close(fd);
  g_tone_on = true;
  g_tone_freq = freq_hz;
  return 0;
#else
  (void)freq_hz;
  (void)volume_db;
  return -ENODEV;
#endif
}

int pw_tone_set_amp(int percent)
{
#ifdef CONFIG_SF32LB52_SPK
  int fd = open("/dev/spk0", O_RDWR);
  int rc;

  if (fd < 0)
    {
      return -errno;
    }

  rc = ioctl(fd, PW_SPK_IOC_AMP, (unsigned long)percent);
  close(fd);
  return rc < 0 ? -errno : 0;
#else
  (void)percent;
  return -ENODEV;
#endif
}

int pw_tone_set_pa(int on)
{
#ifdef CONFIG_SF32LB52_SPK
  int fd = open("/dev/spk0", O_RDWR);
  int rc;

  if (fd < 0)
    {
      return -errno;
    }

  rc = ioctl(fd, PW_SPK_IOC_PA, (unsigned long)(on ? 1 : 0));
  close(fd);
  return rc < 0 ? -errno : 0;
#else
  (void)on;
  return -ENODEV;
#endif
}

void pw_tone_stop(void)
{
#ifdef CONFIG_SF32LB52_SPK
  int fd = open("/dev/spk0", O_RDWR);

  if (fd >= 0)
    {
      ioctl(fd, PW_SPK_IOC_STOP, 0);
      close(fd);
    }
#endif

  g_tone_on = false;
}

bool pw_tone_playing(void)
{
  return g_tone_on;
}

uint32_t pw_tone_freq(void)
{
  return g_tone_freq;
}

int pw_tone_beep(uint32_t freq_hz, int ms, int volume_db)
{
  int ret = pw_tone_play(freq_hz, volume_db);

  if (ret < 0)
    {
      return ret;
    }

  usleep((useconds_t)ms * 1000);
  pw_tone_stop();
  return 0;
}
