/****************************************************************************
 * apps/examples/phywear/phywear_spec.c
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

/* 频谱实验页（T4 模板，UI-4 里程碑）——统一 FFT 引擎。
 *
 * 实例 1：Accel spectrum（Tools）——加速度 3 轴 @50Hz，256 点窗（5.12s），
 *         df=0.195Hz，范围 0..25Hz。多序列 pw_graph（X/Y/Z 三色）。
 * 实例 2：Spectrum（Acoustics）——麦克风 @16kHz，1024 点窗（64ms），
 *         df=15.6Hz，范围 0..8kHz。后台 pthread 采集，UI 节流 ~5Hz。
 * 实例 3：Mag spectrum（Tools）——地磁 3 轴 @25Hz，128 点窗（5.12s），
 *         df=0.195Hz，范围 0..12.5Hz。波形=|B|−EMA 漂移。
 *
 * 页面（横滑 3 页，统一实验页骨架）：
 *   0 Spectrum —— 频谱多序列图（可缩放/平移）+ 主频大号值卡 + 各轴主频行
 *   1 Waveform  —— pw_scope 实时波形（accel: |a|-g；mic: 原始采样）
 *   2 Help      —— 用法说明
 *
 * 分析链：去均值 → Hann 窗 → radix-2 FFT（pw_analysis）→ 幅度谱按全局
 * 最大归一化；主频用抛物线插值细化。图表禁用 lv_chart（EPIC 无 LINE 加速）。
 *
 * 回归：phywear specbench [秒 [起始页]]（合成 2.3Hz+6.1Hz 多音）
 *       phywear micspecbench [秒 [起始页]]（合成 440Hz+2kHz 多音）
 */

#include <nuttx/config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <fcntl.h>
#include <errno.h>
#include <unistd.h>
#include <pthread.h>

#include <lvgl/lvgl.h>

#include "phywear_ui.h"
#include "phywear_sensors.h"
#include "pw_analysis.h"
#include "pw_scope.h"
#include "pw_graph.h"
#include "phywear_spec.h"
#include "phywear_i18n.h"

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define SPEC_PAGES      3
#define PAGE_W          390
#define PAGE_H          342          /* 450 - 顶栏54 - 底部条54 */

/* 加速度频谱：50Hz 采样，256 点窗 = 5.12s，df=0.195Hz，范围 0..25Hz */
#define ACC_FS          50.0f
#define ACC_FFT_N       256
#define ACC_ANALYZE_TK  20           /* 每 20 拍(400ms) 分析一次 */

/* 麦克风频谱：16kHz 采样，1024 点窗 = 64ms，df=15.6Hz，范围 0..8kHz */
#define MIC_FS          16000.0f
#define MIC_FFT_N       1024
#define MIC_RENDER_TK   10           /* 每 10 拍(200ms, 5Hz) 刷一次图 */

/* 地磁频谱：25Hz 采样（每 2 拍 40ms 读一次），128 点窗 = 5.12s，
 * df=0.195Hz，范围 0..12.5Hz */
#define MAG_FS          25.0f
#define MAG_FFT_N       128
#define MAG_SAMPLE_TK   2            /* 每 2 拍读一次地磁 → 25Hz */
#define MAG_ANALYZE_TK  20           /* 每 20 拍(400ms) 分析一次 */

#define SPEC_MAX_CH     3
#define SPEC_MAX_FFT    MIC_FFT_N
#define SPEC_BINS       (SPEC_MAX_FFT / 2 + 1)

#define SPEC_WAVE_N     120          /* 波形页点数 */
#define TICK_MS         20

#define SPEC_KIND_ACC   0
#define SPEC_KIND_MIC   1
#define SPEC_KIND_MAG   2

#define S_ACC            PW_ACC_TOOL   /* 强调色 */
#define S_MIC            PW_ACC_ACOU

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct spec_ui_s
{
  lv_obj_t *scr;
  lv_obj_t *scroller;
  int       idx;
  lv_obj_t *dots[SPEC_PAGES];

  /* 数据源配置 */
  int       kind;
  int       nch;
  int       fft_n;
  float     fs;
  int       mic_fd;
  int       sdiv;        /* 采样分频（mag=2：每 2 拍采一次） */
  float     ema;         /* 波形去直流 EMA（mag/accel 用） */

  /* 滚动样本缓冲（每通道 fft_n 点，旧在前） */
  float     ch[SPEC_MAX_CH][SPEC_MAX_FFT];
  int       n;

  /* 频谱输出 */
  float     fx[SPEC_BINS];
  float     fmag[SPEC_MAX_CH][SPEC_BINS];   /* 归一化 0..1 */
  float     fpeak[SPEC_MAX_CH];             /* 各通道主频 Hz */
  float     gmax;
  int       have;

  /* UI */
  lv_obj_t *big_lab;          /* 主频大号值 */
  lv_obj_t *big_unit;         /* 主频通道标注 */
  lv_obj_t *peak_row;         /* 各通道主频行 */
  struct pw_graph_s *graph;
  lv_obj_t *wave;
  float     wbuf[SPEC_WAVE_N];
  int       wn;
  int       wdiv;
  lv_obj_t *status;

  int       tick;
  int       div;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct spec_ui_s g_s;

static int  g_bench_on;
static int  g_bench_interval;      /* 翻页间隔（拍数*20ms），0=不翻 */
static long g_bench_sample;        /* 合成信号相位计数 */

/* 麦克风后台采集线程状态 */
static pthread_t      g_mic_tid;
static volatile int   g_mic_run;
static volatile int   g_mic_ready;
static int16_t        g_mic_data[MIC_FFT_N];

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: spec_update_status
 ****************************************************************************/

static void spec_update_status(const char *txt, lv_color_t col)
{
  lv_label_set_text(g_s.status, txt);
  lv_obj_set_style_text_color(g_s.status, col, 0);
}

/****************************************************************************
 * Name: spec_set_page / spec_scroll_to / spec_arrow_cb / spec_scroll_end_cb
 *      spec_sync_dots
 ****************************************************************************/

static void spec_sync_dots(void)
{
  int i;

  for (i = 0; i < SPEC_PAGES; i++)
    {
      lv_obj_set_style_bg_color(g_s.dots[i],
                                (i == g_s.idx) ? S_ACC : PW_COL_CARD_LT, 0);
    }
}

static void spec_set_page(int idx)
{
  if (idx < 0)
    {
      idx = 0;
    }

  if (idx >= SPEC_PAGES)
    {
      idx = SPEC_PAGES - 1;
    }

  g_s.idx = idx;
  spec_sync_dots();
}

static void spec_scroll_to(int idx, bool anim)
{
  if (idx < 0 || idx >= SPEC_PAGES)
    {
      return;
    }

  spec_set_page(idx);
  lv_obj_scroll_to_x(g_s.scroller, idx * PAGE_W,
                     anim ? LV_ANIM_ON : LV_ANIM_OFF);
}

static void spec_arrow_cb(lv_event_t *e)
{
  FAR const int *dp = lv_event_get_user_data(e);

  spec_scroll_to(g_s.idx + *dp, true);
}

static void spec_scroll_end_cb(lv_event_t *e)
{
  int idx = (int)((lv_obj_get_scroll_x(g_s.scroller) + PAGE_W / 2) /
                  PAGE_W);

  spec_set_page(idx);
}

/****************************************************************************
 * Name: spec_analyze
 *
 * Description:
 *   对当前窗口做 FFT 分析并刷新频谱图/主频值卡。
 *
 ****************************************************************************/

/* 分析工作缓冲（静态：main 任务栈 16KB，避免大帧深度） */
static float spec_work[SPEC_MAX_FFT];
static float spec_mag[SPEC_BINS];

static void spec_analyze(void)
{
  float mean;
  float maxv = 0.0f;
  int best_ch = -1;
  int ch;
  int i;
  int k;

  if (g_s.n < g_s.fft_n)
    {
      return;
    }

  k = g_s.n - g_s.fft_n;   /* 用最近 fft_n 个样本 */

  for (ch = 0; ch < g_s.nch; ch++)
    {
      mean = 0.0f;
      for (i = 0; i < g_s.fft_n; i++)
        {
          spec_work[i] = g_s.ch[ch][k + i];
          mean += spec_work[i];
        }

      mean /= (float)g_s.fft_n;

      /* 去均值 + Hann 窗（抑制频谱泄漏） */

      for (i = 0; i < g_s.fft_n; i++)
        {
          float w = 0.5f - 0.5f * cosf(2.0f * (float)M_PI * (float)i /
                                       (float)(g_s.fft_n - 1));

          spec_work[i] = (spec_work[i] - mean) * w;
        }

      memset(spec_mag, 0, sizeof(spec_mag));
      pw_fft_magnitude(spec_work, (unsigned)g_s.fft_n, spec_mag);

      /* 主频（跳过直流，抛物线插值） */
      g_s.fpeak[ch] = pw_fft_dominant_freq(spec_mag, g_s.fft_n / 2 + 1,
                                           g_s.fs, (unsigned)g_s.fft_n);

      /* 暂存原始幅度谱（先找全局最大值再归一化） */
      for (i = 0; i < g_s.fft_n / 2 + 1; i++)
        {
          g_s.fmag[ch][i] = spec_mag[i];
          if (i > 0 && spec_mag[i] > maxv)
            {
              maxv = spec_mag[i];
            }
        }
    }

  if (maxv <= 0.0f)
    {
      spec_update_status(PW_STR(SPEC_STATUS_WEAK), PW_COL_DIM);
      return;
    }

  /* 频率轴 + 归一化 */

  for (i = 0; i < g_s.fft_n / 2 + 1; i++)
    {
      g_s.fx[i] = g_s.fs * (float)i / (float)g_s.fft_n;
      for (ch = 0; ch < g_s.nch; ch++)
        {
          g_s.fmag[ch][i] /= maxv;
        }
    }

  g_s.gmax = maxv;

  /* 最强通道的主频 → 大号值卡 */

  for (ch = 0; ch < g_s.nch; ch++)
    {
      if (g_s.fpeak[ch] > 0.0f &&
          (best_ch < 0 || g_s.fpeak[ch] > g_s.fpeak[best_ch]))
        {
          best_ch = ch;
        }
    }

  if (best_ch >= 0)
    {
      lv_label_set_text_fmt(g_s.big_lab, "%.1f",
                            (double)g_s.fpeak[best_ch]);
      lv_obj_set_style_text_color(g_s.big_lab, S_ACC, 0);
      g_s.have = 1;
    }
  else
    {
      lv_label_set_text(g_s.big_lab, "--");
      lv_obj_set_style_text_color(g_s.big_lab, PW_COL_DIM, 0);
      g_s.have = 0;
    }

  /* 各通道主频行（accel: X/Y/Z；mic: 单通道） */

  if (g_s.nch == 3)
    {
      lv_label_set_text_fmt(g_s.peak_row,
                            "X %5.1f  Y %5.1f  Z %5.1f Hz",
                            (double)g_s.fpeak[0],
                            (double)g_s.fpeak[1],
                            (double)g_s.fpeak[2]);
    }
  else
    {
      lv_label_set_text_fmt(g_s.peak_row, "peak %.1f Hz",
                            (double)g_s.fpeak[0]);
    }

  /* 多序列频谱图（仅当前页可见时刷新，省 CPU） */

  if (g_s.idx == 0)
    {
      pw_graph_begin(g_s.graph);
      for (ch = 0; ch < g_s.nch; ch++)
        {
          lv_color_t c = (ch == 0) ? lv_color_hex(0x4fc3f7) :
                         (ch == 1) ? lv_color_hex(0x64b5f6) :
                                     lv_color_hex(0x81c784);

          if (g_s.kind == SPEC_KIND_MIC)
            {
              c = lv_color_hex(0xffb74d);
            }
          else if (g_s.kind == SPEC_KIND_MAG)
            {
              c = (ch == 0) ? lv_color_hex(0xaed581) :
                  (ch == 1) ? lv_color_hex(0x81c784) :
                              lv_color_hex(0xffb74d);
            }

          pw_graph_add_series(g_s.graph, g_s.fx, g_s.fmag[ch],
                              g_s.fft_n / 2 + 1, c);
        }

      pw_graph_end(g_s.graph);
    }

  spec_update_status(PW_STR(SPEC_STATUS_LIVE), S_ACC);
}

/****************************************************************************
 * Name: spec_wave_push
 *
 * Description:
 *   波形页推点：accel=|a|-1g（g），mic=原始采样归一化。
 *
 ****************************************************************************/

static void spec_wave_push(void)
{
  float v;

  if (g_s.n < 1)
    {
      return;   /* 尚无样本（mag 分频首拍） */
    }

  if (g_s.kind == SPEC_KIND_ACC)
    {
      float ax = g_s.ch[0][g_s.n - 1];
      float ay = g_s.ch[1][g_s.n - 1];
      float az = g_s.ch[2][g_s.n - 1];
      float m = sqrtf(ax * ax + ay * ay + az * az) / 1000.0f - 1.0f;

      v = m / 0.5f;   /* ±0.5g 满量程 */
    }
  else if (g_s.kind == SPEC_KIND_MAG)
    {
      float bx = g_s.ch[0][g_s.n - 1];
      float by = g_s.ch[1][g_s.n - 1];
      float bz = g_s.ch[2][g_s.n - 1];
      float m = sqrtf(bx * bx + by * by + bz * bz);   /* mG */

      g_s.ema = g_s.ema * 0.99f + m * 0.01f;
      v = (m - g_s.ema) / 200.0f;   /* ±200mG 满量程（去地磁直流漂移） */
    }
  else
    {
      v = g_s.ch[0][g_s.n - 1] / 0.5f;   /* ±0.5 满量程 */
    }

  if (v > 1.0f)
    {
      v = 1.0f;
    }
  else if (v < -1.0f)
    {
      v = -1.0f;
    }

  if (g_s.wn >= SPEC_WAVE_N)
    {
      memmove(g_s.wbuf, g_s.wbuf + 1, (SPEC_WAVE_N - 1) * sizeof(float));
      g_s.wbuf[SPEC_WAVE_N - 1] = v;
    }
  else
    {
      g_s.wbuf[g_s.wn++] = v;
    }

  /* 波形（pw_scope）：页1可见时每 2 拍(40ms)整段重画 */
  if (g_s.idx == 1 && ++g_s.wdiv >= 2)
    {
      g_s.wdiv = 0;
      pw_scope_set_data(g_s.wave, g_s.wbuf, g_s.wn);
    }
}

/****************************************************************************
 * Name: spec_tick_cb
 *
 * Description:
 *   20ms LVGL 定时器：采集入窗 + 节流分析 + 波形刷新。
 *
 ****************************************************************************/

/* 合成一个麦克风采样块（16kHz，440Hz+2kHz 多音），模拟 read() 返回 */
static void spec_mic_bench_block(long block)
{
  float t0 = (float)block * 0.064f;   /* 每块 64ms（1024/16000） */
  int i;

  for (i = 0; i < MIC_FFT_N; i++)
    {
      float t = t0 + (float)i / MIC_FS;
      float v = 12000.0f * sinf(2.0f * (float)M_PI * 440.0f * t) +
                8000.0f * sinf(2.0f * (float)M_PI * 2000.0f * t) +
                400.0f * sinf(2.0f * (float)M_PI * 513.0f * t);

      g_mic_data[i] = (int16_t)v;
    }

  g_mic_ready = 1;
}

static void spec_tick_cb(lv_timer_t *timer)
{
  if (lv_screen_active() != g_s.scr)
    {
      return;
    }

  if (g_bench_on)
    {
      if (g_s.kind == SPEC_KIND_ACC)
        {
          /* 合成多音：2.3Hz + 6.1Hz + 11.7Hz（X 轴），
           * Y/Z 轴各带不同主频，验证多序列着色与主频检测 */
          float t = (float)g_bench_sample * TICK_MS / 1000.0f;
          float v0 = 300.0f * sinf(2.0f * (float)M_PI * 2.3f * t) +
                     150.0f * sinf(2.0f * (float)M_PI * 6.1f * t) +
                     30.0f * sinf(2.0f * (float)M_PI * 11.7f * t);
          float v1 = 200.0f * sinf(2.0f * (float)M_PI * 4.0f * t) +
                     25.0f * sinf(2.0f * (float)M_PI * 9.3f * t);
          float v2 = 120.0f * sinf(2.0f * (float)M_PI * 7.7f * t);

          g_s.ch[0][g_s.n] = 1000.0f + v0;   /* +1g 静态重力 */
          g_s.ch[1][g_s.n] = v1;
          g_s.ch[2][g_s.n] = v2;
          g_s.n++;
        }
      else if (g_s.kind == SPEC_KIND_MAG)
        {
          /* 合成：0.7Hz + 2.2Hz 磁振荡（±200mG）叠加 600mG 静态地磁 */
          if (++g_s.sdiv >= MAG_SAMPLE_TK)
            {
              float t = (float)g_bench_sample * TICK_MS / 1000.0f;

              g_s.sdiv = 0;
              g_s.ch[0][g_s.n] = 600.0f +
                200.0f * sinf(2.0f * (float)M_PI * 0.7f * t) +
                100.0f * sinf(2.0f * (float)M_PI * 2.2f * t);
              g_s.ch[1][g_s.n] = 150.0f +
                80.0f * sinf(2.0f * (float)M_PI * 1.5f * t);
              g_s.ch[2][g_s.n] = -300.0f +
                120.0f * sinf(2.0f * (float)M_PI * 3.4f * t);
              g_s.n++;
            }
        }
      else
        {
          /* 每 10 拍(200ms)合成一整块 16kHz 数据（模拟 mic 线程） */
          if ((g_bench_sample % 10) == 0)
            {
              spec_mic_bench_block(g_bench_sample / 10);
            }
        }

      g_bench_sample++;

      if (g_bench_interval > 0 &&
          (g_bench_sample % (long)(g_bench_interval * 50)) == 0)
        {
          spec_scroll_to((g_s.idx + 1) % SPEC_PAGES, false);
        }
    }
  else if (g_s.kind == SPEC_KIND_ACC)
    {
      struct pw_imu_s imu;

      if (pw_sensors_read_imu(&imu) < 0)
        {
          return;
        }

      g_s.ch[0][g_s.n] = (float)imu.ax;   /* mg */
      g_s.ch[1][g_s.n] = (float)imu.ay;
      g_s.ch[2][g_s.n] = (float)imu.az;
      g_s.n++;
    }
  else if (g_s.kind == SPEC_KIND_MAG)
    {
      struct pw_mag_s mag;

      if (++g_s.sdiv >= MAG_SAMPLE_TK)
        {
          g_s.sdiv = 0;
          if (pw_sensors_read_mag(&mag) < 0)
            {
              return;
            }

          g_s.ch[0][g_s.n] = (float)mag.x;   /* mG */
          g_s.ch[1][g_s.n] = (float)mag.y;
          g_s.ch[2][g_s.n] = (float)mag.z;
          g_s.n++;
        }
    }

  if (g_s.kind == SPEC_KIND_MIC)
    {
      /* mic：后台线程已把 1024 样本写入 g_mic_data（bench 亦然） */
      if (g_mic_ready)
        {
          int i;

          for (i = 0; i < MIC_FFT_N; i++)
            {
              g_s.ch[0][i] = (float)g_mic_data[i] / 32768.0f;
            }

          g_s.n = MIC_FFT_N;
          g_mic_ready = 0;
        }
      else
        {
          return;   /* 无新块 */
        }
    }

  /* 滚动窗口（满后每拍丢最旧 1 点） */
  while (g_s.n > g_s.fft_n)
    {
      int ch;

      for (ch = 0; ch < g_s.nch; ch++)
        {
          memmove(g_s.ch[ch], g_s.ch[ch] + 1,
                  (g_s.fft_n - 1) * sizeof(float));
        }

      g_s.n--;
    }

  spec_wave_push();

  /* 节流分析（各源不同节奏） */
  {
    int every = (g_s.kind == SPEC_KIND_ACC) ? ACC_ANALYZE_TK :
                (g_s.kind == SPEC_KIND_MAG) ? MAG_ANALYZE_TK :
                                              MIC_RENDER_TK;

    if (++g_s.div >= every)
      {
        g_s.div = 0;
        spec_analyze();
      }
  }
}

/****************************************************************************
 * Name: mic_thread
 *
 * Description:
 *   麦克风连续采集线程：read 阻塞 ~64ms，不进 LVGL 定时器以免卡 UI。
 *
 ****************************************************************************/

static void *mic_thread(void *arg)
{
  int fd;

  fd = open("/dev/mic0", O_RDONLY);
  if (fd < 0)
    {
      g_mic_run = 0;
      return NULL;
    }

  while (g_mic_run)
    {
      ssize_t r = read(fd, g_mic_data, sizeof(g_mic_data));

      if (r == (ssize_t)sizeof(g_mic_data))
        {
          g_mic_ready = 1;
        }
      else
        {
          usleep(10 * 1000);
        }
    }

  close(fd);
  return NULL;
}

/****************************************************************************
 * Name: mic_thread_start / mic_thread_stop
 ****************************************************************************/

static void mic_thread_start(void)
{
  if (g_mic_run)
    {
      return;
    }

  g_mic_run = 1;
  g_mic_ready = 0;
  if (pthread_create(&g_mic_tid, NULL, mic_thread, NULL) != 0)
    {
      g_mic_run = 0;
    }
}

static void mic_thread_stop(void)
{
  g_mic_run = 0;
}

/****************************************************************************
 * Name: spec_del_cb
 *
 * Description:
 *   屏幕删除时停止麦克风线程。
 *
 ****************************************************************************/

static void spec_del_cb(lv_event_t *e)
{
  if (g_s.kind == SPEC_KIND_MIC)
    {
      mic_thread_stop();
    }
}

/****************************************************************************
 * (migrated: graph controls use shared pw_graph_controls_add)
 ****************************************************************************/

/****************************************************************************
 * Name: spec_gctl_status
 *
 * Description:
 *   图控条范围回传 → 状态行（共用 pw_graph_controls_add）。
 ****************************************************************************/

static void spec_gctl_status(void *ud, const char *msg)
{
  (void)ud;
  spec_update_status(msg, PW_COL_DIM);
}

/****************************************************************************
 * Private page builders
 ****************************************************************************/

/****************************************************************************
 * Name: spec_build_page_spectrum
 ****************************************************************************/

static void spec_build_page_spectrum(void)
{
  lv_obj_t *pg = lv_obj_create(g_s.scroller);
  lv_obj_t *lab;
  lv_obj_t *card;

  lv_obj_set_size(pg, PAGE_W, PAGE_H);
  lv_obj_set_pos(pg, 0, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  lab = pw_label_new(pg, PW_STR(SPEC_TITLE), PW_FNT_MED, S_ACC);
  lv_obj_set_pos(lab, 20, 4);

  lab = pw_label_new(pg, PW_STR(SPEC_MAG_NORM),
                     PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 150, 8);

  /* 主频值卡 */

  card = pw_card_new(pg, 354, 64, PW_COL_CARD);
  lv_obj_set_pos(card, 18, 26);

  lab = pw_label_new(card, PW_STR(SPEC_DOMINANT), PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 14, 8);

  g_s.big_lab = pw_label_new(card, "--", PW_FNT_XXL, S_ACC);
  lv_obj_set_pos(g_s.big_lab, 130, 2);

  lab = pw_label_new(card, "Hz", PW_FNT_MED, PW_COL_DIM);
  lv_obj_set_pos(lab, 300, 20);

  g_s.peak_row = pw_label_new(card, PW_STR(SPEC_XYZ_HINT),
                              PW_FNT_SMALL, PW_COL_TEXT);
  lv_obj_set_pos(g_s.peak_row, 14, 42);

  /* 频谱图卡 */

  card = pw_card_new(pg, 354, 246, PW_COL_CARD);
  lv_obj_set_pos(card, 18, 96);

  lab = pw_label_new(card, PW_STR(AXIS_Y_REL_MAG), PW_FNT_BODY,
                     PW_COL_DIM);
  lv_obj_set_pos(lab, 12, 4);

  g_s.graph = pw_graph_create(card, 330, 170, S_ACC, PW_COL_CARD, 0);
  lv_obj_set_pos(pw_graph_obj(g_s.graph), 12, 20);
  pw_graph_controls_add(card, g_s.graph, spec_gctl_status, NULL);

  if (g_s.kind == SPEC_KIND_ACC)
    {
      lab = pw_label_new(card, PW_STR(AXIS_X_FREQ_25),
                         PW_FNT_BODY, PW_COL_FAINT);
    }
  else if (g_s.kind == SPEC_KIND_MAG)
    {
      lab = pw_label_new(card, PW_STR(AXIS_X_FREQ_12_5),
                         PW_FNT_BODY, PW_COL_FAINT);
    }
  else
    {
      lab = pw_label_new(card, PW_STR(AXIS_X_FREQ_8K),
                         PW_FNT_BODY, PW_COL_FAINT);
    }

  lv_obj_set_pos(lab, 12, 228);
}

/****************************************************************************
 * Name: spec_build_page_wave
 ****************************************************************************/

static void spec_build_page_wave(void)
{
  lv_obj_t *pg = lv_obj_create(g_s.scroller);
  lv_obj_t *lab;
  lv_obj_t *card;

  lv_obj_set_size(pg, PAGE_W, PAGE_H);
  lv_obj_set_pos(pg, PAGE_W, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  lab = pw_label_new(pg, PW_STR(SPEC_WAVEFORM), PW_FNT_MED, S_ACC);
  lv_obj_set_pos(lab, 20, 4);

  if (g_s.kind == SPEC_KIND_ACC)
    {
      lab = pw_label_new(pg, PW_STR(SPEC_ACC_MINUS_G),
                         PW_FNT_BODY, PW_COL_FAINT);
      lv_obj_set_pos(lab, 160, 8);

      card = pw_card_new(pg, 354, 200, PW_COL_CARD);
      lv_obj_set_pos(card, 18, 60);

      lab = pw_label_new(card, PW_STR(SPEC_Y_05G), PW_FNT_BODY, PW_COL_DIM);
      lv_obj_set_pos(lab, 12, 6);

      g_s.wave = pw_scope_create(card, 330, 150, PW_ACC_ACC, PW_COL_CARD);
      lv_obj_set_pos(g_s.wave, 12, 24);
      pw_scope_axes(g_s.wave, PW_COL_FAINT);

      lab = pw_label_new(card, PW_STR(LAST_2_4S), PW_FNT_BODY, PW_COL_FAINT);
      lv_obj_set_pos(lab, 12, 182);
    }
  else if (g_s.kind == SPEC_KIND_MAG)
    {
      lab = pw_label_new(pg, PW_STR(SPEC_MAG_MINUS_DRIFT), PW_FNT_BODY, PW_COL_FAINT);
      lv_obj_set_pos(lab, 160, 8);

      card = pw_card_new(pg, 354, 200, PW_COL_CARD);
      lv_obj_set_pos(card, 18, 60);

      lab = pw_label_new(card, PW_STR(SPEC_Y_200MG), PW_FNT_BODY, PW_COL_DIM);
      lv_obj_set_pos(lab, 12, 6);

      g_s.wave = pw_scope_create(card, 330, 150, PW_ACC_MAG, PW_COL_CARD);
      lv_obj_set_pos(g_s.wave, 12, 24);
      pw_scope_axes(g_s.wave, PW_COL_FAINT);

      lab = pw_label_new(card, PW_STR(LAST_2_4S), PW_FNT_BODY, PW_COL_FAINT);
      lv_obj_set_pos(lab, 12, 182);
    }
  else
    {
      lab = pw_label_new(pg, PW_STR(SPEC_RAW_MIC), PW_FNT_BODY, PW_COL_FAINT);
      lv_obj_set_pos(lab, 170, 8);

      card = pw_card_new(pg, 354, 200, PW_COL_CARD);
      lv_obj_set_pos(card, 18, 60);

      lab = pw_label_new(card, PW_STR(SPEC_Y_FULLSCALE), PW_FNT_BODY,
                         PW_COL_DIM);
      lv_obj_set_pos(lab, 12, 6);

      g_s.wave = pw_scope_create(card, 330, 150, PW_ACC_ACOU, PW_COL_CARD);
      lv_obj_set_pos(g_s.wave, 12, 24);
      pw_scope_axes(g_s.wave, PW_COL_FAINT);

      lab = pw_label_new(card, PW_STR(SPEC_BLOCK_STREAM),
                         PW_FNT_BODY, PW_COL_FAINT);
      lv_obj_set_pos(lab, 12, 182);
    }
}

/****************************************************************************
 * Name: spec_build_page_help
 ****************************************************************************/

static void spec_build_page_help(void)
{
  lv_obj_t *pg = lv_obj_create(g_s.scroller);
  lv_obj_t *lab;
  lv_obj_t *card;

  lv_obj_set_size(pg, PAGE_W, PAGE_H);
  lv_obj_set_pos(pg, PAGE_W * 2, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  lab = pw_label_new(pg, PW_STR(HELP), PW_FNT_MED, S_ACC);
  lv_obj_set_pos(lab, 20, 4);

  card = pw_card_new(pg, 354, 280, PW_COL_CARD);
  lv_obj_set_pos(card, 18, 40);

  if (g_s.kind == SPEC_KIND_ACC)
    {
      lab = pw_label_new(card, PW_STR(SPEC_HELP_ACCEL),
        PW_FNT_BODY, PW_COL_DIM);
    }
  else if (g_s.kind == SPEC_KIND_MAG)
    {
      lab = pw_label_new(card, PW_STR(SPEC_HELP_MAG),
        PW_FNT_BODY, PW_COL_DIM);
    }
  else
    {
      lab = pw_label_new(card,
        "Whistle, hum, or tap near the microphone. The spectrum "
        "shows the frequency content of the sound.\n"
        "Dominant frequency is shown in the value card.\n"
        "Drag the plot to pan, use the buttons below it to zoom.\n"
        "Mic: 16 kHz, window 1024 samples = 64 ms, resolution "
        "15.6 Hz, range 0..8 kHz.",
        PW_FNT_BODY, PW_COL_DIM);
    }

  lv_obj_set_pos(lab, 14, 10);
  lv_obj_set_width(lab, 326);
  lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pw_spec_bench
 ****************************************************************************/

void pw_spec_bench(int on, int page_interval_s, int start_page)
{
  g_bench_on = on;
  g_bench_interval = page_interval_s;
  g_bench_sample = 0;

  if (on && start_page > 0 && g_s.scroller != NULL)
    {
      spec_scroll_to(start_page % SPEC_PAGES, false);
    }
}

/****************************************************************************
 * Name: spec_screen_common
 *
 * Description:
 *   构建频谱实验屏（accel/mic 共用骨架）。
 *
 ****************************************************************************/

static lv_obj_t *spec_screen_common(int kind, const char *title)
{
  lv_obj_t *btn;
  lv_obj_t *lab;
  int i;

  memset(&g_s, 0, sizeof(g_s));
  g_s.kind = kind;

  if (kind == SPEC_KIND_ACC)
    {
      g_s.nch = 3;
      g_s.fft_n = ACC_FFT_N;
      g_s.fs = ACC_FS;
    }
  else if (kind == SPEC_KIND_MAG)
    {
      g_s.nch = 3;
      g_s.fft_n = MAG_FFT_N;
      g_s.fs = MAG_FS;
    }
  else
    {
      g_s.nch = 1;
      g_s.fft_n = MIC_FFT_N;
      g_s.fs = MIC_FS;
    }

  g_s.scr = pw_scr_new();
  lv_obj_add_event_cb(g_s.scr, spec_del_cb, LV_EVENT_DELETE, NULL);

  g_s.scroller = pw_topbar(g_s.scr, title);
  lv_obj_set_size(g_s.scroller, PAGE_W, PAGE_H);
  lv_obj_set_scroll_dir(g_s.scroller, LV_DIR_HOR);
  lv_obj_set_scroll_snap_x(g_s.scroller, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scrollbar_mode(g_s.scroller, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(g_s.scroller, LV_OBJ_FLAG_SCROLL_ONE);
  lv_obj_add_event_cb(g_s.scroller, spec_scroll_end_cb,
                      LV_EVENT_SCROLL_END, NULL);

  spec_build_page_spectrum();
  spec_build_page_wave();
  spec_build_page_help();

  /* 底部条：状态行 + 箭头 + 圆点 */

  g_s.status = pw_label_new(g_s.scr,
                            (kind == SPEC_KIND_ACC) ?
                            PW_STR(SPEC_HINT_ACCEL) :
                            (kind == SPEC_KIND_MAG) ?
                            PW_STR(SPEC_HINT_MAG) :
                            PW_STR(SPEC_HINT_MIC),
                            PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(g_s.status, 80, 398);
  lv_obj_set_width(g_s.status, 230);
  lv_obj_set_style_text_align(g_s.status, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(g_s.status, LV_LABEL_LONG_WRAP);

  btn = pw_card_new(g_s.scr, 64, 44, PW_COL_CARD);
  lv_obj_set_pos(btn, 4, 396);
  {
    static const int dm = -1;
    lv_obj_add_event_cb(btn, spec_arrow_cb, LV_EVENT_CLICKED, (void *)&dm);
    lab = pw_label_new(btn, "<", PW_FNT_XL, PW_COL_DIM);
    lv_obj_center(lab);
  }

  for (i = 0; i < SPEC_PAGES; i++)
    {
      lv_obj_t *d = lv_obj_create(g_s.scr);
      lv_obj_set_size(d, 12, 12);
      lv_obj_set_style_bg_color(d, PW_COL_CARD_LT, 0);
      lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(d, 0, 0);
      lv_obj_remove_flag(d, LV_OBJ_FLAG_SCROLLABLE);
      g_s.dots[i] = d;
      lv_obj_align(d, LV_ALIGN_BOTTOM_MID, (i - (SPEC_PAGES - 1) / 2) * 18,
                   -24);
    }

  btn = pw_card_new(g_s.scr, 64, 44, PW_COL_CARD);
  lv_obj_set_pos(btn, PAGE_W - 4 - 64, 396);
  {
    static const int dp = 1;
    lv_obj_add_event_cb(btn, spec_arrow_cb, LV_EVENT_CLICKED, (void *)&dp);
    lab = pw_label_new(btn, ">", PW_FNT_XL, PW_COL_DIM);
    lv_obj_center(lab);
  }

  spec_set_page(0);

  /* 连续采集 + 节流分析（屏幕删除自动释放） */

  pw_scr_set_tick(g_s.scr, spec_tick_cb, TICK_MS);

  return g_s.scr;
}

lv_obj_t *pw_spec_accel_screen(void)
{
  return spec_screen_common(SPEC_KIND_ACC, PW_STR(SPEC_ACCEL_TITLE));
}

lv_obj_t *pw_spec_mic_screen(void)
{
  lv_obj_t *scr = spec_screen_common(SPEC_KIND_MIC, PW_STR(SPEC_TITLE));

  if (!g_bench_on)
    {
      mic_thread_start();
    }

  return scr;
}

lv_obj_t *pw_spec_mag_screen(void)
{
  return spec_screen_common(SPEC_KIND_MAG, PW_STR(SPEC_MAG_TITLE));
}
