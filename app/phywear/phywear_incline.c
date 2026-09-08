/****************************************************************************
 * apps/examples/phywear/phywear_incline.c
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

/* 斜面倾角（Tools → Incline，phyphox inclination 对齐）。
 *
 * 角 = atan2(ax, sqrt(ay²+az²))：x 轴与水平面的夹角。
 * 0°=手表平放（屏幕朝上），±90°=表盘竖直。20ms 读取，~5Hz 记历史。
 *
 * 页面（横滑 3 页，统一实验页骨架）：
 *   0 Angle   —— 大号角度 + 三轴原始值
 *   1 History —— 角度-时间曲线（pw_graph，可缩放）
 *   2 Help    —— 怎么摆
 *
 * 回归：phywear inclinebench [秒 [起始页]]（合成 45°±20° 摆动）
 */

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <math.h>

#include <lvgl/lvgl.h>

#include "phywear_ui.h"
#include "phywear_sensors.h"
#include "pw_graph.h"
#include "phywear_incline.h"
#include "phywear_i18n.h"

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define INC_PAGES       3
#define PAGE_W          390
#define PAGE_H          342

#define TICK_MS         20
#define HIST_EVERY      4              /* 每 4 拍(80ms)记一点 → 12.5Hz */
#define HIST_MAX        200            /* 16s 历史 */

#define I_ACC           PW_ACC_TOOL

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct incline_ui_s
{
  lv_obj_t *scr;
  lv_obj_t *scroller;
  int       idx;
  lv_obj_t *dots[INC_PAGES];

  lv_obj_t *ang_lab;
  lv_obj_t *axl_lab;
  lv_obj_t *hist_lab;
  struct pw_graph_s *graph;

  float     tbuf[HIST_MAX];
  float     abuf[HIST_MAX];
  int       hn;
  float     tnow;
  int       tick;
  int       div;      /* 数值标签节流（25Hz 而非 50Hz） */

  lv_obj_t *status;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct incline_ui_s g_i;

static int  g_bench_on;
static int  g_bench_interval;
static long g_bench_sample;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void inc_update_status(const char *txt, lv_color_t col)
{
  lv_label_set_text(g_i.status, txt);
  lv_obj_set_style_text_color(g_i.status, col, 0);
}

static void inc_sync_dots(void)
{
  int i;

  for (i = 0; i < INC_PAGES; i++)
    {
      lv_obj_set_style_bg_color(g_i.dots[i],
                                (i == g_i.idx) ? I_ACC : PW_COL_CARD_LT, 0);
    }
}

static void inc_set_page(int idx)
{
  if (idx < 0)
    {
      idx = 0;
    }

  if (idx >= INC_PAGES)
    {
      idx = INC_PAGES - 1;
    }

  g_i.idx = idx;
  inc_sync_dots();
}

static void inc_scroll_to(int idx, bool anim)
{
  if (idx < 0 || idx >= INC_PAGES)
    {
      return;
    }

  inc_set_page(idx);
  lv_obj_scroll_to_x(g_i.scroller, idx * PAGE_W,
                     anim ? LV_ANIM_ON : LV_ANIM_OFF);
}

static void inc_arrow_cb(lv_event_t *e)
{
  FAR const int *dp = lv_event_get_user_data(e);

  inc_scroll_to(g_i.idx + *dp, true);
}

static void inc_scroll_end_cb(lv_event_t *e)
{
  int idx = (int)((lv_obj_get_scroll_x(g_i.scroller) + PAGE_W / 2) /
                  PAGE_W);

  inc_set_page(idx);
}

/****************************************************************************
 * Name: inc_tick_cb
 ****************************************************************************/

static void inc_tick_cb(lv_timer_t *timer)
{
  struct pw_imu_s imu;
  float ang;
  float ax;
  float ay;
  float az;

  if (lv_screen_active() != g_i.scr)
    {
      return;
    }

  if (g_bench_on)
    {
      /* 合成：45°±20° 摆动，重力矢量随角度变化 */
      float t = (float)g_bench_sample * TICK_MS / 1000.0f;
      float a = (45.0f + 20.0f * sinf(2.0f * (float)M_PI * 0.25f * t)) *
                (float)M_PI / 180.0f;

      ax = 1000.0f * sinf(a);
      ay = 0.0f;
      az = 1000.0f * cosf(a);
      g_bench_sample++;

      if (g_bench_interval > 0 &&
          (g_bench_sample % (long)(g_bench_interval * 50)) == 0)
        {
          inc_scroll_to((g_i.idx + 1) % INC_PAGES, false);
        }
    }
  else
    {
      if (pw_sensors_read_imu(&imu) < 0)
        {
          return;
        }

      ax = (float)imu.ax;
      ay = (float)imu.ay;
      az = (float)imu.az;
    }

  ang = atan2f(ax, sqrtf(ay * ay + az * az)) * 180.0f / (float)M_PI;

  /* 大号标签 25Hz 节流（50Hz 刷新浪费：文字重排成本高） */

  if (++g_i.div >= 2)
    {
      g_i.div = 0;
      lv_label_set_text_fmt(g_i.ang_lab, "%+.1f", (double)ang);
      lv_label_set_text_fmt(g_i.axl_lab,
                            "ax %+5.0f  ay %+5.0f  az %+5.0f mg",
                            (double)ax, (double)ay, (double)az);
    }

  /* 历史曲线（12.5Hz，200 点 = 16s） */

  if (++g_i.tick >= HIST_EVERY)
    {
      g_i.tick = 0;
      g_i.tnow += (float)TICK_MS * HIST_EVERY / 1000.0f;

      if (g_i.hn >= HIST_MAX)
        {
          memmove(g_i.tbuf, g_i.tbuf + 1, (HIST_MAX - 1) * sizeof(float));
          memmove(g_i.abuf, g_i.abuf + 1, (HIST_MAX - 1) * sizeof(float));
          g_i.hn = HIST_MAX - 1;
        }

      g_i.tbuf[g_i.hn] = g_i.tnow;
      g_i.abuf[g_i.hn] = ang;
      g_i.hn++;

      if (g_i.idx == 1)
        {
          pw_graph_set_data(g_i.graph, g_i.tbuf, g_i.abuf, g_i.hn);
        }
    }

  inc_update_status(PW_STR(INCLINE_STATUS_LIVE), I_ACC);
}

/****************************************************************************
 * Name: inc_gctl_cb / inc_add_graph_controls
 ****************************************************************************/

/****************************************************************************
 * Name: inc_gctl_status
 *
 * Description:
 *   图控条范围回传 → 状态行（共用 pw_graph_controls_add）。
 ****************************************************************************/

static void inc_gctl_status(void *ud, const char *msg)
{
  (void)ud;
  inc_update_status(msg, PW_COL_DIM);
}

/****************************************************************************
 * Private page builders
 ****************************************************************************/

static void inc_build_page_angle(void)
{
  lv_obj_t *pg = lv_obj_create(g_i.scroller);
  lv_obj_t *lab;

  lv_obj_set_size(pg, PAGE_W, PAGE_H);
  lv_obj_set_pos(pg, 0, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  lab = pw_label_new(pg, PW_STR(INCLINE_ANGLE), PW_FNT_MED, I_ACC);
  lv_obj_set_pos(lab, 20, 4);

  lab = pw_label_new(pg, PW_STR(INCLINE_FORMULA),
                     PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 140, 8);

  g_i.ang_lab = pw_label_new(pg, "+0.0", PW_FNT_XXL, I_ACC);
  lv_obj_set_pos(g_i.ang_lab, 64, 96);

  lab = pw_label_new(pg, "deg", PW_FNT_MED, PW_COL_DIM);
  lv_obj_set_pos(lab, 320, 130);

  lab = pw_label_new(pg, PW_STR(INCLINE_FACE_UP),
                     PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 64, 170);

  g_i.axl_lab = pw_label_new(pg, PW_STR(INCLINE_AXLAB),
                             PW_FNT_MED, PW_COL_TEXT);
  lv_obj_set_pos(g_i.axl_lab, 40, 210);

  lab = pw_label_new(pg,
                     "Hold the watch against a ramp: the angle "
                     "between the x-axis and the horizon is shown.",
                     PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 20, 256);
  lv_obj_set_width(lab, 350);
  lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
}

static void inc_build_page_history(void)
{
  lv_obj_t *pg = lv_obj_create(g_i.scroller);
  lv_obj_t *lab;
  lv_obj_t *card;

  lv_obj_set_size(pg, PAGE_W, PAGE_H);
  lv_obj_set_pos(pg, PAGE_W, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  lab = pw_label_new(pg, PW_STR(INCLINE_HISTORY), PW_FNT_MED, I_ACC);
  lv_obj_set_pos(lab, 20, 4);

  lab = pw_label_new(pg, PW_STR(INCLINE_ANGLE_TIME), PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 160, 8);

  g_i.hist_lab = pw_label_new(pg, PW_STR(LAST_16S), PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(g_i.hist_lab, 20, 40);

  card = pw_card_new(pg, 354, 268, PW_COL_CARD);
  lv_obj_set_pos(card, 18, 62);

  lab = pw_label_new(card, PW_STR(AXIS_Y_ANGLE), PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 12, 6);

  g_i.graph = pw_graph_create(card, 330, 180, I_ACC, PW_COL_CARD, 0);
  lv_obj_set_pos(pw_graph_obj(g_i.graph), 12, 24);
  pw_graph_controls_add(card, g_i.graph, inc_gctl_status, NULL);

  lab = pw_label_new(card, PW_STR(AXIS_X_TIME), PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 12, 248);
}

static void inc_build_page_help(void)
{
  lv_obj_t *pg = lv_obj_create(g_i.scroller);
  lv_obj_t *lab;
  lv_obj_t *card;

  lv_obj_set_size(pg, PAGE_W, PAGE_H);
  lv_obj_set_pos(pg, PAGE_W * 2, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  lab = pw_label_new(pg, PW_STR(HELP), PW_FNT_MED, I_ACC);
  lv_obj_set_pos(lab, 20, 4);

  card = pw_card_new(pg, 354, 280, PW_COL_CARD);
  lv_obj_set_pos(card, 18, 40);

  lab = pw_label_new(card,
        "The watch measures tilt using gravity: the angle is "
        "between the watch x-axis and the horizon.\n"
        "Flat on the table = 0 deg; standing on its side = 90 deg.\n"
        "Use it to check ramp angles: lay the watch on the ramp "
        "with the top edge pointing downhill.\n"
        "Accuracy is best when the watch is still (no shaking).",
        PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 14, 10);
  lv_obj_set_width(lab, 326);
  lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void pw_incline_bench(int on, int page_interval_s, int start_page)
{
  g_bench_on = on;
  g_bench_interval = page_interval_s;
  g_bench_sample = 0;

  if (on && start_page > 0)
    {
      inc_scroll_to(start_page % INC_PAGES, false);
    }
}

lv_obj_t *pw_incline_screen(void)
{
  lv_obj_t *btn;
  lv_obj_t *lab;
  int i;

  memset(&g_i, 0, sizeof(g_i));

  g_i.scr = pw_scr_new();

  g_i.scroller = pw_topbar(g_i.scr, PW_STR(INCLINE_TITLE));
  lv_obj_set_size(g_i.scroller, PAGE_W, PAGE_H);
  lv_obj_set_scroll_dir(g_i.scroller, LV_DIR_HOR);
  lv_obj_set_scroll_snap_x(g_i.scroller, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scrollbar_mode(g_i.scroller, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(g_i.scroller, LV_OBJ_FLAG_SCROLL_ONE);
  lv_obj_add_event_cb(g_i.scroller, inc_scroll_end_cb,
                      LV_EVENT_SCROLL_END, NULL);

  inc_build_page_angle();
  inc_build_page_history();
  inc_build_page_help();

  g_i.status = pw_label_new(g_i.scr, PW_STR(INCLINE_RAMP_PROMPT),
                            PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(g_i.status, 80, 398);
  lv_obj_set_width(g_i.status, 230);
  lv_obj_set_style_text_align(g_i.status, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(g_i.status, LV_LABEL_LONG_WRAP);

  btn = pw_card_new(g_i.scr, 64, 44, PW_COL_CARD);
  lv_obj_set_pos(btn, 4, 396);
  {
    static const int dm = -1;
    lv_obj_add_event_cb(btn, inc_arrow_cb, LV_EVENT_CLICKED, (void *)&dm);
    lab = pw_label_new(btn, "<", PW_FNT_XL, PW_COL_DIM);
    lv_obj_center(lab);
  }

  for (i = 0; i < INC_PAGES; i++)
    {
      lv_obj_t *d = lv_obj_create(g_i.scr);
      lv_obj_set_size(d, 12, 12);
      lv_obj_set_style_bg_color(d, PW_COL_CARD_LT, 0);
      lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(d, 0, 0);
      lv_obj_remove_flag(d, LV_OBJ_FLAG_SCROLLABLE);
      g_i.dots[i] = d;
      lv_obj_align(d, LV_ALIGN_BOTTOM_MID, (i - (INC_PAGES - 1) / 2) * 18,
                   -24);
    }

  btn = pw_card_new(g_i.scr, 64, 44, PW_COL_CARD);
  lv_obj_set_pos(btn, PAGE_W - 4 - 64, 396);
  {
    static const int dp = 1;
    lv_obj_add_event_cb(btn, inc_arrow_cb, LV_EVENT_CLICKED, (void *)&dp);
    lab = pw_label_new(btn, ">", PW_FNT_XL, PW_COL_DIM);
    lv_obj_center(lab);
  }

  inc_set_page(0);

  pw_scr_set_tick(g_i.scr, inc_tick_cb, TICK_MS);

  return g_i.scr;
}
