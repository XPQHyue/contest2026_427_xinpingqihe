/****************************************************************************
 * apps/examples/phywear/pw_power.c
 *
 * [AGENT-WIN11] 电源/电量实现（黄山派 SF32LB52）
 *
 * 数据来源（都是现成的设备节点，不改驱动）：
 *   - 电池电压： /dev/adc0  —— sf32lb_adc 初始化时就把通道固定成
 *                ADC_CHAN_5 = ADC_CHAN_VBAT（见 sf32lb_adc.h），
 *                驱动内部已做 (raw - offset) * ratio 换算，返回引脚 mV；
 *                硬件对 VBAT 有 ~2.01 分压，故再乘回 2.01。
 *                该驱动只认 ANIOC_TRIGGER（触发一次采样），read() 取结果。
 *   - 充电状态： PA_44 = VBUS_DET（bsp_pinmux.c:208 已配成 GPIO 输入），
 *                板级把它注册成 /dev/gpio1 的第 2 个 gpiochar 输入脚
 *                （/dev/gpio0 是原有 PB_34，别混用）。
 *   - 背光/息屏：/dev/lcdlight 写 1 字节 0~100，0 = 背光关。
 *
 * 说明：这里只做只读采样 + 背光控制，不动供电门控（PA38/PA26/PA30），
 *       因此不影响"插电池就开机"的既有行为。
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0.
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <fcntl.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/analog/adc.h>
#include <nuttx/analog/ioctl.h>
#include <nuttx/ioexpander/gpio.h>

#include "pw_power.h"
#include "pw_settings.h"

#ifdef CONFIG_EXAMPLES_PHYWEAR_POWER

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define PW_ADC_DEV        "/dev/adc0"

/* 引脚 mV → 电池 mV 的换算系数。
 *
 * 历史：第一版按"硬件 2.01 分压"写死 2.01，结果 USB 供电时算出 6.7 V
 * （见 2026-11-03 启动日志），物理上不可能 —— 单节锂电在有 VBUS 时不会
 * 超过 4.35 V。也就是说 2.01 这个值跟板上实际通道对不上，或者驱动的
 * mV 换算缺出厂标定而整体偏大约 1.6 倍（启动日志确有
 * "ADC calibration data missing, use defaults"，标定表是代码里硬编码的默认值）。
 *
 * 系数定稿（2026-11-04，`D-20261103-08` / BOARD §8）：
 * 用户用万用表实测，USB 供电且充电完成（CV 段结束）时 VBAT = **4.24 V**，
 * 同一时刻 ADC 引脚读到 3347 mV → 4240 x 1000 / 3347 = 1266.8 → **1267/1000**。
 * 这是实测锚点，不再是推断值；电池规格 120 mAh / 3.7 V / 0.444 Wh。
 * 若日后换电芯或改分压，用 `phywear batcal <实测 mV>` 现场反推新常数。 */

#define PW_VBAT_DIV_NUM   1267
#define PW_VBAT_DIV_DEN   1000

#define PW_VBAT_MIN_MV    3400        /* 0%（保护板截止，未实测；用户确认后再下调） */
#define PW_VBAT_MAX_MV    4240        /* 100%（万用表实测满电） */

#define PW_LCDLIGHT_DEV   "/dev/lcdlight"
#define PW_POWER_BL_ON    CONFIG_EXAMPLES_PHYWEAR_POWER_BL_ON

#define PW_GPIO_DEV       "/dev/gpio1"
#define PW_GPIO_VBUS_DET  44          /* PA_44 */

/****************************************************************************
 * Public Functions
 ****************************************************************************/

static int g_pw_last_pin_mv;

/* 运行时换算系数：默认取上面的编译期常数，`phywear batcal <mv>` 可现场改 */

static int g_pw_cal_num = PW_VBAT_DIV_NUM;
static int g_pw_cal_den = PW_VBAT_DIV_DEN;

/* 采样环形缓冲（见 pw_power.h 的说明） */

#define PW_PLOG_MAX 64

struct pw_plog_rec
{
  int32_t t_ms;
  int16_t pin;
  int16_t vbat;
  int8_t  chg;
};

static struct pw_plog_rec g_plog[PW_PLOG_MAX];
static int  g_plog_head;
static int  g_plog_cnt;

/* 状态缓存：顶栏 1 Hz 刷新，但 ADC+GPIO 每 5 s 采一次就够（插拔充电器是慢变
 * 事件，采太快只是白占总线；ADC 每次 open/ioctl/read/close 也不便宜）。 */

#define PW_POWER_CACHE_MS  5000

static int     g_pw_cache_pct = -1;
static int     g_pw_cache_chg = -1;
static int64_t g_pw_cache_ms;

static int64_t pw_power_now_ms(void)
{
  struct timespec ts;

  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

int pw_power_vbat_mv(void)
{
  struct adc_msg_s sample;
  int fd;
  int ret;

  fd = open(PW_ADC_DEV, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }

  /* 该驱动不支持 bind，只支持"触发一次采样"；采样后 read 才有数据 */

  ret = ioctl(fd, ANIOC_TRIGGER, 0);
  if (ret < 0)
    {
      close(fd);
      return -errno;
    }

  ret = read(fd, &sample, sizeof(sample));
  close(fd);

  if (ret != (int)sizeof(sample))
    {
      return -EIO;
    }

  /* sample.am_data 是引脚上的 mV（已按驱动内部标定换算）。
   * 注意：本板启动日志报 "ADC calibration data missing, use defaults"，
   * 出厂标定缺失时这个值只能当趋势看，绝对值需要软件 trim 或补标定。
   */

  g_pw_last_pin_mv = (int)sample.am_data;

  return (int)((int32_t)sample.am_data * g_pw_cal_num / g_pw_cal_den);
}

int pw_power_set_ref(int ref_mv)
{
  int pin = pw_power_vbat_mv();       /* 顺便取一次最新引脚值 */
  int pin_mv;
  int pct;

  if (pin < 0)
    {
      return pin;
    }

  pin_mv = g_pw_last_pin_mv;
  if (pin_mv <= 0 || ref_mv <= 0)
    {
      return -EINVAL;
    }

  /* 选让 ref_mv 精确落位的整数对（分母 1000，分子四舍五入） */

  g_pw_cal_num = (ref_mv * 1000 + pin_mv / 2) / pin_mv;
  g_pw_cal_den = 1000;

  pct = pw_power_pct();

  printf("[power] batcal: pin=%d mV ref=%d mV -> "
         "PW_VBAT_DIV_NUM=%d PW_VBAT_DIV_DEN=%d (vbat=%d mV soc=%d%%)\n",
         pin_mv, ref_mv, g_pw_cal_num, g_pw_cal_den, ref_mv, pct);
  fflush(stdout);
  return 0;
}

int pw_power_pct(void)
{
  int mv = pw_power_vbat_mv();

  if (mv < 0)
    {
      return mv;
    }

  if (mv <= PW_VBAT_MIN_MV)
    {
      return 0;
    }

  if (mv >= PW_VBAT_MAX_MV)
    {
      return 100;
    }

  /* 四舍五入：整数截断会让 4199 mV 显示 99%，顶部 1 mV 抖动就在 100/99
   * 之间来回跳；宁可偏半个百分点，也不要看起来在"掉电"。 */

  return ((mv - PW_VBAT_MIN_MV) * 100 + (PW_VBAT_MAX_MV - PW_VBAT_MIN_MV) / 2)
         / (PW_VBAT_MAX_MV - PW_VBAT_MIN_MV);
}

int pw_power_charging(void)
{
  bool level = false;
  int fd;
  int ret;

  fd = open(PW_GPIO_DEV, O_RDONLY);
  if (fd < 0)
    {
      return -errno;
    }

  /* GPIOC_READ 的参数是"指向 bool 的指针" */

  ret = ioctl(fd, GPIOC_READ, (unsigned long)&level);
  close(fd);

  if (ret < 0)
    {
      return -errno;
    }

  return level ? 1 : 0;
}

int pw_power_state(int *pct, int *chg)
{
  int64_t now = pw_power_now_ms();

  if (g_pw_cache_pct < 0 || now - g_pw_cache_ms >= PW_POWER_CACHE_MS)
    {
      int p = pw_power_pct();
      int c = pw_power_charging();

      if (p >= 0)
        {
          g_pw_cache_pct = p;
        }

      if (c >= 0)
        {
          g_pw_cache_chg = c;
        }

      /* 记一条环形缓冲：拔 USB 时串口会断，板子得自己把过渡过程记下来 */

      if (p >= 0 && c >= 0)
        {
          struct pw_plog_rec *rec = &g_plog[g_plog_head];

          rec->t_ms = (int32_t)now;
          rec->pin  = (int16_t)g_pw_last_pin_mv;
          rec->vbat = (int16_t)((int32_t)g_pw_last_pin_mv *
                                g_pw_cal_num / g_pw_cal_den);
          rec->chg  = (int8_t)c;

          g_plog_head = (g_plog_head + 1) % PW_PLOG_MAX;
          if (g_plog_cnt < PW_PLOG_MAX)
            {
              g_plog_cnt++;
            }
        }

      g_pw_cache_ms = now;
    }

  if (pct != NULL)
    {
      *pct = g_pw_cache_pct;
    }

  if (chg != NULL)
    {
      *chg = g_pw_cache_chg;
    }

  return g_pw_cache_pct >= 0 ? 0 : -EAGAIN;
}

int pw_power_pin_mv(void)
{
  return g_pw_last_pin_mv;
}

void pw_power_log_dump(void)
{
  int idx;
  int i;
  int32_t base;

  if (g_plog_cnt == 0)
    {
      printf("[plog] 空（GUI 还没采过样）\n");
      return;
    }

  idx  = (g_plog_head - g_plog_cnt + PW_PLOG_MAX) % PW_PLOG_MAX;
  base = g_plog[idx].t_ms;

  printf("[plog] %d 样本，格式：+秒 pin_mV vbat_mV chg\n", g_plog_cnt);

  for (i = 0; i < g_plog_cnt; i++)
    {
      const struct pw_plog_rec *rec = &g_plog[(idx + i) % PW_PLOG_MAX];
      long dt = (long)(rec->t_ms - base);

      printf("[plog] +%ld.%03lds pin=%d vbat=%d chg=%d\n",
             dt / 1000, dt % 1000, rec->pin, rec->vbat, rec->chg);
    }
}

int pw_power_backlight(int level)
{
  uint8_t val = (uint8_t)(level < 0 ? 0 : (level > 100 ? 100 : level));
  int fd;
  int ret;

  fd = open(PW_LCDLIGHT_DEV, O_WRONLY);
  if (fd < 0)
    {
      return -errno;
    }

  ret = write(fd, &val, 1);
  close(fd);

  return ret == 1 ? 0 : -EIO;
}

int pw_power_screen_off(void)
{
  return pw_power_backlight(0);
}

int pw_power_screen_on(void)
{
  return pw_power_backlight(PW_POWER_BL_ON);
}

void pw_power_report(void)
{
  int mv = pw_power_vbat_mv();
  int pct = pw_power_pct();
  int chg = pw_power_charging();

  if (mv < 0)
    {
      printf("[power] vbat read failed: %d\n", mv);
      return;
    }

  printf("[power] vbat=%d mV soc=%d%% chg=%d pin=%d mV (VBUS_DET PA44)\n",
         mv, pct, chg, g_pw_last_pin_mv);
}

#endif /* CONFIG_EXAMPLES_PHYWEAR_POWER */

/****************************************************************************
 * 关机 / 重启（长按电源键菜单）
 *
 * HAL_PMU_* 在 vendor 的 HAL 里，apps 侧没有稳定头文件，这里只做外部声明：
 * 链接时由 vendor 的 HAL 库提供（bsp_power.c 里已经在用同一套 PMU 代码）。
 ****************************************************************************/

/* 直接操作 PMUC 寄存器：不引 vendor 头（apps 侧 include 路径里没有 bf0_hal.h），
 * 也不调 HAL_PMU_*（它们会先 WakeCore 等 LCPU，本板单核必死等）。
 * 地址与位定义来自 vendor/sifli/chips/drivers/cmsis/sf32lb52x/{register.h,pmuc.h}。
 */

#define PW_PMUC_BASE        0x500CA000UL
#define PW_PMUC_CR          (*(volatile uint32_t *)(PW_PMUC_BASE + 0x00))
#define PW_PMUC_WSR         (*(volatile uint32_t *)(PW_PMUC_BASE + 0x08))
#define PW_PMUC_WCR         (*(volatile uint32_t *)(PW_PMUC_BASE + 0x0c))
#define PW_PMUC_WER         (*(volatile uint32_t *)(PW_PMUC_BASE + 0x04))   /* 唤醒使能：bit8=CHG, bit7=LOWBAT */
#define PW_PMUC_CR_HIBER_EN (1u << 1)
#define PW_PMUC_CR_REBOOT   (1u << 2)

#define PW_AIRCR            (*(volatile uint32_t *)0xE000ED0Cu)
#define PW_AIRCR_SYSRESETREQ (1u << 2)
#define PW_AIRCR_VECTKEY    (0x05FAu << 16)

volatile int pw_pmu_keep_alive;   /* 只为保住 vendor PMU 段被链接进来 */

void pw_power_shutdown(void)
{
  extern void HAL_PMU_EnterShutdown(void);

  if (pw_pmu_keep_alive)
    {
      HAL_PMU_EnterShutdown();   /* 永不执行，仅阻止 --gc-sections 丢掉该段 */
    }

  /* **不要用 HAL_PMU_EnterShutdown()**：它内部先 HAL_HPAON_WakeCore(CORE_ID_LCPU)
   * 并忙等 LCPU 唤醒，而黄山派是单核板（LCPU 没跑固件），这一等就是死等 ——
   * 用户实测"点关机后屏幕卡住、整机无响应"，就是这个原因（与 ADC 标定那处同一个坑）。
   * 这里照抄 SDK 里真正断电的那几步，跳过 LCPU 相关部分。
   */

  syslog(LOG_INFO, "[power] shutdown requested by user\n");

  up_irq_save();

  /* 插着 USB 时会被"充电器插入"事件唤醒，表现为"关机后又自己开机"。
   * 关掉 CHG(bit8) 与 LOWBAT(bit7) 这两个唤醒源，保留按键/RTC 等其余配置。
   */

  PW_PMUC_WER &= ~((1u << 8) | (1u << 7));

  PW_PMUC_WCR = PW_PMUC_WSR;          /* 清 wakeup status（等价 HAL_PMU_CLEAR_WSR） */

  /* 先拉低再拉高，产生足够宽的掉电脉冲（SDK 里那 100 ms 是有原因的） */

  PW_PMUC_CR &= ~PW_PMUC_CR_HIBER_EN;
  up_mdelay(100);
  PW_PMUC_CR |= PW_PMUC_CR_HIBER_EN;

  /* 保险：如果 PMU 没有真的切断电源（比如分区配置不对），
   * 1 s 后用系统复位兜底，至少不会停在"黑屏不响应"。
   */

  /* 不再做"1 秒后复位"兜底：那条兜底会在 hibernate 没生效时把板子重启，
   * 用户看到的就是"关机后又自己开机"。这里就停住不动。
   */

  for (;;)
    {
    }
}

void pw_power_reboot(void)
{
  extern void HAL_PMU_Reboot(void);

  if (pw_pmu_keep_alive)
    {
      HAL_PMU_Reboot();
    }


  syslog(LOG_INFO, "[power] reboot requested by user\n");

  up_irq_save();

  /* 用 PMUC 的 REBOOT 位做整芯片复位，不用 AIRCR.SYSRESETREQ：
   * 后者只复位 CPU 核，QSPI/flash 还停在 4 字节地址模式，ROM 起不来
   * → 表现就是"点重启卡住"。SDK 的 HAL_PMU_Reboot() 最后也是这一位。
   */

  if (PW_PMUC_CR & PW_PMUC_CR_REBOOT)
    {
      PW_PMUC_CR &= ~PW_PMUC_CR_REBOOT;
      up_mdelay(100);
    }

  PW_PMUC_CR |= PW_PMUC_CR_REBOOT;

  for (;;)
    {
    }
}
