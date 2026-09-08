/****************************************************************************
 * apps/examples/phywear/phywear_spring.c
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

/* 弹簧振子实验（phyphox spring 对齐）。
 *
 * 数据层（phyphox §5.2，与 pendulum 分析链同模块、换输入）：
 *   - 输入：线性加速度（去重力）。手表无 Android 融合 linear_acc，
 *     用加速度矢量模 |a| @50Hz → EMA 去静态重力直流 → 振荡分量。
 *   - 分析：自相关测周期（⚠️ phyphox 不用 FFT）→ f=1/T，
 *     amplitude=stddev/f²（相对单位，phyphox 同款）。
 *
 * 页面（横滑 3 页，统一实验页骨架）：
 *   0 Measure f —— 大号 f + T + amplitude（rel），附实时波形
 *   1 Autocorr  —— 当前窗口自相关曲线 + T（教学：看周期怎么来的）
 *   2 Help      —— 怎么挂橡皮筋/读数说明
 *
 * 物理配合：橡皮筋一端固定、另一端挂重物（手表贴重物或挂重物下方），
 * 竖直方向轻拉后释放，保持竖直振动。
 *
 * 回归：phywear springbench [秒 [起始页]]（合成 2.0Hz）
 *       phywear spring [秒]（串口文本采集，不经 GUI，供物理实测）
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
#include "phywear_spring.h"
#include "phywear_i18n.h"

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define SPRING_DT       0.02f          /* 采样间隔 s（~50Hz） */
#define WIN_SAMPLES     600            /* 分析窗口 = 12s */
#define CHART_POINTS    120            /* 波形图点数 = 2.4s 窗口 */

#define AC_MAXLAG       150            /* 自相关曲线 0..3s */

#define TICK_SAMPLE_MS  20
#define ANALYSIS_EVERY  15             /* 每 15 拍(300ms) 分析一次 */

#define MOTION_SAMPLES  40             /* 用最近 0.8s 判断是否在振 */
#define MOTION_STD      40.0f          /* 振荡分量标准差阈值（mg） */
#define MIN_ANALYZE     80             /* 窗口 <1.6s 不分析 */

#define SPRING_PAGES    3
#define PAGE_W          390
#define PAGE_H          342            /* 450 - 顶栏54 - 底部条54 */

#define S_ACC           PW_ACC_MECH    /* 强调色 */

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct spring_ui_s
{
  lv_obj_t *scr;
  lv_obj_t *scroller;
  int       idx;
  lv_obj_t *dots[SPRING_PAGES];

  /* 页 0：Measure f */
  lv_obj_t *f_lab;
  lv_obj_t *t_lab;
  lv_obj_t *amp_lab;
  lv_obj_t *wave;                       /* 实时波形（pw_scope） */
  float       ema;
  float       scope[CHART_POINTS];
  int         scope_n;
  uint32_t    wave_div;

  /* 页 1：Autocorrelation */
  lv_obj_t *ac_lab;
  struct pw_graph_s *ac_graph;
  float       ac_lag[AC_MAXLAG + 1];
  uint32_t    ac_div;

  lv_obj_t *status;

  float       buf[WIN_SAMPLES];        /* 振荡分量滑动窗口（旧在前） */
  int         n;
  int         tick;
  int         have_val;
  int         bad_streak;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct spring_ui_s g_sp;

static int  g_bench_on;
static int  g_bench_interval;
static long g_bench_sample;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void spring_update_status(const char *txt, lv_color_t col)
{
  lv_label_set_text(g_sp.status, txt);
  lv_obj_set_style_text_color(g_sp.status, col, 0);
}

static void spring_sync_dots(void)
{
  int i;

  for (i = 0; i < SPRING_PAGES; i++)
    {
      lv_obj_set_style_bg_color(g_sp.dots[i],
                                (i == g_sp.idx) ? S_ACC : PW_COL_CARD_LT, 0);
    }
}

static void spring_set_page(int idx)
{
  if (idx < 0)
    {
      idx = 0;
    }

  if (idx >= SPRING_PAGES)
    {
      idx = SPRING_PAGES - 1;
    }

  g_sp.idx = idx;
  spring_sync_dots();
}

static void spring_scroll_to(int idx, bool anim)
{
  if (idx < 0 || idx >= SPRING_PAGES)
    {
      return;
    }

  spring_set_page(idx);
  lv_obj_scroll_to_x(g_sp.scroller, idx * PAGE_W,
                     anim ? LV_ANIM_ON : LV_ANIM_OFF);
}

static void spring_arrow_cb(lv_event_t *e)
{
  FAR const int *dp = lv_event_get_user_data(e);

  spring_scroll_to(g_sp.idx + *dp, true);
}

static void spring_scroll_end_cb(lv_event_t *e)
{
  int idx = (int)((lv_obj_get_scroll_x(g_sp.scroller) + PAGE_W / 2) /
                  PAGE_W);

  spring_set_page(idx);
}

static float spring_stddev_recent(void)
{
  float mean = 0.0f;
  float var = 0.0f;
  int k = g_sp.n < MOTION_SAMPLES ? g_sp.n : MOTION_SAMPLES;
  int i;

  if (k < 8)
    {
      return 0.0f;
    }

  for (i = g_sp.n - k; i < g_sp.n; i++)
    {
      mean += g_sp.buf[i];
    }

  mean /= (float)k;

  for (i = g_sp.n - k; i < g_sp.n; i++)
    {
      float d = g_sp.buf[i] - mean;
      var += d * d;
    }

  return sqrtf(var / (float)k);
}

static void spring_blank_result(void)
{
  lv_label_set_text(g_sp.f_lab, "--");
  lv_obj_set_style_text_color(g_sp.f_lab, PW_COL_DIM, 0);
  lv_label_set_text(g_sp.t_lab, "T --");
  lv_label_set_text(g_sp.amp_lab, PW_STR(SPRING_AMP_HINT));
  lv_label_set_text(g_sp.ac_lab, "T --");
}

/****************************************************************************
 * Name: spring_fill_ac_curve
 ****************************************************************************/

static void spring_fill_ac_curve(void)
{
  float det[WIN_SAMPLES];
  float ac[AC_MAXLAG + 1];
  float norm[AC_MAXLAG + 1];
  float mean = 0.0f;
  float a0;
  unsigned i;

  if (g_sp.n < MIN_ANALYZE)
    {
      return;
    }

  for (i = 0; i < (unsigned)g_sp.n; i++)
    {
      mean += g_sp.buf[i];
    }

  mean /= (float)g_sp.n;

  for (i = 0; i < (unsigned)g_sp.n; i++)
    {
      det[i] = g_sp.buf[i] - mean;
    }

  pw_autocorr(det, g_sp.n, ac, AC_MAXLAG);
  a0 = ac[0];
  if (a0 <= 0.0f)
    {
      return;
    }

  for (i = 0; i <= AC_MAXLAG; i++)
    {
      g_sp.ac_lag[i] = (float)i * SPRING_DT;
      norm[i] = ac[i] / a0;
    }

  pw_graph_set_data(g_sp.ac_graph, g_sp.ac_lag, norm, AC_MAXLAG + 1);
}

/****************************************************************************
 * Name: spring_analyze
 ****************************************************************************/

static void spring_analyze(void)
{
  float T;
  float f;
  float amp;
  int n = g_sp.n;

  if (n < MIN_ANALYZE)
    {
      spring_update_status(PW_STR(SPRING_STATUS_COLLECT), PW_COL_TEXT);
      return;
    }

  if (spring_stddev_recent() < MOTION_STD)
    {
      if (++g_sp.bad_streak > 6)
        {
          spring_blank_result();
          spring_update_status(PW_STR(SPRING_STATUS_NO_MOTION), PW_COL_DIM);
        }
      else if (g_sp.have_val)
        {
          spring_update_status(PW_STR(SPRING_STATUS_SLOWING), PW_COL_DIM);
        }

      return;
    }

  g_sp.bad_streak = 0;

  /* 自相关测周期（phyphox 同款：不用 FFT；0.2..5s 范围） */

  T = pw_signal_period(g_sp.buf, n, SPRING_DT);
  if (T <= 0.0f)
    {
      spring_update_status(PW_STR(SPRING_STATUS_STEADY), S_ACC);
      return;
    }

  f = 1.0f / T;

  /* amplitude = stddev / f²（phyphox spring 相对幅度） */

  amp = spring_stddev_recent() / (f * f);

  if (f < 0.2f || f > 6.0f)
    {
      spring_update_status(PW_STR(SPRING_STATUS_UNREAL), PW_SER_ORANGE);
      return;
    }

  g_sp.have_val = 1;

  lv_label_set_text_fmt(g_sp.f_lab, "%.2f", (double)f);
  lv_obj_set_style_text_color(g_sp.f_lab, S_ACC, 0);
  lv_label_set_text_fmt(g_sp.t_lab, "T = %.3f s", (double)T);
  lv_label_set_text_fmt(g_sp.amp_lab, "amplitude %.2f (rel)",
                        (double)amp);
  lv_label_set_text_fmt(g_sp.ac_lab, "T = %.3f s   (first peak)",
                        (double)T);
  spring_update_status(PW_STR(SPRING_STATUS_LIVE), S_ACC);
}

/****************************************************************************
 * Name: spring_tick_cb
 ****************************************************************************/

static void spring_tick_cb(lv_timer_t *timer)
{
  struct pw_imu_s imu;
  float m;
  float v;

  if (lv_screen_active() != g_sp.scr)
    {
      return;
    }

  if (g_bench_on)
    {
      /* 合成：2.0Hz 竖直振荡 ±300mg + 噪声 */
      float ph = 2.0f * (float)M_PI * 2.0f * (g_bench_sample * SPRING_DT);

      m = 1000.0f + 300.0f * sinf(ph) + 40.0f * sinf(2.0f * ph);
      g_bench_sample++;
    }
  else
    {
      if (pw_sensors_read_imu(&imu) < 0)
        {
          return;
        }

      m = sqrtf((float)imu.ax * imu.ax +
                (float)imu.ay * imu.ay +
                (float)imu.az * imu.az);   /* |a| mg */
    }

  /* bench 自动翻页 */

  if (g_bench_interval > 0 &&
      (g_bench_sample % (long)(g_bench_interval * 50)) == 0)
    {
      spring_scroll_to((g_sp.idx + 1) % SPRING_PAGES, false);
    }

  /* 去重力：EMA 直流 → 振荡分量 */

  g_sp.ema = g_sp.ema * 0.99f + m * 0.01f;
  v = m - g_sp.ema;

  if (g_sp.n >= WIN_SAMPLES)
    {
      memmove(g_sp.buf, g_sp.buf + 1, (WIN_SAMPLES - 1) * sizeof(float));
      g_sp.buf[WIN_SAMPLES - 1] = v;
    }
  else
    {
      g_sp.buf[g_sp.n++] = v;
    }

  /* 波形（pw_scope）：页0可见时每 2 拍(40ms)整段重画，±500mg 满量程 */

  if (g_sp.scope_n >= CHART_POINTS)
    {
      memmove(g_sp.scope, g_sp.scope + 1,
              (CHART_POINTS - 1) * sizeof(float));
      g_sp.scope[CHART_POINTS - 1] = v / 500.0f;
    }
  else
    {
      g_sp.scope[g_sp.scope_n++] = v / 500.0f;
    }

  if (g_sp.idx == 0 && ++g_sp.wave_div >= 2)
    {
      g_sp.wave_div = 0;
      pw_scope_set_data(g_sp.wave, g_sp.scope, g_sp.scope_n);
    }

  /* 自相关曲线：页1可见且有运动时每 5 拍(100ms)刷新 */

  if (g_sp.idx == 1 && ++g_sp.ac_div >= 5)
    {
      g_sp.ac_div = 0;
      if (g_sp.n >= MIN_ANALYZE && spring_stddev_recent() >= MOTION_STD)
        {
          spring_fill_ac_curve();
        }
    }

  if (++g_sp.tick >= ANALYSIS_EVERY)
    {
      g_sp.tick = 0;
      spring_analyze();
    }
}

/****************************************************************************
 * Name: spring_gctl_cb / spring_add_graph_controls
 ****************************************************************************/

struct spring_gctl_s
{
  struct pw_graph_s *g;
  int op;
};

static void spring_gctl_status(void *ud, const char *msg)
{
  (void)ud;
  spring_update_status(msg, PW_COL_DIM);
}

/****************************************************************************
 * Private page builders
 ****************************************************************************/

static void spring_build_page_f(void)
{
  lv_obj_t *pg = lv_obj_create(g_sp.scroller);
  lv_obj_t *lab;

  lv_obj_set_size(pg, PAGE_W, PAGE_H);
  lv_obj_set_pos(pg, 0, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  lab = pw_label_new(pg, PW_STR(SPRING_MEASURE_F), PW_FNT_MED, S_ACC);
  lv_obj_set_pos(lab, 20, 4);

  lab = pw_label_new(pg, PW_STR(SPRING_BOUNCE_DESC),
                     PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 150, 8);

  /* 大号 f */

  lab = pw_label_new(pg, "f", PW_FNT_MED, S_ACC);
  lv_obj_set_pos(lab, 26, 66);

  g_sp.f_lab = pw_label_new(pg, "--", PW_FNT_XXL, S_ACC);
  lv_obj_set_pos(g_sp.f_lab, 64, 56);

  lab = pw_label_new(pg, "Hz", PW_FNT_MED, PW_COL_DIM);
  lv_obj_set_pos(lab, 320, 90);

  g_sp.t_lab = pw_label_new(pg, "T --", PW_FNT_MED, PW_COL_TEXT);
  lv_obj_set_pos(g_sp.t_lab, 64, 128);

  g_sp.amp_lab = pw_label_new(pg, PW_STR(SPRING_AMP_HINT), PW_FNT_MED,
                              PW_COL_TEXT);
  lv_obj_set_pos(g_sp.amp_lab, 64, 156);

  /* 实时波形（pw_scope） */

  lab = pw_label_new(pg, PW_STR(AMP_GRAVITY_MG), PW_FNT_BODY,
                     PW_COL_FAINT);
  lv_obj_set_pos(lab, 20, 196);

  lab = pw_label_new(pg, PW_STR(LAST_2_4S), PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 300, 196);

  g_sp.wave = pw_scope_create(pg, 354, 96, PW_ACC_ACC, PW_COL_CARD);
  lv_obj_set_pos(g_sp.wave, 18, 214);
  pw_scope_axes(g_sp.wave, PW_COL_FAINT);
}

static void spring_build_page_ac(void)
{
  lv_obj_t *pg = lv_obj_create(g_sp.scroller);
  lv_obj_t *lab;
  lv_obj_t *card;

  lv_obj_set_size(pg, PAGE_W, PAGE_H);
  lv_obj_set_pos(pg, PAGE_W, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  lab = pw_label_new(pg, PW_STR(SPRING_AC_TITLE), PW_FNT_MED, S_ACC);
  lv_obj_set_pos(lab, 20, 4);

  lab = pw_label_new(pg, PW_STR(SPRING_AC_DESC),
                     PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 150, 8);

  g_sp.ac_lab = pw_label_new(pg, "T --", PW_FNT_MED, PW_COL_TEXT);
  lv_obj_set_pos(g_sp.ac_lab, 20, 40);

  card = pw_card_new(pg, 354, 268, PW_COL_CARD);
  lv_obj_set_pos(card, 18, 62);

  lab = pw_label_new(card, PW_STR(AC_CORR), PW_FNT_BODY,
                     PW_COL_DIM);
  lv_obj_set_pos(lab, 12, 6);

  g_sp.ac_graph = pw_graph_create(card, 330, 180, S_ACC, PW_COL_CARD, 0);
  lv_obj_set_pos(pw_graph_obj(g_sp.ac_graph), 12, 24);
  pw_graph_controls_add(card, g_sp.ac_graph, spring_gctl_status, NULL);

  lab = pw_label_new(card, PW_STR(AC_SHIFT_0_3S),
                     PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 12, 248);
}

static void spring_build_page_help(void)
{
  lv_obj_t *pg = lv_obj_create(g_sp.scroller);
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

  lab = pw_label_new(card,
        "1. Fix a rubber band at one end, hang a weight at the "
        "other, and strap the watch to the weight.\n"
        "2. Pull the weight down slightly and release it so it "
        "bounces vertically.\n"
        "3. f = 1/T is found by autocorrelation of the "
        "acceleration (gravity removed) - no FFT, same method "
        "as phyphox.\n"
        "4. amplitude = stddev / f^2 (relative units, phyphox "
        "formula).",
        PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 14, 10);
  lv_obj_set_width(lab, 326);
  lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void pw_spring_bench(int on, int page_interval_s, int start_page)
{
  g_bench_on = on;
  g_bench_interval = page_interval_s;
  g_bench_sample = 0;

  if (on && start_page > 0)
    {
      spring_scroll_to(start_page % SPRING_PAGES, false);
    }
}

lv_obj_t *pw_spring_screen(void)
{
  lv_obj_t *btn;
  lv_obj_t *lab;
  int i;

  memset(&g_sp, 0, sizeof(g_sp));

  g_sp.scr = pw_scr_new();

  g_sp.scroller = pw_topbar(g_sp.scr, PW_STR(SPRING_TITLE));
  lv_obj_set_size(g_sp.scroller, PAGE_W, PAGE_H);
  lv_obj_set_scroll_dir(g_sp.scroller, LV_DIR_HOR);
  lv_obj_set_scroll_snap_x(g_sp.scroller, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scrollbar_mode(g_sp.scroller, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(g_sp.scroller, LV_OBJ_FLAG_SCROLL_ONE);
  lv_obj_add_event_cb(g_sp.scroller, spring_scroll_end_cb,
                      LV_EVENT_SCROLL_END, NULL);

  spring_build_page_f();
  spring_build_page_ac();
  spring_build_page_help();

  /* 底部条 */

  g_sp.status = pw_label_new(g_sp.scr, PW_STR(SPRING_BOUNCE_PROMPT),
                            PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(g_sp.status, 80, 398);
  lv_obj_set_width(g_sp.status, 230);
  lv_obj_set_style_text_align(g_sp.status, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(g_sp.status, LV_LABEL_LONG_WRAP);

  btn = pw_card_new(g_sp.scr, 64, 44, PW_COL_CARD);
  lv_obj_set_pos(btn, 4, 396);
  {
    static const int dm = -1;
    lv_obj_add_event_cb(btn, spring_arrow_cb, LV_EVENT_CLICKED,
                        (void *)&dm);
    lab = pw_label_new(btn, "<", PW_FNT_XL, PW_COL_DIM);
    lv_obj_center(lab);
  }

  for (i = 0; i < SPRING_PAGES; i++)
    {
      lv_obj_t *d = lv_obj_create(g_sp.scr);
      lv_obj_set_size(d, 12, 12);
      lv_obj_set_style_bg_color(d, PW_COL_CARD_LT, 0);
      lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(d, 0, 0);
      lv_obj_remove_flag(d, LV_OBJ_FLAG_SCROLLABLE);
      g_sp.dots[i] = d;
      lv_obj_align(d, LV_ALIGN_BOTTOM_MID, (i - (SPRING_PAGES - 1) / 2) * 18,
                   -24);
    }

  btn = pw_card_new(g_sp.scr, 64, 44, PW_COL_CARD);
  lv_obj_set_pos(btn, PAGE_W - 4 - 64, 396);
  {
    static const int dp = 1;
    lv_obj_add_event_cb(btn, spring_arrow_cb, LV_EVENT_CLICKED, (void *)&dp);
    lab = pw_label_new(btn, ">", PW_FNT_XL, PW_COL_DIM);
    lv_obj_center(lab);
  }

  spring_set_page(0);

  pw_scr_set_tick(g_sp.scr, spring_tick_cb, TICK_SAMPLE_MS);

  return g_sp.scr;
}
