/****************************************************************************
 * vendor/sifli/boards/sf32lb52/drivers/audio/sf32lb52_spk.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * SF32LB52 板载扬声器驱动（字符设备 /dev/spk0）。
 *
 * 硬件：片内 AUDCODEC DAC0 → 外部功放（AUDIO_PA_CTRL = PA42 使能）→ 扬声器。
 * 参考：SiFli-SDK rtos/rtthread/bsp/sifli/drivers/drv_audcodec_m.c
 *       （SF32LB52 单实例 AUDCODEC，对应 HAL bf0_hal_audcodec_m.c）
 *
 * 实现：DMA 循环播放一段固定长度的正弦缓冲（不依赖应用持续喂数据），
 *       频率/音量由 ioctl 控制；PA 只在播放时拉高，避免静态底噪。
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/fs/fs.h>
#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/kmalloc.h>
#include <nuttx/cache.h>

#include <debug.h>
#include <errno.h>
#include <math.h>
#include <string.h>

#include "bf0_hal.h"
#include "dma_config.h"

#include "sf32lb52_spk.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define SPK_BUFSIZE   (PW_SPK_FRAMES * 4)   /* 立体声 16bit：L/R 各 2 字节 */
#define SPK_AMP       1.00                  /* 满量程比例（真机 A/B：100% 明显比 30% 响） */
#define SPK_PA_PIN    42                    /* PA42 = AUDIO_PA_CTRL */
#define SPK_PA_PORT   hwp_gpio1

/* DAC 时钟配置（xtal 48M，16 kHz），对应 SDK codec_dac_clk_config_xtal[3]。
 * SINC_GAIN 取 0x14D = AVDD 3.3V 档（SDK 里 AVDD_V18_ENABLE 未定义时的值）。 */

#define SPK_SINC_GAIN 0x14D

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const AUDCODE_DAC_CLK_CONFIG_TYPE g_spk_dac_clk =
{
  PW_SPK_RATE,       /* samplerate */
  0,                 /* clk_src_sel: 0 = xtal 48M */
  1,                 /* clk_div */
  4,                 /* osr_sel */
  SPK_SINC_GAIN,     /* sinc_gain */
  0,                 /* sel_clk_dac_source: 0 = xtal */
  5,                 /* diva_clk_dac */
  4,                 /* diva_clk_chop_dac */
  2,                 /* divb_clk_chop_dac */
  20,                /* diva_clk_chop_bg */
  20,                /* diva_clk_chop_refgen */
  0,                 /* sel_clk_dac (SF32LB52X) */
};

static AUDCODEC_HandleTypeDef g_spk_codec;
static DMA_HandleTypeDef g_spk_dma;

static uint8_t g_spk_buf[SPK_BUFSIZE] __attribute__((aligned(32)));

/* 淡入/淡出：真机听测发现直接开功放或去静音会有明显"哒"声（功放把瞬态放大）。
 * 用 DAC 音量从静音档爬到目标档（约 40 ms），听感平滑。 */

#define SPK_FADE_STEPS   6
#define SPK_FADE_STEP_US 7000

static int spk_nuttx_isr(int irq, FAR void *context, FAR void *arg);

static bool g_spk_hw_ready;
static bool g_spk_playing;
static bool g_spk_dma_running;   /* DMA 一旦启动就常驻（循环模式） */
static uint32_t g_spk_freq;
static int g_spk_volume = PW_SPK_VOL_DEFAULT;
static int g_spk_amp = (int)(SPK_AMP * 100.0f);   /* 幅度百分比，可运行时调 */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: spk_pa_set
 *
 * Description:
 *   功放使能（PA42）。播放前拉高，停止后拉低。
 ****************************************************************************/

static void spk_pa_set(bool on)
{
  HAL_GPIO_WritePin(SPK_PA_PORT, SPK_PA_PIN,
                    on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

/****************************************************************************
 * Name: spk_pa_init
 ****************************************************************************/

static void spk_pa_init(void)
{
  GPIO_InitTypeDef gpio;

  HAL_PIN_Set(PAD_PA42, GPIO_A42, PIN_NOPULL, 1);

  memset(&gpio, 0, sizeof(gpio));
  gpio.Pin  = SPK_PA_PIN;
  gpio.Mode = GPIO_MODE_OUTPUT;
  gpio.Pull = GPIO_NOPULL;
  HAL_GPIO_Init(SPK_PA_PORT, &gpio);

  spk_pa_set(false);
}

/****************************************************************************
 * Name: spk_fill_tone
 *
 * Description:
 *   用正弦填满 DMA 缓冲：L/R 交错 16bit。频率对齐到 PW_SPK_FREQ_STEP 的
 *   整数倍，保证 50 ms 缓冲内正好是整数个周期，循环处不会出现相位跳变。
 *
 ****************************************************************************/

static void spk_fill_tone(uint32_t freq)
{
  FAR int16_t *pcm = (FAR int16_t *)g_spk_buf;
  double amp = (double)g_spk_amp / 100.0 * 32767.0;
  double step = 2.0 * M_PI * (double)freq / (double)PW_SPK_RATE;
  double phase = 0.0;
  int i;

  for (i = 0; i < PW_SPK_FRAMES; i++)
    {
      int16_t s = (int16_t)(amp * sin(phase));

      pcm[i * 2]     = s;   /* 左 */
      pcm[i * 2 + 1] = s;   /* 右 */
      phase += step;

      if (phase >= 2.0 * M_PI)
        {
          phase -= 2.0 * M_PI;
        }
    }

  up_clean_dcache((uintptr_t)g_spk_buf, (uintptr_t)g_spk_buf + SPK_BUFSIZE);
}

/****************************************************************************
 * Name: spk_apply_volume
 ****************************************************************************/

static void spk_apply_volume(int db)
{
  if (db < -36)
    {
      db = -36;
    }
  else if (db > 54)
    {
      db = 54;
    }

  g_spk_volume = db;

  /* HAL 用 Q31.1 定点表示 dB（-72..108） */

  HAL_AUDCODEC_Config_DACPath_Volume(&g_spk_codec, 0, db * 2);
  HAL_AUDCODEC_Config_DACPath_Volume(&g_spk_codec, 1, db * 2);
}

/****************************************************************************
 * Name: spk_fade
 *
 * Description:
 *   把 DAC 音量从 from_db 分步爬到 to_db（用于淡入/淡出，避免爆音）。
 ****************************************************************************/

static void spk_fade(int from_db, int to_db)
{
  int i;

  for (i = 0; i <= SPK_FADE_STEPS; i++)
    {
      int db = from_db + (to_db - from_db) * i / SPK_FADE_STEPS;

      HAL_AUDCODEC_Config_DACPath_Volume(&g_spk_codec, 0, db * 2);
      HAL_AUDCODEC_Config_DACPath_Volume(&g_spk_codec, 1, db * 2);
      usleep(SPK_FADE_STEP_US);
    }
}

/****************************************************************************
 * Name: spk_start
 ****************************************************************************/

static int spk_start(uint32_t freq, int volume_db)
{
  if (!g_spk_hw_ready)
    {
      return -ENODEV;
    }

  if (freq < PW_SPK_FREQ_MIN)
    {
      freq = PW_SPK_FREQ_MIN;
    }
  else if (freq > PW_SPK_FREQ_MAX)
    {
      freq = PW_SPK_FREQ_MAX;
    }

  freq = (freq / PW_SPK_FREQ_STEP) * PW_SPK_FREQ_STEP;

  /* 已经在放同一个频率：只更新音量，不碰缓冲（避免爆音） */

  if (g_spk_playing && freq == g_spk_freq)
    {
      spk_apply_volume(volume_db);
      return OK;
    }

  /* 换频率：直接重填 DMA 缓冲，**不做 mute/unmute**。
   * 真机听测：切频率时静音再开会产生"较大的一声"爆音；正弦缓冲首尾都在
   * 相位 0（频率对齐到 20 Hz 整数倍 ⇒ 缓冲内正好整数个周期），
   * 直接换缓冲最多只有一次半周期过渡，听感是平滑的。
   * DMA 循环模式且**只启动一次**：反复 Start/Abort 第二次会返回 HAL_BUSY。 */

  spk_fill_tone(freq);
  g_spk_freq = freq;
  spk_apply_volume(volume_db);

  if (!g_spk_dma_running)
    {
      g_spk_codec.State[HAL_AUDCODEC_DAC_CH0] = HAL_AUDCODEC_STATE_READY;

      if (HAL_AUDCODEC_Transmit_DMA(&g_spk_codec, g_spk_buf, SPK_BUFSIZE,
                                    HAL_AUDCODEC_DAC_CH0) != HAL_OK)
        {
          int err = errno;

          snerr("ERROR: spk Transmit_DMA failed\n");
          errno = err;
          return -EIO;
        }

      __HAL_AUDCODEC_DAC_ENABLE(&g_spk_codec);
      HAL_AUDCODEC_Config_Analog_DACPath((AUDCODE_DAC_CLK_CONFIG_TYPE *)&g_spk_dac_clk);
      g_spk_dma_running = true;
    }

  /* 先以静音档去静音、再开功放，最后爬到目标音量（避免"哒"声） */

  g_spk_codec.State[HAL_AUDCODEC_DAC_CH0] = g_spk_codec.State[HAL_AUDCODEC_DAC_CH0];
  HAL_AUDCODEC_Config_DACPath_Volume(&g_spk_codec, 0, PW_SPK_VOL_MIN * 2);
  HAL_AUDCODEC_Config_DACPath_Volume(&g_spk_codec, 1, PW_SPK_VOL_MIN * 2);
  HAL_AUDCODEC_Config_DACPath(&g_spk_codec, 0);   /* unmute */
  spk_pa_set(true);
  spk_fade(PW_SPK_VOL_MIN, g_spk_volume);
  g_spk_playing = true;

  sninfo("spk tone %u Hz, %d dB\n", (unsigned)freq, g_spk_volume);
  return OK;
}

/****************************************************************************
 * Name: spk_stop
 ****************************************************************************/

static void spk_stop(void)
{
  if (!g_spk_hw_ready)
    {
      return;
    }

  if (g_spk_dma_running && g_spk_playing)
    {
      spk_fade(g_spk_volume, PW_SPK_VOL_MIN);         /* 先淡出 */
      HAL_AUDCODEC_Config_DACPath(&g_spk_codec, 1);   /* 再静音，DMA 继续跑 */
    }

  spk_pa_set(false);
  g_spk_playing = false;
}

/****************************************************************************
 * Name: spk_hw_init
 ****************************************************************************/

static int spk_hw_init(void)
{
  int ret;

  /* DAC0 使用 DMA2_Channel1（SF32LB58x 官方配置同样把 DAC0 放这里） */

  memset(&g_spk_dma, 0, sizeof(g_spk_dma));
  g_spk_dma.Instance     = DMA2_Channel1;
  g_spk_dma.Init.Request = AUDCODEC_DAC0_DMA_REQUEST;
  g_spk_codec.hdma[HAL_AUDCODEC_DAC_CH0] = &g_spk_dma;

  g_spk_codec.Instance        = hwp_audcodec;
  g_spk_codec.Init.dac_cfg.opmode  = 1;   /* 1 = mem → audcodec */
  g_spk_codec.Init.dac_cfg.dac_clk = (AUDCODE_DAC_CLK_CONFIG_TYPE *)&g_spk_dac_clk;

  /* 音频电源域/模块时钟打开后需要一点建立时间，否则后续 AUDCODEC 寄存器
   * 配置可能不生效（真机实测：缺少这段延时偶发启动期挂死）。 */

  HAL_PMU_EnableAudio(1);
  usleep(2000);
  HAL_RCC_EnableModule(RCC_MOD_AUDCODEC);
  usleep(2000);
  HAL_AUDCODEC_Init(&g_spk_codec);
  usleep(2000);

  ret = irq_attach(NX_IRQ(DMAC2_CH1_IRQn), spk_nuttx_isr, NULL);
  if (ret < 0)
    {
      snerr("ERROR: spk irq_attach failed: %d\n", ret);
      return ret;
    }

  up_enable_irq(NX_IRQ(DMAC2_CH1_IRQn));

  /* 与麦克风驱动共用同一颗 AUDCODEC：xtal 模式下 PLL 只开一次 */

  HAL_TURN_ON_PLL();

  spk_pa_init();
  spk_apply_volume(g_spk_volume);

  g_spk_hw_ready = true;
  return OK;
}

/****************************************************************************
 * Name: spk_nuttx_isr
 *
 * Description:
 *   NuttX 中断入口，转发到 HAL 的 DMA2 通道 1 处理器（AUDCODEC DAC0）。
 *
 ****************************************************************************/

static int spk_nuttx_isr(int irq, FAR void *context, FAR void *arg)
{
  HAL_DMA_IRQHandler(&g_spk_dma);
  return OK;
}

/****************************************************************************
 * Name: spk_ioctl
 ****************************************************************************/

static int spk_ioctl(FAR struct file *filep, int cmd, unsigned long arg)
{
  switch (cmd)
    {
      case PW_SPK_IOC_TONE:
        {
          FAR struct pw_spk_tone_s *t =
              (FAR struct pw_spk_tone_s *)arg;
          int vol;

          if (t == NULL)
            {
              return -EINVAL;
            }

          vol = (t->volume_db == INT32_MIN) ? g_spk_volume
                                            : (int)t->volume_db;
          return spk_start(t->freq_hz, vol);
        }

      case PW_SPK_IOC_STOP:
        spk_stop();
        return OK;

      case PW_SPK_IOC_VOLUME:
        spk_apply_volume((int)arg);
        return OK;

      case PW_SPK_IOC_PA:
        spk_pa_set(arg != 0);
        return OK;

      case PW_SPK_IOC_AMP:
        {
          int a = (int)arg;

          if (a < 5)
            {
              a = 5;
            }
          else if (a > 100)
            {
              a = 100;
            }

          g_spk_amp = a;

          /* 正在播放时立即按新幅度重填缓冲（正弦首尾同相位，听感平滑） */

          if (g_spk_playing)
            {
              spk_fill_tone(g_spk_freq);
            }

          sninfo("spk amplitude %d%%\n", a);
          return OK;
        }

      default:
        return -ENOTTY;
    }
}

/****************************************************************************
 * Private Data (file operations)
 ****************************************************************************/

static const struct file_operations g_spk_fops =
{
  NULL,          /* open */
  NULL,          /* close */
  NULL,          /* read */
  NULL,          /* write */
  NULL,          /* seek */
  spk_ioctl,     /* ioctl */
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int sf32lb52_spk_register(FAR const char *devpath)
{
  int ret;

  DEBUGASSERT(devpath != NULL);

  ret = spk_hw_init();
  if (ret < 0)
    {
      return ret;
    }

  ret = register_driver(devpath, &g_spk_fops, 0666, NULL);
  if (ret < 0)
    {
      snerr("ERROR: spk register_driver failed: %d\n", ret);
      return ret;
    }

  sninfo("SF32LB52 speaker registered as %s\n", devpath);
  return OK;
}
