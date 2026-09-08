/****************************************************************************
 * vendor/sifli/boards/sf32lb52/drivers/audio/sf32lb52_mic.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.  The
 * ASF licenses this file to you under the Apache License, Version 2.0 (the
 * "License"); you may not use this file except in compliance with the
 * License.  You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS, WITHOUT
 * WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.  See the
 * License for the specific language governing permissions and limitations
 * under the License.
 *
 ****************************************************************************/

/* SF32LB52 板载模拟 MEMS 麦克风驱动（字符设备 /dev/mic0）。
 *
 * 硬件：模拟 MEMS MIC 经片内 AUDCODEC 单端 ADC 输入
 *       （MIC_BIAS=引脚36 供电，ADCP=引脚37 输入）。
 * 参考：SiFli-SDK rtos/rtthread/bsp/sifli/drivers/drv_audcodec_m.c
 *       （SF32LB52 单实例 AUDCODEC 版本，对应 HAL bf0_hal_audcodec_m.c）。
 *
 * 实现：单次 DMA 采集 —— 每次 read() 触发一次 16kHz/16bit/单声道采样，
 *       采集 MIC_SAMPLES 个样本后返回。DMA 使用 AUDCODEC_ADC0(DMA1_Channel4)。
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <nuttx/fs/fs.h>
#include <nuttx/irq.h>
#include <nuttx/arch.h>
#include <nuttx/kmalloc.h>
#include <nuttx/semaphore.h>
#include <nuttx/cache.h>

#include <assert.h>
#include <debug.h>
#include <errno.h>
#include <string.h>

#include "bf0_hal.h"
#include "dma_config.h"

/* HAL 内部函数（bf0_hal_audcodec_m.c 提供但未在头文件声明）：
 * 设置 ADC 通路麦克风增益。 */

HAL_StatusTypeDef HAL_AUDCODEC_Config_ADCPath_Volume(
    AUDCODEC_HandleTypeDef *hacodec, int channel, int volume);

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

#define MIC_SAMPLERATE   16000    /* 采样率 16kHz */
#define MIC_SAMPLES      1024     /* 每次 read 返回的采样数（16-bit 单声道） */
#define MIC_BUFSIZE      (MIC_SAMPLES * 2)
#define MIC_VOLUME       0        /* 麦克风增益（dB，-60~30；0dB 避免环境噪声饱和） */

/****************************************************************************
 * Private Data
 ****************************************************************************/

/* 16kHz xtal 时钟配置，对应 SDK codec_adc_clk_config_xtal[3] */

static AUDCODE_ADC_CLK_CONFIG_TYPE g_mic_adc_clk =
{
  MIC_SAMPLERATE,    /* samplerate */
  0,                 /* clk_src_sel: 0 = xtal 48M */
  10,                /* clk_div */
  1,                 /* osr_sel: 300 */
  0,                 /* sel_clk_adc_source: 0 = xtal */
  0,                 /* sel_clk_adc */
  5,                 /* diva_clk_adc */
  2,                 /* fsp */
};

static AUDCODEC_HandleTypeDef g_hacodec;
static DMA_HandleTypeDef g_hdma_adc0;
static sem_t g_mic_sem;
static volatile bool g_capture_done;

/* DMA 接收缓冲（32 字节对齐，便于 cache 操作） */

static uint8_t g_mic_buf[MIC_BUFSIZE] __attribute__((aligned(32)));

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: HAL_AUDCODEC_RxCpltCallback
 *
 * Description:
 *   覆盖 HAL 弱符号：DMA 收满一帧后置完成标志并释放信号量。
 *
 ****************************************************************************/

void HAL_AUDCODEC_RxCpltCallback(AUDCODEC_HandleTypeDef *hacodec, int cid)
{
  g_capture_done = true;
  nxsem_post(&g_mic_sem);
}

/****************************************************************************
 * Name: mic_nuttx_isr
 *
 * Description:
 *   NuttX 中断入口，转发到 HAL 的 DMA 通道 4 处理器
 *   （AUDCODEC_ADC0 使用 DMA1_Channel4）。
 *
 ****************************************************************************/

static int mic_nuttx_isr(int irq, FAR void *context, FAR void *arg)
{
  HAL_DMAC1_CH4_IRQHandler();
  return OK;
}

/****************************************************************************
 * Name: mic_hw_init
 *
 * Description:
 *   一次性硬件初始化：DMA 句柄 → 电源/时钟 → HAL 初始化 → 中断挂接。
 *
 ****************************************************************************/

static int mic_hw_init(void)
{
  int ret;

  /* DMA 句柄：ADC 通道 0 使用 DMA1_Channel4 */

  memset(&g_hdma_adc0, 0, sizeof(g_hdma_adc0));
  g_hdma_adc0.Instance      = AUDCODEC_ADC0_DMA_INSTANCE;
  g_hdma_adc0.Init.Request  = AUDCODEC_ADC0_DMA_REQUEST;
  g_hacodec.hdma[HAL_AUDCODEC_ADC_CH0] = &g_hdma_adc0;

  /* Init 参数（单实例 Instance + ADC 时钟） */

  g_hacodec.Instance          = hwp_audcodec;
  g_hacodec.Init.en_dly_sel   = 0;
  g_hacodec.Init.adc_cfg.opmode = 1;
  g_hacodec.Init.adc_cfg.adc_clk = &g_mic_adc_clk;
  g_hacodec.Init.dac_cfg.opmode = 1;

  /* 音频电源 + 模块时钟 + HAL 初始化 */

  HAL_PMU_EnableAudio(1);
  HAL_RCC_EnableModule(RCC_MOD_AUDCODEC);
  HAL_AUDCODEC_Init(&g_hacodec);

  /* NuttX 中断挂接（DMAC1_CH4_IRQn = AUDCODEC_ADC0_DMA_IRQ = 53） */

  ret = irq_attach(NX_IRQ(DMAC1_CH4_IRQn), mic_nuttx_isr, NULL);
  if (ret < 0)
    {
      snerr("ERROR: mic irq_attach failed: %d\n", ret);
      return ret;
    }

  up_enable_irq(NX_IRQ(DMAC1_CH4_IRQn));

  /* 一次性打开模拟 PLL 参考（xtal 模式只开一次，避免重复初始化 PLL） */

  HAL_TURN_ON_PLL();

  /* 一次性配置 ADC 通道 + 麦克风增益 + 模拟通路（含 MIC_BIAS）并使能 ADC。
   * 提前预热，让 VCM / HPF 稳定，避免采集开头出现直流瞬态。
   * 之后每次 read 只做 DMA 启停，不再重开模拟通路。 */

  HAL_AUDCODEC_Config_RChanel(&g_hacodec, 0, &g_hacodec.Init.adc_cfg);
  HAL_AUDCODEC_Config_ADCPath_Volume(&g_hacodec, 0, MIC_VOLUME);
  HAL_AUDCODEC_Config_Analog_ADCPath(&g_mic_adc_clk);
  __HAL_AUDCODEC_ADC_ENABLE(&g_hacodec);
  usleep(100 * 1000);   /* 100ms 预热 */

  return OK;
}

/****************************************************************************
 * Name: mic_capture
 *
 * Description:
 *   触发一次单次采集：清 State → 启动 DMA → 等待收满 → 停止 DMA。
 *   ADC / 模拟通路保持常开（在 mic_hw_init 已预热）。
 *
 ****************************************************************************/

static int mic_capture(FAR uint8_t *buf, uint32_t size)
{
  int ret;

  g_capture_done = false;

  /* 清除上一次采集遗留的 BUSY 状态，否则 Receive_DMA 会返回 HAL_BUSY */

  g_hacodec.State[HAL_AUDCODEC_ADC_CH0] = HAL_AUDCODEC_STATE_READY;

  /* 启动 DMA 接收（ADC 已常开，数据立即有效） */

  HAL_AUDCODEC_Receive_DMA(&g_hacodec, buf, size, HAL_AUDCODEC_ADC_CH0);
  HAL_NVIC_EnableIRQ(AUDCODEC_ADC0_DMA_IRQ);

  /* 等待 DMA 收满（64ms @ 16kHz/1024 样本，1s 超时防卡死） */

  ret = nxsem_tickwait_uninterruptible(&g_mic_sem, MSEC2TICK(1000));
  if (ret < 0)
    {
      snerr("ERROR: mic sem wait failed: %d\n", ret);
    }

  /* 停止 DMA 并清除 State，便于下次重新启动 */

  HAL_AUDCODEC_DMAStop(&g_hacodec, HAL_AUDCODEC_ADC_CH0);
  g_hacodec.State[HAL_AUDCODEC_ADC_CH0] = HAL_AUDCODEC_STATE_READY;

  /* DMA 写入的数据需失效 dcache，确保 CPU 读到最新值 */

  up_invalidate_dcache((uintptr_t)buf, (uintptr_t)buf + size);

  return ret;
}

/****************************************************************************
 * Name: mic_read
 ****************************************************************************/

static ssize_t mic_read(FAR struct file *filep, FAR char *buffer,
                        size_t buflen)
{
  int ret;

  if (buflen < MIC_BUFSIZE)
    {
      return -EINVAL;
    }

  ret = mic_capture(g_mic_buf, MIC_BUFSIZE);
  if (ret < 0)
    {
      return ret;
    }

  memcpy(buffer, g_mic_buf, MIC_BUFSIZE);
  return MIC_BUFSIZE;
}

/****************************************************************************
 * Private Data (file operations)
 ****************************************************************************/

static const struct file_operations g_mic_fops =
{
  NULL,         /* open */
  NULL,         /* close */
  mic_read,     /* read */
  NULL,         /* write */
  NULL,         /* seek */
  NULL,         /* ioctl */
};

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: sf32lb52_mic_register
 *
 * Description:
 *   注册板载麦克风字符设备为 'devpath'（如 "/dev/mic0"）。
 *
 ****************************************************************************/

int sf32lb52_mic_register(FAR const char *devpath)
{
  int ret;

  DEBUGASSERT(devpath != NULL);

  /* 初始化完成信号量（初始值为 0，等待 DMA 完成） */

  ret = nxsem_init(&g_mic_sem, 0, 0);
  if (ret < 0)
    {
      snerr("ERROR: mic nxsem_init failed: %d\n", ret);
      return ret;
    }

  /* 硬件初始化 */

  ret = mic_hw_init();
  if (ret < 0)
    {
      snerr("ERROR: mic_hw_init failed: %d\n", ret);
      return ret;
    }

  ret = register_driver(devpath, &g_mic_fops, 0666, NULL);
  if (ret < 0)
    {
      snerr("ERROR: mic register_driver failed: %d\n", ret);
      return ret;
    }

  sninfo("SF32LB52 mic registered as %s\n", devpath);
  return OK;
}
