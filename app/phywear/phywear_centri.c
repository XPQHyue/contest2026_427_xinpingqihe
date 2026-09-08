/****************************************************************************
 * apps/examples/phywear/phywear_centri.c
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

/* 向心加速度实验（phyphox centripetal_acceleration 对齐）。
 *
 * 数据层（phyphox §5.3）：
 *   - acc+gyr 同步采样 @2Hz（每 25 拍 500ms 采一点）
 *   - a_c = sqrt(|a|^2 - g^2)：去重力（假设转轴竖直，重力恒定 1g）
 *   - w   = |gyr|（rad/s，mdps → π/180/1000）
 *   - 点对 (w^2, a_c)，仅收 w>0.3 rad/s 且 a_c>0.05g 的有效转圈点
 *   - r = 过原点最小二乘斜率 = Σ(a·x)/Σ(x^2)，x=w^2
 *
 * 页面（横滑 3 页，统一实验页骨架）：
 *   0 Measure r —— 大号 r + 点数 + 当前 w/a_c 实时值
 *   1 Plot      —— (w^2, a_c) 散点 + 拟合线（pw_graph 多序列，可缩放）
 *   2 Help      —— 怎么转（转盘/手腕水平画圈，轴保持竖直）
 *
 * 回归：phywear centribench [秒 [起始页]]（合成 r=0.15m）
 *       phywear centripetal [秒]（串口文本采集，供物理实测）
 */

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <math.h>

#include <lvgl/lvgl.h>

#include "phywear_ui.h"
#include "phywear_sensors.h"
#include "pw_scope.h"
#include "pw_graph.h"
#include "phywear_centri.h"
#include "phywear_i18n.h"

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define CENTRI_PAGES    3
#define PAGE_W          390
#define PAGE_H          342

#define TICK_MS         20
#define SAMPLE_EVERY    25             /* 每 25 拍(500ms)采一点 → 2Hz */

#define PT_MAX          200            /* 散点历史上限 */
#define GRAVITY_MG      1000.0f        /* 1g = 1000 mg */

#define MIN_OMEGA       0.3f           /* rad/s，低于此视为未转 */
#define MIN_AC          (0.05f * GRAVITY_MG)   /* 去重力后最小向心加速度 */

#define C_ACC           PW_ACC_MECH

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct centri_ui_s
{
  lv_obj_t *scr;
  lv_obj_t *scroller;
  int       idx;
  lv_obj_t *dots[CENTRI_PAGES];

  /* 页 0：Measure r */
  lv_obj_t *r_lab;
  lv_obj_t *info_lab;      /* 点数 + 当前 w/a */
  lv_obj_t *w_lab;         /* 当前角速度大字 */

  /* 页 1：Plot */
  struct pw_graph_s *plot;
  float     px[PT_MAX];       /* w^2 */
  float     py[PT_MAX];       /* a_c (m/s^2) */
  int       pn;
  float     fitx[2];
  float     fity[2];

  lv_obj_t *status;

  float       r;             /* 当前回归半径 m */
  int         tick;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct centri_ui_s g_c;

static int  g_bench_on;
static int  g_bench_interval;
static long g_bench_sample;
static float g_bench_phase;   /* 合成转圈相位 */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void centri_update_status(const char *txt, lv_color_t col)
{
  lv_label_set_text(g_c.status, txt);
  lv_obj_set_style_text_color(g_c.status, col, 0);
}

static void centri_sync_dots(void)
{
  int i;

  for (i = 0; i < CENTRI_PAGES; i++)
    {
      lv_obj_set_style_bg_color(g_c.dots[i],
                                (i == g_c.idx) ? C_ACC : PW_COL_CARD_LT, 0);
    }
}

static void centri_set_page(int idx)
{
  if (idx < 0)
    {
      idx = 0;
    }

  if (idx >= CENTRI_PAGES)
    {
      idx = CENTRI_PAGES - 1;
    }

  g_c.idx = idx;
  centri_sync_dots();
}

static void centri_scroll_to(int idx, bool anim)
{
  if (idx < 0 || idx >= CENTRI_PAGES)
    {
      return;
    }

  centri_set_page(idx);
  lv_obj_scroll_to_x(g_c.scroller, idx * PAGE_W,
                     anim ? LV_ANIM_ON : LV_ANIM_OFF);
}

static void centri_arrow_cb(lv_event_t *e)
{
  FAR const int *dp = lv_event_get_user_data(e);

  centri_scroll_to(g_c.idx + *dp, true);
}

static void centri_scroll_end_cb(lv_event_t *e)
{
  int idx = (int)((lv_obj_get_scroll_x(g_c.scroller) + PAGE_W / 2) /
                  PAGE_W);

  centri_set_page(idx);
}

/****************************************************************************
 * Name: centri_refit
 *
 * Description:
 *   过原点最小二乘：r = Σ(a·x)/Σ(x²)，x=ω²。刷新 r 值卡 + 拟合线。
 *
 ****************************************************************************/

static void centri_refit(void)
{
  float num = 0.0f;
  float den = 0.0f;
  float xmax = 0.0f;
  int i;

  for (i = 0; i < g_c.pn; i++)
    {
      num += g_c.py[i] * g_c.px[i];
      den += g_c.px[i] * g_c.px[i];
      if (g_c.px[i] > xmax)
        {
          xmax = g_c.px[i];
        }
    }

  if (den <= 0.0f || g_c.pn < 3)
    {
      g_c.r = 0.0f;
      lv_label_set_text(g_c.r_lab, "--");
      lv_obj_set_style_text_color(g_c.r_lab, PW_COL_DIM, 0);
      return;
    }

  g_c.r = num / den;

  lv_label_set_text_fmt(g_c.r_lab, "%.3f", (double)g_c.r);
  lv_obj_set_style_text_color(g_c.r_lab, C_ACC, 0);
  lv_label_set_text_fmt(g_c.info_lab, "%d pts", g_c.pn);

  /* 拟合线（两点：原点 → xmax 处 r·x） */

  g_c.fitx[0] = 0.0f;
  g_c.fity[0] = 0.0f;
  g_c.fitx[1] = xmax * 1.05f;
  g_c.fity[1] = g_c.r * g_c.fitx[1];

  if (g_c.idx == 1)
    {
      pw_graph_begin(g_c.plot);
      pw_graph_add_series(g_c.plot, g_c.px, g_c.py, g_c.pn,
                          lv_color_hex(0x81c784));
      pw_graph_add_series(g_c.plot, g_c.fitx, g_c.fity, 2,
                          lv_color_hex(0xffb74d));
      pw_graph_end(g_c.plot);
    }
}

/****************************************************************************
 * Name: centri_add_point
 *
 * Description:
 *   采集一对 (w^2, a_c) 并更新回归。
 *
 ****************************************************************************/

static void centri_add_point(float a_mg, float w_rad)
{
  float am = sqrtf(a_mg * a_mg - GRAVITY_MG * GRAVITY_MG);  /* mg */
  float ac;

  if (w_rad < MIN_OMEGA || !(am > 0.0f) || am < MIN_AC)
    {
      centri_update_status(PW_STR(CENTRI_STATUS_SPIN), PW_COL_TEXT);
      return;
    }

  ac = am / GRAVITY_MG * 9.81f;   /* mg → m/s² */

  if (g_c.pn >= PT_MAX)
    {
      memmove(g_c.px, g_c.px + 1, (PT_MAX - 1) * sizeof(float));
      memmove(g_c.py, g_c.py + 1, (PT_MAX - 1) * sizeof(float));
      g_c.pn = PT_MAX - 1;
    }

  g_c.px[g_c.pn] = w_rad * w_rad;
  g_c.py[g_c.pn] = ac;
  g_c.pn++;

  /* 当前值 */

  lv_label_set_text_fmt(g_c.w_lab, "%.1f", (double)w_rad);

  centri_refit();
  centri_update_status(PW_STR(CENTRI_STATUS_LIVE), C_ACC);
}

/****************************************************************************
 * Name: centri_tick_cb
 ****************************************************************************/

static void centri_tick_cb(lv_timer_t *timer)
{
  struct pw_imu_s imu;
  float am;
  float wr;

  if (lv_screen_active() != g_c.scr)
    {
      return;
    }

  if (g_bench_on)
    {
      /* 合成：r=0.15m，角速度 0.8~1.4Hz 渐变（加噪声验证回归） */
      float frot = 0.8f + 0.6f * (0.5f + 0.5f *
                    sinf(2.0f * (float)M_PI * g_bench_sample /
                         (50.0f * 20.0f)));
      float w = 2.0f * (float)M_PI * frot;
      float a = 0.15f * w * w;    /* m/s² */

      wr = w;
      am = sqrtf(GRAVITY_MG * GRAVITY_MG + (a / 9.81f * GRAVITY_MG) *
                 (a / 9.81f * GRAVITY_MG)) + 20.0f * sinf(g_bench_sample);
      g_bench_sample++;

      if (g_bench_interval > 0 &&
          (g_bench_sample % (long)(g_bench_interval * 50)) == 0)
        {
          centri_scroll_to((g_c.idx + 1) % CENTRI_PAGES, false);
        }
    }
  else
    {
      if (pw_sensors_read_imu(&imu) < 0)
        {
          return;
        }

      am = sqrtf((float)imu.ax * imu.ax +
                 (float)imu.ay * imu.ay +
                 (float)imu.az * imu.az);            /* mg */
      wr = sqrtf((float)imu.gx * imu.gx +
                 (float)imu.gy * imu.gy +
                 (float)imu.gz * imu.gz) *
           (float)M_PI / 180.0f / 1000.0f;           /* rad/s */
    }

  /* 每 500ms 采一点（2Hz，phyphox 同率） */

  if (++g_c.tick >= SAMPLE_EVERY)
    {
      g_c.tick = 0;
      centri_add_point(am, wr);
    }
}

/****************************************************************************
 * (migrated: graph controls use shared pw_graph_controls_add)
 ****************************************************************************/

/****************************************************************************
 * Name: centri_gctl_status
 *
 * Description:
 *   图控条范围回传 → 状态行（共用 pw_graph_controls_add）。
 ****************************************************************************/

static void centri_gctl_status(void *ud, const char *msg)
{
  (void)ud;
  centri_update_status(msg, PW_COL_DIM);
}

/****************************************************************************
 * Private page builders
 ****************************************************************************/

static void centri_build_page_r(void)
{
  lv_obj_t *pg = lv_obj_create(g_c.scroller);
  lv_obj_t *lab;

  lv_obj_set_size(pg, PAGE_W, PAGE_H);
  lv_obj_set_pos(pg, 0, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  lab = pw_label_new(pg, PW_STR(CENTRI_MEASURE_R), PW_FNT_MED, C_ACC);
  lv_obj_set_pos(lab, 20, 4);

  lab = pw_label_new(pg, PW_STR(CENTRI_SLOPE_DESC),
                     PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 160, 8);

  /* 大号 r */

  lab = pw_label_new(pg, "r", PW_FNT_MED, C_ACC);
  lv_obj_set_pos(lab, 26, 66);

  g_c.r_lab = pw_label_new(pg, "--", PW_FNT_XXL, C_ACC);
  lv_obj_set_pos(g_c.r_lab, 64, 56);

  lab = pw_label_new(pg, "m", PW_FNT_MED, PW_COL_DIM);
  lv_obj_set_pos(lab, 320, 90);

  g_c.info_lab = pw_label_new(pg, PW_STR(CENTRI_PTS_ZERO), PW_FNT_MED, PW_COL_TEXT);
  lv_obj_set_pos(g_c.info_lab, 64, 128);

  /* 当前角速度 */

  lab = pw_label_new(pg, PW_STR(CENTRI_W_NOW), PW_FNT_MED, C_ACC);
  lv_obj_set_pos(lab, 26, 176);

  g_c.w_lab = pw_label_new(pg, "--", PW_FNT_XL, PW_COL_TEXT);
  lv_obj_set_pos(g_c.w_lab, 130, 168);

  lab = pw_label_new(pg, "rad/s", PW_FNT_MED, PW_COL_DIM);
  lv_obj_set_pos(lab, 300, 182);

  lab = pw_label_new(pg,
                     "Each point = one spin sample. The slope of "
                     "a vs w^2 is the radius r.",
                     PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 20, 240);
  lv_obj_set_width(lab, 350);
  lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
}

static void centri_build_page_plot(void)
{
  lv_obj_t *pg = lv_obj_create(g_c.scroller);
  lv_obj_t *lab;
  lv_obj_t *card;

  lv_obj_set_size(pg, PAGE_W, PAGE_H);
  lv_obj_set_pos(pg, PAGE_W, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  lab = pw_label_new(pg, PW_STR(CENTRI_PLOT_TITLE), PW_FNT_MED, C_ACC);
  lv_obj_set_pos(lab, 20, 4);

  lab = pw_label_new(pg, PW_STR(CENTRI_PLOT_DESC),
                     PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 160, 8);

  card = pw_card_new(pg, 354, 268, PW_COL_CARD);
  lv_obj_set_pos(card, 18, 40);

  lab = pw_label_new(card, PW_STR(CENTRI_YLAB_A), PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 12, 6);

  g_c.plot = pw_graph_create(card, 330, 180, C_ACC, PW_COL_CARD, 1);
  lv_obj_set_pos(pw_graph_obj(g_c.plot), 12, 24);
  pw_graph_controls_add(card, g_c.plot, centri_gctl_status, NULL);

  lab = pw_label_new(card, PW_STR(CENTRI_XLAB_W2),
                     PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 12, 248);

  lab = pw_label_new(pg, PW_STR(CENTRI_GREEN_ORANGE),
                     PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 20, 314);
}

static void centri_build_page_help(void)
{
  lv_obj_t *pg = lv_obj_create(g_c.scroller);
  lv_obj_t *lab;
  lv_obj_t *card;

  lv_obj_set_size(pg, PAGE_W, PAGE_H);
  lv_obj_set_pos(pg, PAGE_W * 2, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  lab = pw_label_new(pg, PW_STR(HELP), PW_FNT_MED, C_ACC);
  lv_obj_set_pos(lab, 20, 4);

  card = pw_card_new(pg, 354, 280, PW_COL_CARD);
  lv_obj_set_pos(card, 18, 40);

  lab = pw_label_new(card,
        "1. Place the watch on a turntable, or strap it on your "
        "wrist and spin with the arm drawing circles.\n"
        "2. Keep the spin axis vertical: gravity must stay "
        "constant so it can be removed (sqrt(|a|^2 - g^2)).\n"
        "3. Vary the spin speed so points spread along w^2.\n"
        "4. r = slope of a vs w^2 (least squares through the "
        "origin). Compare with a ruler measurement.",
        PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 14, 10);
  lv_obj_set_width(lab, 326);
  lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void pw_centri_bench(int on, int page_interval_s, int start_page)
{
  g_bench_on = on;
  g_bench_interval = page_interval_s;
  g_bench_sample = 0;
  g_bench_phase = 0.0f;

  if (on && start_page > 0)
    {
      centri_scroll_to(start_page % CENTRI_PAGES, false);
    }
}

lv_obj_t *pw_centri_screen(void)
{
  lv_obj_t *btn;
  lv_obj_t *lab;
  int i;

  memset(&g_c, 0, sizeof(g_c));

  g_c.scr = pw_scr_new();

  g_c.scroller = pw_topbar(g_c.scr, PW_STR(CENTRI_TITLE));
  lv_obj_set_size(g_c.scroller, PAGE_W, PAGE_H);
  lv_obj_set_scroll_dir(g_c.scroller, LV_DIR_HOR);
  lv_obj_set_scroll_snap_x(g_c.scroller, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scrollbar_mode(g_c.scroller, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(g_c.scroller, LV_OBJ_FLAG_SCROLL_ONE);
  lv_obj_add_event_cb(g_c.scroller, centri_scroll_end_cb,
                      LV_EVENT_SCROLL_END, NULL);

  centri_build_page_r();
  centri_build_page_plot();
  centri_build_page_help();

  g_c.status = pw_label_new(g_c.scr, PW_STR(CENTRI_SPIN_PROMPT),
                            PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(g_c.status, 80, 398);
  lv_obj_set_width(g_c.status, 230);
  lv_obj_set_style_text_align(g_c.status, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(g_c.status, LV_LABEL_LONG_WRAP);

  btn = pw_card_new(g_c.scr, 64, 44, PW_COL_CARD);
  lv_obj_set_pos(btn, 4, 396);
  {
    static const int dm = -1;
    lv_obj_add_event_cb(btn, centri_arrow_cb, LV_EVENT_CLICKED,
                        (void *)&dm);
    lab = pw_label_new(btn, "<", PW_FNT_XL, PW_COL_DIM);
    lv_obj_center(lab);
  }

  for (i = 0; i < CENTRI_PAGES; i++)
    {
      lv_obj_t *d = lv_obj_create(g_c.scr);
      lv_obj_set_size(d, 12, 12);
      lv_obj_set_style_bg_color(d, PW_COL_CARD_LT, 0);
      lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(d, 0, 0);
      lv_obj_remove_flag(d, LV_OBJ_FLAG_SCROLLABLE);
      g_c.dots[i] = d;
      lv_obj_align(d, LV_ALIGN_BOTTOM_MID,
                   (i - (CENTRI_PAGES - 1) / 2) * 18, -24);
    }

  btn = pw_card_new(g_c.scr, 64, 44, PW_COL_CARD);
  lv_obj_set_pos(btn, PAGE_W - 4 - 64, 396);
  {
    static const int dp = 1;
    lv_obj_add_event_cb(btn, centri_arrow_cb, LV_EVENT_CLICKED, (void *)&dp);
    lab = pw_label_new(btn, ">", PW_FNT_XL, PW_COL_DIM);
    lv_obj_center(lab);
  }

  centri_set_page(0);

  pw_scr_set_tick(g_c.scr, centri_tick_cb, TICK_MS);

  return g_c.scr;
}
