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

/* 指针式角度盘几何（角度页）。盘心 = (DIAL_X+DIAL_SZ/2, DIAL_Y+DIAL_SZ/2)。
 * 刻度用 13 根 lv_line（-90°..+90° 每 15°，每 30° 一根长粗刻度）；
 * **不用 lv_arc** —— 实测软件圆弧绘制会把本页帧率从 ~26 拉到 23。 */

#define DIAL_SZ         168
#define DIAL_X          14
#define DIAL_Y          40
#define NEEDLE_LEN      62             /* 指尖半径（刻度内缘 68） */
#define DIAL_TICKS      13
#define DIAL_TICK_STEP  15

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

  /* 指针式角度盘：刻度线 + 指针 + 盘心，全部 LVGL 图元，
   * 不开新缓冲、不占 pw_scope 槽位、零字体成本。 */

  lv_obj_t *needle;
  lv_obj_t *hub;
  lv_point_precise_t nd_pt[2];
  lv_obj_t *ticks[DIAL_TICKS];
  uint16_t  ticks_lit;   /* 已点亮刻度的位图（只改变化的位） */
  int       dial_div;    /* 指针盘 12.5Hz 节流（比标签更贵） */
  float     dial_last;   /* 上次画针的角度；变化 <1° 不重绘 */
  bool      dial_drawn;

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

/* 定义在文件后部（角度盘构建处），tick 会用到 */

static void inc_dial_needle(float d);
static void inc_dial_fill(float d);

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

      /* 指针盘：量程 ±90°（超量程贴边），指针 0° 朝上、+90° 朝右。
       * 12.5Hz 节流 + 位移 <1°（针尖 ~1.2px）不重绘：静止的手表不该有绘制开销。 */

      if (++g_i.dial_div >= 2)
        {
          float d = ang;

          g_i.dial_div = 0;

          if (d > 90.0f)
            {
              d = 90.0f;
            }
          else if (d < -90.0f)
            {
              d = -90.0f;
            }

          if (!g_i.dial_drawn || fabsf(d - g_i.dial_last) >= 1.0f)
            {
              inc_dial_needle(d);
              inc_dial_fill(d);
              g_i.dial_last = d;
              g_i.dial_drawn = true;
            }
        }
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
 * Name: inc_line_place
 *
 * Description:
 *   把一根 lv_line 摆到 (x0,y0)-(x1,y1)（角度盘内坐标）。
 *
 *   关键：lv_line_set_points() 失效的是**整个对象框**，所以对象框必须收紧到
 *   线段自身的包围盒（外扩 2px 防描边被裁）—— 否则一次移针就会让 168×168
 *   的整盘重画。刻度与指针共用这一函数。
 *
 ****************************************************************************/

static void inc_line_place(lv_obj_t *ln, lv_point_precise_t *pt,
                           int x0, int y0, int x1, int y1)
{
  int ax = ((x0 < x1) ? x0 : x1) - 2;
  int ay = ((y0 < y1) ? y0 : y1) - 2;
  int bx = ((x0 < x1) ? x1 : x0) + 2;
  int by = ((y0 < y1) ? y1 : y0) + 2;

  lv_obj_set_pos(ln, DIAL_X + ax, DIAL_Y + ay);
  lv_obj_set_size(ln, (bx - ax) + 1, (by - ay) + 1);

  pt[0].x = x0 - ax;
  pt[0].y = y0 - ay;
  pt[1].x = x1 - ax;
  pt[1].y = y1 - ay;
  lv_line_set_points(ln, pt, 2);
}

/****************************************************************************
 * Name: inc_dial_ticks_build
 *
 * Description:
 *   建 DIAL_TICKS 根刻度短线（-90°..+90°，每 15°；每 30° 一根长粗刻度）。
 *   刻度是静态的，只在"点亮/熄灭"时改颜色，因此不需要 lv_arc。
 *
 ****************************************************************************/

static lv_point_precise_t g_tick_pt[DIAL_TICKS][2];

static void inc_dial_ticks_build(lv_obj_t *parent)
{
  int i;

  for (i = 0; i < DIAL_TICKS; i++)
    {
      int a = -90 + i * DIAL_TICK_STEP;
      bool major = ((a % 30) == 0);
      float rad = (float)a * (float)M_PI / 180.0f;
      float ux = sinf(rad);
      float uy = -cosf(rad);
      int c = DIAL_SZ / 2;
      int r0 = major ? 68 : 72;
      int r1 = 80;
      lv_obj_t *ln = lv_line_create(parent);

      lv_obj_remove_flag(ln, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_set_style_bg_opa(ln, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(ln, 0, 0);
      lv_obj_set_style_pad_all(ln, 0, 0);
      lv_obj_set_style_line_width(ln, major ? 3 : 2, 0);
      lv_obj_set_style_line_color(ln, PW_COL_DIM, 0);

      inc_line_place(ln, g_tick_pt[i],
                     c + (int)(r0 * ux), c + (int)(r0 * uy),
                     c + (int)(r1 * ux), c + (int)(r1 * uy));
      g_i.ticks[i] = ln;
    }

  g_i.ticks_lit = 0xffff;   /* 强制首次全量同步一次 */
}

/****************************************************************************
 * Name: inc_dial_needle
 *
 * Description:
 *   把指针放到角度 d（度，0=朝上、+90=朝右）。
 *
 ****************************************************************************/

static void inc_dial_needle(float d)
{
  int c = DIAL_SZ / 2;
  float rad = d * (float)M_PI / 180.0f;

  inc_line_place(g_i.needle, g_i.nd_pt, c, c,
                 c + (int)(NEEDLE_LEN * sinf(rad)),
                 c - (int)(NEEDLE_LEN * cosf(rad)));
}

/****************************************************************************
 * Name: inc_dial_fill
 *
 * Description:
 *   按当前角度点亮/熄灭刻度（0° 在正上方，正值向右）。
 *   只对"状态变了"的刻度写颜色 —— 一次移针通常只改 0~1 根。
 *
 ****************************************************************************/

static void inc_dial_fill(float d)
{
  uint16_t want = 0;
  int i;

  for (i = 0; i < DIAL_TICKS; i++)
    {
      int a = -90 + i * DIAL_TICK_STEP;
      bool lit = (d >= 0.0f) ? (a >= 0 && (float)a <= d)
                             : (a <= 0 && (float)a >= d);

      if (lit)
        {
          want |= (uint16_t)(1u << i);
        }
    }

  if (want == g_i.ticks_lit)
    {
      return;
    }

  for (i = 0; i < DIAL_TICKS; i++)
    {
      uint16_t bit = (uint16_t)(1u << i);

      if ((want ^ g_i.ticks_lit) & bit)
        {
          lv_obj_set_style_line_color(g_i.ticks[i],
                                      (want & bit) ? PW_ACC_TOOL : PW_COL_DIM,
                                      0);
        }
    }

  g_i.ticks_lit = want;
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

  /* 指针式角度盘：13 根刻度短线（每 15°）+ 指针 + 盘心圆点。
   * 不用 lv_arc —— 软件圆弧绘制实测把本页帧率从 ~26 拉到 23。 */

  inc_dial_ticks_build(pg);

  g_i.needle = lv_line_create(pg);
  lv_obj_remove_flag(g_i.needle, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_opa(g_i.needle, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g_i.needle, 0, 0);
  lv_obj_set_style_pad_all(g_i.needle, 0, 0);
  lv_obj_set_style_line_width(g_i.needle, 3, 0);
  lv_obj_set_style_line_color(g_i.needle, PW_COL_TEXT, 0);
  lv_obj_set_style_line_rounded(g_i.needle, true, 0);

  g_i.hub = lv_obj_create(pg);
  lv_obj_set_size(g_i.hub, 14, 14);
  lv_obj_set_pos(g_i.hub, DIAL_X + DIAL_SZ / 2 - 7, DIAL_Y + DIAL_SZ / 2 - 7);
  lv_obj_remove_flag(g_i.hub, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_radius(g_i.hub, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_bg_color(g_i.hub, PW_COL_TEXT, 0);
  lv_obj_set_style_border_width(g_i.hub, 0, 0);
  lv_obj_set_style_pad_all(g_i.hub, 0, 0);

  inc_dial_needle(0.0f);
  inc_dial_fill(0.0f);

  /* 右侧大号读数。标签保持**自适应宽度**：定宽标签每帧重绘的是整个对象框
   * （176px），自适应只重绘文字实际范围 —— 这一步直接量在本页帧率上。 */

  g_i.ang_lab = pw_label_new(pg, "+0.0", PW_FNT_XXL, I_ACC);
  lv_obj_set_pos(g_i.ang_lab, 196, 92);

  lab = pw_label_new(pg, "deg", PW_FNT_MED, PW_COL_DIM);
  lv_obj_set_pos(lab, 196, 146);

  lab = pw_label_new(pg, PW_STR(INCLINE_FACE_UP),
                     PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 196, 180);

  g_i.axl_lab = pw_label_new(pg, PW_STR(INCLINE_AXLAB),
                             PW_FNT_MED, PW_COL_TEXT);
  lv_obj_set_pos(g_i.axl_lab, 30, 226);

  lab = pw_label_new(pg,
                     PW_STR(INCLINE_HELP_MAIN),
                     PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 30, 266);
  lv_obj_set_width(lab, 330);
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
        PW_STR(INCLINE_HELP_PAGE),
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
