/****************************************************************************
 * apps/examples/phywear/phywear_pend.c
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

/* 摆测实验（Pendulum）—— phyphox 对齐：连续流式 + 实验内多页。
 *
 * 数据层（与 phyphox pendulum 相同）：
 *   - 进页即 50Hz 连续采集（陀螺仪三轴和，12s 滑动窗口），无 START；
 *   - 每 ~300ms 周期分析一次：自相关（首峰粗估+末谐波峰精化）为主，
 *     FFT 主频为兜底；数值实时刷新，停摆自动归 "--"。
 *
 * 页面（横滑 + 圆点 + 箭头）：
 *   0 Measure g     —— 输摆长 L → 实时 g（4π²L/T²），附实时波形
 *   1 Measure L     —— 设 g=9.81 → 实时反推摆长 L（phyphox Length 页）
 *   2 Autocorr      —— 当前窗口自相关曲线 + T（教学：看周期怎么来的）
 *
 * 性能结论（2026-09-03 实测）：
 *   - LVGL 的 SiFli EPIC draw unit 只加速 FILL/BORDER/IMAGE/LABEL/LAYER，
 *     不含 LINE；lv_chart 折线走软件渲染，本板逐帧重绘极贵（300 点任何
 *     刷新方式都崩塌、96 点也只到 ~4fps）。
 *   - 解法：自研 pw_scope（CPU 光栅化曲线到 RGB565 缓冲 → lv_image 呈现
 *     → EPIC IMAGE 任务硬件 blit）。实测 300 点@25fps 波形 + 251 点@10Hz
 *     自相关均流畅（页0 ~90 loops/2s、其余 ~160），无雪崩。
 *   - 后续所有实时曲线（频谱等）复用 pw_scope，勿用 lv_chart。
 *
 * Resonance（受迫振动扫频曲线）需可控扫频驱动，手表手工难复现，
 * 留待后续（honest planned，不入本页）。
 */

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <math.h>

#include <lvgl/lvgl.h>

#include "phywear_ui.h"
#include "phywear_sensors.h"
#include "pw_analysis.h"
#include "pw_scope.h"
#include "pw_graph.h"
#include "phywear_pend.h"
#include "phywear_i18n.h"

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define PEND_DT         0.02f          /* 采样间隔 s（~50Hz） */
#define WIN_SAMPLES     600            /* 分析窗口 = 12s */
#define CHART_POINTS    120            /* 波形图点数 = 2.4s 窗口（50Hz） */

#define AC_MAXLAG       250            /* 自相关曲线 0..5s */
#define RES_MAX         64             /* 共振历史点数 */

#define PEND_L_MIN      0.05f
#define PEND_L_MAX      5.0f
#define PEND_L_STEP     0.05f

#define TICK_SAMPLE_MS  20
#define ANALYSIS_EVERY  15             /* 每 15 拍(300ms) 周期分析一次 */

#define MOTION_SAMPLES  40             /* 用最近 0.8s 判断是否在摆 */
#define MOTION_STD      180.0f         /* 标准差阈值（mdps） */

#define MIN_ANALYZE     80             /* 窗口 <1.6s 不分析 */

#define PEND_PAGES      4
#define PAGE_W          390
#define PAGE_H          342            /* 450 - 顶栏54 - 底部条54 */

#define G_ACC           PW_ACC_MECH    /* 强调色 */
#define G_STATIC_G      9.81f

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct pend_ui_s
{
  lv_obj_t *scr;
  lv_obj_t *scroller;
  int       idx;                       /* 当前页 0..2 */
  lv_obj_t *dots[PEND_PAGES];

  /* 页 0：Measure g */
  lv_obj_t *len_lab;
  lv_obj_t *len_minus;
  lv_obj_t *len_plus;
  lv_obj_t *wave;                       /* 页0 实时波形（pw_scope） */
  float       ema;
  float       scope[CHART_POINTS];
  int         scope_n;
  uint32_t    wave_div;
  lv_obj_t *g_lab;
  lv_obj_t *tf_lab;

  /* 页 1：Measure L */
  lv_obj_t *l_lab;

  /* 页 2：Autocorrelation */
  lv_obj_t *ac_lab;                    /* T 数值行 */
  struct pw_graph_s *ac_graph;         /* 自相关曲线（可缩放） */
  float       ac_lag[AC_MAXLAG + 1];   /* 自相关横轴：时间位移 s */
  uint32_t    ac_div;

  /* 页 3：Resonance（散点历史） */
  struct pw_graph_s *res_graph;
  float     res_freq[RES_MAX];
  float     res_amp[RES_MAX];
  int       res_n;
  float     res_maxamp;

  lv_obj_t *status;                  /* 底部公共状态行 */

  float       L;
  float       buf[WIN_SAMPLES];        /* 滑动窗口（旧在前） */
  int         n;
  int         tick;
  int         have_val;
  int         bad_streak;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct pend_ui_s g_p;

/* bench 控制（见 phywear_pend.h） */
static int  g_bench_on;
static int  g_bench_interval;   /* 翻页间隔（样本拍数*20ms），0=不翻 */
static long g_bench_sample;     /* 采样序号（合成信号相位） */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pend_set_length
 ****************************************************************************/

static void pend_set_length(float L)
{
  if (L < PEND_L_MIN)
    {
      L = PEND_L_MIN;
    }

  if (L > PEND_L_MAX)
    {
      L = PEND_L_MAX;
    }

  g_p.L = L;
  lv_label_set_text_fmt(g_p.len_lab, "%.2f", (double)L);
}

/****************************************************************************
 * Name: pend_len_step_cb
 ****************************************************************************/

static void pend_len_step_cb(lv_event_t *e)
{
  FAR const int *dp = lv_event_get_user_data(e);

  pend_set_length(g_p.L + (*dp) * PEND_L_STEP);
}

/****************************************************************************
 * Name: pend_len_preset_cb
 ****************************************************************************/

static void pend_len_preset_cb(lv_event_t *e)
{
  FAR const float *pv = lv_event_get_user_data(e);

  pend_set_length(*pv);
}

/****************************************************************************
 * Name: pend_sync_dots
 ****************************************************************************/

static void pend_sync_dots(void)
{
  int i;

  for (i = 0; i < PEND_PAGES; i++)
    {
      lv_obj_set_style_bg_color(g_p.dots[i],
                                (i == g_p.idx) ? G_ACC : PW_COL_CARD_LT, 0);
    }
}

/****************************************************************************
 * Name: pend_set_page
 ****************************************************************************/

static void pend_set_page(int idx)
{
  if (idx < 0)
    {
      idx = 0;
    }

  if (idx >= PEND_PAGES)
    {
      idx = PEND_PAGES - 1;
    }

  g_p.idx = idx;
  pend_sync_dots();
}

/****************************************************************************
 * Name: pend_scroll_to
 ****************************************************************************/

static void pend_scroll_to(int idx, bool anim)
{
  if (idx < 0 || idx >= PEND_PAGES)
    {
      return;
    }

  pend_set_page(idx);
  lv_obj_scroll_to_x(g_p.scroller, idx * PAGE_W,
                     anim ? LV_ANIM_ON : LV_ANIM_OFF);
}

/****************************************************************************
 * Name: pend_arrow_cb
 ****************************************************************************/

static void pend_arrow_cb(lv_event_t *e)
{
  FAR const int *dp = lv_event_get_user_data(e);

  pend_scroll_to(g_p.idx + *dp, true);
}

/****************************************************************************
 * Name: pend_scroll_end_cb
 ****************************************************************************/

static void pend_scroll_end_cb(lv_event_t *e)
{
  int idx = (int)((lv_obj_get_scroll_x(g_p.scroller) + PAGE_W / 2) /
                  PAGE_W);

  pend_set_page(idx);
}

/****************************************************************************
 * Name: pend_stddev_recent
 ****************************************************************************/

static float pend_stddev_recent(void)
{
  float mean = 0.0f;
  float var = 0.0f;
  int k = g_p.n < MOTION_SAMPLES ? g_p.n : MOTION_SAMPLES;
  int i;

  if (k < 8)
    {
      return 0.0f;
    }

  for (i = g_p.n - k; i < g_p.n; i++)
    {
      mean += g_p.buf[i];
    }

  mean /= (float)k;

  for (i = g_p.n - k; i < g_p.n; i++)
    {
      float d = g_p.buf[i] - mean;
      var += d * d;
    }

  return sqrtf(var / (float)k);
}

/****************************************************************************
 * Name: pend_period_fft_fallback
 ****************************************************************************/

static float pend_period_fft_fallback(void)
{
  float mag[WIN_SAMPLES / 2 + 1];
  float det[WIN_SAMPLES];
  float mean = 0.0f;
  float f;
  unsigned nb;
  int i;

  for (i = 0; i < g_p.n; i++)
    {
      mean += g_p.buf[i];
    }

  mean /= (float)g_p.n;

  for (i = 0; i < g_p.n; i++)
    {
      det[i] = g_p.buf[i] - mean;
    }

  nb = pw_fft_magnitude(det, g_p.n, mag);
  f  = pw_fft_dominant_freq(mag, nb, 1.0f / PEND_DT, g_p.n);

  if (f < 0.2f || f > 3.0f)
    {
      return 0.0f;
    }

  return 1.0f / f;
}

/****************************************************************************
 * Name: pend_update_status
 ****************************************************************************/

static void pend_update_status(const char *txt, lv_color_t col)
{
  lv_label_set_text(g_p.status, txt);
  lv_obj_set_style_text_color(g_p.status, col, 0);
}

/****************************************************************************
 * Name: pend_blank_result
 ****************************************************************************/

static void pend_blank_result(void)
{
  lv_label_set_text(g_p.g_lab, "--");
  lv_obj_set_style_text_color(g_p.g_lab, PW_COL_DIM, 0);
  lv_label_set_text(g_p.l_lab, "--");
  lv_obj_set_style_text_color(g_p.l_lab, PW_COL_DIM, 0);
  lv_label_set_text(g_p.tf_lab, PW_STR(PEND_TF_HINT));
  lv_label_set_text(g_p.ac_lab, "T --");
}

/****************************************************************************
 * Name: pend_fill_ac_curve
 *
 * Description:
 *   计算当前窗口自相关并刷新页 2 曲线（归一化到 ac[0]）。
 *
 ****************************************************************************/

static void pend_fill_ac_curve(void)
{
  float det[WIN_SAMPLES];
  float ac[AC_MAXLAG + 1];
  float norm[AC_MAXLAG + 1];
  float mean = 0.0f;
  float a0;
  unsigned i;

  if (g_p.n < MIN_ANALYZE)
    {
      return;
    }

  for (i = 0; i < (unsigned)g_p.n; i++)
    {
      mean += g_p.buf[i];
    }

  mean /= (float)g_p.n;

  for (i = 0; i < (unsigned)g_p.n; i++)
    {
      det[i] = g_p.buf[i] - mean;
    }

  pw_autocorr(det, g_p.n, ac, AC_MAXLAG);
  a0 = ac[0];
  if (a0 <= 0.0f)
    {
      return;
    }

  for (i = 0; i <= AC_MAXLAG; i++)
    {
      float lag = (float)i * PEND_DT;

      g_p.ac_lag[i] = lag;
      /* ac[i]/ac[0] 已是 [-1,1] 的衰减振荡，直接交图（勿再缩放） */
      norm[i] = ac[i] / a0;
    }

  pw_graph_set_data(g_p.ac_graph, g_p.ac_lag, norm, AC_MAXLAG + 1);
}

/****************************************************************************
 * Name: pend_analyze
 *
 * Description:
 *   对当前滑动窗口做周期分析并刷新数值/状态（~3.3Hz）。
 *
 ****************************************************************************/

static void pend_analyze(void)
{
  float T = 0.0f;
  float gval;
  float lval;
  int n = g_p.n;

  if (n < MIN_ANALYZE)
    {
      pend_update_status(PW_STR(PEND_STATUS_COLLECT), PW_COL_TEXT);
      return;
    }

  if (pend_stddev_recent() < MOTION_STD)
    {
      if (++g_p.bad_streak > 6)
        {
          pend_blank_result();
          pend_update_status(PW_STR(PEND_STATUS_NO_MOTION), PW_COL_DIM);
        }
      else if (g_p.have_val)
        {
          pend_update_status(PW_STR(PEND_STATUS_SLOWING), PW_COL_DIM);
        }

      return;
    }

  g_p.bad_streak = 0;

  T = pw_signal_period(g_p.buf, n, PEND_DT);

  if (T <= 0.0f)
    {
      T = pend_period_fft_fallback();
    }

  if (T <= 0.0f)
    {
      pend_update_status(PW_STR(PEND_STATUS_STEADY), G_ACC);
      return;
    }

  gval = 4.0f * (float)M_PI * (float)M_PI * g_p.L / (T * T);
  lval = G_STATIC_G * T * T / (4.0f * (float)M_PI * (float)M_PI);

  if (gval < 0.5f || gval > 25.0f || lval < 0.01f || lval > 10.0f)
    {
      pend_update_status(PW_STR(PEND_STATUS_UNREAL), PW_SER_ORANGE);
      return;
    }

  g_p.have_val = 1;

  lv_label_set_text_fmt(g_p.g_lab, "%.2f", (double)gval);
  lv_obj_set_style_text_color(g_p.g_lab, G_ACC, 0);
  lv_label_set_text_fmt(g_p.l_lab, "%.3f", (double)lval);
  lv_obj_set_style_text_color(g_p.l_lab, G_ACC, 0);
  lv_label_set_text_fmt(g_p.tf_lab, "T = %.3f s   f = %.2f Hz",
                        (double)T, (double)(1.0f / T));
  lv_label_set_text_fmt(g_p.ac_lab, "T = %.3f s   (first peak)",
                        (double)T);
  pend_update_status(PW_STR(PEND_STATUS_LIVE), G_ACC);

  /* 共振历史攒点：每次有效测量记录 (频率, 相对幅度) */

  {
    float amp = pend_stddev_recent();
    float f = 1.0f / T;

    if (amp > g_p.res_maxamp)
      {
        g_p.res_maxamp = amp;
      }

    if (g_p.res_maxamp > 0.0f)
      {
        if (g_p.res_n >= RES_MAX)
          {
            memmove(g_p.res_freq, g_p.res_freq + 1,
                    (RES_MAX - 1) * sizeof(float));
            memmove(g_p.res_amp, g_p.res_amp + 1,
                    (RES_MAX - 1) * sizeof(float));
            g_p.res_n = RES_MAX - 1;
          }

        g_p.res_freq[g_p.res_n] = f;
        g_p.res_amp[g_p.res_n] = amp / g_p.res_maxamp;
        g_p.res_n++;
      }

    if (g_p.idx == 3)
      {
        pw_graph_set_data(g_p.res_graph, g_p.res_freq, g_p.res_amp,
                          g_p.res_n);
      }
  }
}

/****************************************************************************
 * Name: pend_tick_cb
 *
 * Description:
 *   20ms LVGL 定时器：采集入窗 + 波形推点 + 周期分析。
 *
 ****************************************************************************/

static void pend_tick_cb(lv_timer_t *timer)
{
  struct pw_imu_s imu;
  float v;

  if (lv_screen_active() != g_p.scr)
    {
      return;
    }

  if (g_bench_on)
    {
      /* 合成：1.4Hz 摆 + 少量噪声（近似真实摆动） */
      float ph = 2.0f * (float)M_PI * 1.4f * (g_bench_sample * PEND_DT);
      v = 6000.0f * sinf(ph) + 300.0f * sinf(2.0f * ph);
      g_bench_sample++;
    }
  else
    {
      if (pw_sensors_read_imu(&imu) < 0)
        {
          return;
        }

      v = (float)imu.gx + imu.gy + imu.gz;   /* phyphox anyGyr */
    }

  /* bench 自动翻页（演示"数据累积 + 多页图表"场景） */
  if (g_bench_interval > 0 &&
      (g_bench_sample % (long)(g_bench_interval * 50)) == 0)
    {
      pend_scroll_to((g_p.idx + 1) % PEND_PAGES, false);
    }

  if (g_p.n >= WIN_SAMPLES)
    {
      memmove(g_p.buf, g_p.buf + 1, (WIN_SAMPLES - 1) * sizeof(float));
      g_p.buf[WIN_SAMPLES - 1] = v;
    }
  else
    {
      g_p.buf[g_p.n++] = v;
    }

  g_p.ema = g_p.ema * 0.99f + v * 0.01f;

  {
    float d = v - g_p.ema;

    if (g_p.scope_n >= CHART_POINTS)
      {
        memmove(g_p.scope, g_p.scope + 1,
                (CHART_POINTS - 1) * sizeof(float));
        g_p.scope[CHART_POINTS - 1] = d;
      }
    else
      {
        g_p.scope[g_p.scope_n++] = d;
      }
  }

  /* 波形（pw_scope）：页0可见时每 2 拍(40ms, 25fps)整段重画，归一化到
   * [-1,1]（±20000 mdps 满量程）。CPU 光栅 + EPIC blit，开销小。 */
  if (g_p.idx == 0 && ++g_p.wave_div >= 2)
    {
      int i;

      g_p.wave_div = 0;
      for (i = 0; i < g_p.scope_n; i++)
        {
          float dv = g_p.scope[i] / 20000.0f;
          float tmp;

          if (dv > 1.0f)
            {
              dv = 1.0f;
            }
          else if (dv < -1.0f)
            {
              dv = -1.0f;
            }

          tmp = dv;
          /* 就地写回（scope 缓冲区是 float，与 pw_scope 输入类型一致） */
          g_p.scope[i] = tmp;
        }

      pw_scope_set_data(g_p.wave, g_p.scope, g_p.scope_n);
    }

  /* 自相关曲线：页2可见且有运动时每 5 拍(100ms)刷新（pw_scope，便宜） */
  if (g_p.idx == 2 && ++g_p.ac_div >= 5)
    {
      g_p.ac_div = 0;
      if (g_p.n >= MIN_ANALYZE && pend_stddev_recent() >= MOTION_STD)
        {
          pend_fill_ac_curve();
        }
    }

  if (++g_p.tick >= ANALYSIS_EVERY)
    {
      g_p.tick = 0;
      pend_analyze();
    }
}

/****************************************************************************
 * Name: pend_gctl_status
 *
 * Description:
 *   图控条范围回传 → 状态行（共用 pw_graph_controls_add）。
 *
 ****************************************************************************/

static void pend_gctl_status(void *ud, const char *msg)
{
  (void)ud;
  pend_update_status(msg, PW_COL_DIM);
}

/****************************************************************************
 * Private page builders
 ****************************************************************************/

/****************************************************************************
 * Name: pend_build_page_g
 *
 * Description:
 *   页 0：Measure g —— L 参数 + 大号 g + T/f + 实时波形。
 *
 ****************************************************************************/

static void pend_build_page_g(void)
{
  static const float preset[] = {0.10f, 0.30f, 0.50f};
  static const int   delta_m1 = -1;
  static const int   delta_p1 = 1;
  lv_obj_t *pg = lv_obj_create(g_p.scroller);
  lv_obj_t *lab;
  lv_obj_t *card;
  lv_obj_t *btn;
  int i;

  lv_obj_set_size(pg, PAGE_W, PAGE_H);
  lv_obj_set_pos(pg, 0, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  lab = pw_label_new(pg, PW_STR(PEND_MEASURE_G), PW_FNT_MED, G_ACC);
  lv_obj_set_pos(lab, 20, 4);

  lab = pw_label_new(pg, PW_STR(PEND_FORMULA_G), PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 180, 8);

  /* 摆长参数卡：行1 = [-] 数值 [+]；行2 = 预设 */

  card = pw_card_new(pg, 354, 94, PW_COL_CARD);
  lv_obj_set_pos(card, 18, 28);

  lab = pw_label_new(card, PW_STR(LENGTH_L_M), PW_FNT_MED, PW_COL_TEXT);
  lv_obj_set_pos(lab, 12, 6);

  g_p.len_minus = pw_card_new(card, 52, 38, PW_COL_CARD_LT);
  lv_obj_set_pos(g_p.len_minus, 150, 6);
  lv_obj_add_event_cb(g_p.len_minus, pend_len_step_cb,
                      LV_EVENT_CLICKED, (void *)&delta_m1);
  lab = pw_label_new(g_p.len_minus, "-", PW_FNT_XL, PW_COL_DIM);
  lv_obj_center(lab);

  g_p.len_lab = pw_label_new(card, "0.50", PW_FNT_XL, PW_COL_TEXT);
  lv_obj_set_pos(g_p.len_lab, 210, 10);

  g_p.len_plus = pw_card_new(card, 52, 38, PW_COL_CARD_LT);
  lv_obj_set_pos(g_p.len_plus, 290, 6);
  lv_obj_add_event_cb(g_p.len_plus, pend_len_step_cb,
                      LV_EVENT_CLICKED, (void *)&delta_p1);
  lab = pw_label_new(g_p.len_plus, "+", PW_FNT_XL, PW_COL_DIM);
  lv_obj_center(lab);

  /* 预设快捷值（第二行） */

  for (i = 0; i < 3; i++)
    {
      btn = pw_card_new(card, 60, 34, PW_COL_CARD_LT);
      lv_obj_set_pos(btn, 12 + i * 68, 52);
      lv_obj_add_event_cb(btn, pend_len_preset_cb,
                          LV_EVENT_CLICKED, (void *)&preset[i]);
      lab = pw_label_new(btn, "", PW_FNT_BODY, PW_COL_DIM);
      lv_label_set_text_fmt(lab, "%.2f", (double)preset[i]);
      lv_obj_center(lab);
    }

  /* 大号 g */

  lab = pw_label_new(pg, "g", PW_FNT_MED, G_ACC);
  lv_obj_set_pos(lab, 26, 146);

  g_p.g_lab = pw_label_new(pg, "--", PW_FNT_XXL, G_ACC);
  lv_obj_set_pos(g_p.g_lab, 64, 136);

  lab = pw_label_new(pg, "m/s^2", PW_FNT_MED, PW_COL_DIM);
  lv_obj_set_pos(lab, 300, 170);

  g_p.tf_lab = pw_label_new(pg, PW_STR(PEND_TF_HINT), PW_FNT_MED, PW_COL_TEXT);
  lv_obj_set_pos(g_p.tf_lab, 64, 198);

  /* 实时波形（pw_scope：CPU 光栅 + EPIC blit，25fps 平滑） */

  lab = pw_label_new(pg, PW_STR(PEND_GYRO_SUM), PW_FNT_BODY,
                     PW_COL_FAINT);
  lv_obj_set_pos(lab, 20, 228);

  lab = pw_label_new(pg, PW_STR(LAST_2_4S), PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 300, 228);

  g_p.wave = pw_scope_create(pg, 354, 96, PW_ACC_GYRO, PW_COL_CARD);
  lv_obj_set_pos(g_p.wave, 18, 246);
  pw_scope_axes(g_p.wave, PW_COL_FAINT);
}

/****************************************************************************
 * Name: pend_build_page_l
 *
 * Description:
 *   页 1：Measure L —— 设 g=9.81，由周期反推摆长（phyphox Length 页）。
 *
 ****************************************************************************/

static void pend_build_page_l(void)
{
  lv_obj_t *pg = lv_obj_create(g_p.scroller);
  lv_obj_t *lab;
  lv_obj_t *card;

  lv_obj_set_size(pg, PAGE_W, PAGE_H);
  lv_obj_set_pos(pg, PAGE_W, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  lab = pw_label_new(pg, PW_STR(PEND_MEASURE_L), PW_FNT_MED, G_ACC);
  lv_obj_set_pos(lab, 20, 4);

  lab = pw_label_new(pg, PW_STR(PEND_ASSUME_G),
                     PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 176, 8);

  lab = pw_label_new(pg, "L", PW_FNT_MED, G_ACC);
  lv_obj_set_pos(lab, 26, 108);

  g_p.l_lab = pw_label_new(pg, "--", PW_FNT_XXL, G_ACC);
  lv_obj_set_pos(g_p.l_lab, 64, 98);

  lab = pw_label_new(pg, "m", PW_FNT_MED, PW_COL_DIM);
  lv_obj_set_pos(lab, 320, 132);

  lab = pw_label_new(pg, PW_STR(PEND_FORMULA_L),
                     PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 64, 162);

  card = pw_card_new(pg, 354, 110, PW_COL_CARD);
  lv_obj_set_pos(card, 18, 210);

  lab = pw_label_new(card,
                     PW_STR(PEND_SWING_HELP),
                     PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 14, 10);
  lv_obj_set_width(lab, 326);
  lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
}

/****************************************************************************
 * Name: pend_build_page_ac
 *
 * Description:
 *   页 2：Autocorrelation —— 当前窗口自相关曲线 + T。
 *
 ****************************************************************************/

static void pend_build_page_ac(void)
{
  lv_obj_t *pg = lv_obj_create(g_p.scroller);
  lv_obj_t *lab;
  lv_obj_t *card;

  lv_obj_set_size(pg, PAGE_W, PAGE_H);
  lv_obj_set_pos(pg, PAGE_W * 2, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  lab = pw_label_new(pg, PW_STR(PEND_AC_TITLE), PW_FNT_MED, G_ACC);
  lv_obj_set_pos(lab, 20, 4);

  lab = pw_label_new(pg, PW_STR(PEND_AC_DESC),
                     PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 188, 8);

  g_p.ac_lab = pw_label_new(pg, "T --", PW_FNT_MED, PW_COL_TEXT);
  lv_obj_set_pos(g_p.ac_lab, 20, 40);

  card = pw_card_new(pg, 354, 268, PW_COL_CARD);
  lv_obj_set_pos(card, 18, 62);

  lab = pw_label_new(card, PW_STR(AC_CORR), PW_FNT_BODY,
                     PW_COL_DIM);
  lv_obj_set_pos(lab, 12, 6);

  g_p.ac_graph = pw_graph_create(card, 330, 180, G_ACC, PW_COL_CARD, 0);
  lv_obj_set_pos(pw_graph_obj(g_p.ac_graph), 12, 24);
  pw_graph_controls_add(card, g_p.ac_graph, pend_gctl_status, NULL);

  lab = pw_label_new(card, PW_STR(AC_SHIFT_0_5S),
                     PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 12, 248);
}

/****************************************************************************
 * Name: pend_build_page_res
 *
 * Description:
 *   页 3：Resonance —— 振幅-频率散点历史（phyphox 共振页，受迫扫频用）。
 *
 ****************************************************************************/

static void pend_build_page_res(void)
{
  lv_obj_t *pg = lv_obj_create(g_p.scroller);
  lv_obj_t *lab;
  lv_obj_t *card;

  lv_obj_set_size(pg, PAGE_W, PAGE_H);
  lv_obj_set_pos(pg, PAGE_W * 3, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  lab = pw_label_new(pg, PW_STR(PEND_RESONANCE), PW_FNT_MED, G_ACC);
  lv_obj_set_pos(lab, 20, 4);

  lab = pw_label_new(pg, PW_STR(PEND_AMP_FREQ),
                     PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 170, 8);

  card = pw_card_new(pg, 354, 268, PW_COL_CARD);
  lv_obj_set_pos(card, 18, 40);

  lab = pw_label_new(card, PW_STR(AMP_REL), PW_FNT_BODY,
                     PW_COL_DIM);
  lv_obj_set_pos(lab, 12, 6);

  g_p.res_graph = pw_graph_create(card, 330, 180, G_ACC, PW_COL_CARD, 1);
  lv_obj_set_pos(pw_graph_obj(g_p.res_graph), 12, 24);
  pw_graph_controls_add(card, g_p.res_graph, pend_gctl_status, NULL);

  lab = pw_label_new(card, PW_STR(X_FREQ_0_3),
                     PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 12, 248);

  lab = pw_label_new(pg,
                     "Each dot = one steady swing. A resonance peak "
                     "appears when you drive the pendulum at changing "
                     "frequencies.",
                     PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 20, 314);
  lv_obj_set_width(lab, 350);
  lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
}

/****************************************************************************
 * Name: pw_pend_bench
 ****************************************************************************/

void pw_pend_bench(int on, int page_interval_s, int start_page)
{
  g_bench_on = on;
  g_bench_interval = page_interval_s;
  g_bench_sample = 0;

  if (on && start_page > 0)
    {
      pend_scroll_to(start_page % PEND_PAGES, false);
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

lv_obj_t *pw_pendulum_screen(void)
{
  lv_obj_t *btn;
  lv_obj_t *lab;
  int i;

  memset(&g_p, 0, sizeof(g_p));
  g_p.L = 0.50f;

  g_p.scr = pw_scr_new();

  /* 顶栏 + 横滑内容区（页面宽 390） */

  g_p.scroller = pw_topbar(g_p.scr, PW_STR(PEND_TITLE));
  lv_obj_set_size(g_p.scroller, PAGE_W, PAGE_H);
  lv_obj_set_scroll_dir(g_p.scroller, LV_DIR_HOR);
  lv_obj_set_scroll_snap_x(g_p.scroller, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scrollbar_mode(g_p.scroller, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(g_p.scroller, LV_OBJ_FLAG_SCROLL_ONE);
  lv_obj_add_event_cb(g_p.scroller, pend_scroll_end_cb,
                      LV_EVENT_SCROLL_END, NULL);

  /* 三页 */

  pend_build_page_g();
  pend_build_page_l();
  pend_build_page_ac();
  pend_build_page_res();

  /* 底部条：状态行居中，左箭头 + 圆点 + 右箭头 */

  g_p.status = pw_label_new(g_p.scr, PW_STR(PEND_SWING_PROMPT),
                            PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(g_p.status, 80, 398);
  lv_obj_set_width(g_p.status, 230);
  lv_obj_set_style_text_align(g_p.status, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(g_p.status, LV_LABEL_LONG_WRAP);

  btn = pw_card_new(g_p.scr, 64, 44, PW_COL_CARD);
  lv_obj_set_pos(btn, 4, 396);
  {
    static const int dm = -1;
    lv_obj_add_event_cb(btn, pend_arrow_cb, LV_EVENT_CLICKED, (void *)&dm);
    lab = pw_label_new(btn, "<", PW_FNT_XL, PW_COL_DIM);
    lv_obj_center(lab);
  }

  for (i = 0; i < PEND_PAGES; i++)
    {
      lv_obj_t *d = lv_obj_create(g_p.scr);
      lv_obj_set_size(d, 12, 12);
      lv_obj_set_style_bg_color(d, PW_COL_CARD_LT, 0);
      lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(d, 0, 0);
      lv_obj_remove_flag(d, LV_OBJ_FLAG_SCROLLABLE);
      g_p.dots[i] = d;
      lv_obj_align(d, LV_ALIGN_BOTTOM_MID, (i - (PEND_PAGES - 1) / 2) * 18,
                   -24);
    }

  btn = pw_card_new(g_p.scr, 64, 44, PW_COL_CARD);
  lv_obj_set_pos(btn, PAGE_W - 4 - 64, 396);
  {
    static const int dp = 1;
    lv_obj_add_event_cb(btn, pend_arrow_cb, LV_EVENT_CLICKED, (void *)&dp);
    lab = pw_label_new(btn, ">", PW_FNT_XL, PW_COL_DIM);
    lv_obj_center(lab);
  }

  pend_set_page(0);

  /* 连续采集 + 实时分析（屏幕删除自动释放） */

  pw_scr_set_tick(g_p.scr, pend_tick_cb, TICK_SAMPLE_MS);

  return g_p.scr;
}
