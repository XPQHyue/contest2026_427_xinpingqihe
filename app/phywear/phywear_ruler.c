/****************************************************************************
 * apps/examples/phywear/phywear_ruler.c
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

/* 磁性标尺（Tools → Magnet ruler，phyphox magnetic_ruler 对齐）。
 *
 * 数据层：地磁 |B| @25Hz（每 2 拍读一次），整段会话缓冲（512 点=20s），
 * 滑动平均平滑（win=3）→ 阈值+滞回峰检测计数（pw_count_peaks，
 * min_gap=12 点 ≈ 0.5s 防回声/毛刺）。整段重算，Reset 清零。
 * 阈值内嵌步进（默认 900mG 触发 / 750mG 回落；地磁 ~600mG，磁铁靠近 >1000mG）。
 *
 * 页面（横滑 3 页）：
 *   0 Count —— 大号计数 + 阈值步进 + Reset
 *   1 Field —— |B| 曲线（pw_graph，可缩放）+ 计数回显
 *   2 Help  —— 怎么扫
 *
 * 回归：phywear rulerbench [秒 [起始页]]（合成磁场扫峰）
 */

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <math.h>

#include <lvgl/lvgl.h>

#include "phywear_ui.h"
#include "phywear_sensors.h"
#include "pw_analysis.h"
#include "pw_graph.h"
#include "phywear_ruler.h"
#include "phywear_i18n.h"

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define RULER_PAGES     3
#define PAGE_W          390
#define PAGE_H          342

#define TICK_MS         20
#define SAMPLE_TK       2              /* 每 2 拍(40ms)采一点 → 25Hz */
#define BUF_MAX         512            /* 20s 会话缓冲 */

#define TH_MIN          500.0f         /* mG */
#define TH_MAX          3000.0f
#define TH_STEP         50.0f
#define TH_DEFAULT      900.0f
#define HYST            150.0f         /* 回落滞回 = th - HYST */
#define MIN_GAP         12             /* 峰间最小间隔（点，≈0.5s） */
#define MA_WIN          3

#define R_ACC           PW_ACC_TOOL

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct ruler_ui_s
{
  lv_obj_t *scr;
  lv_obj_t *scroller;
  int       idx;
  lv_obj_t *dots[RULER_PAGES];

  lv_obj_t *cnt_lab;
  lv_obj_t *th_lab;
  lv_obj_t *cnt2_lab;
  struct pw_graph_s *graph;

  float     tbuf[BUF_MAX];
  float     bbuf[BUF_MAX];    /* 原始 |B| mG */
  float     sbuf[BUF_MAX];    /* 平滑后 */
  int       n;
  float     tnow;
  int       sdiv;
  float     th;

  lv_obj_t *status;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct ruler_ui_s g_r;

static int  g_bench_on;
static int  g_bench_interval;
static long g_bench_sample;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void ruler_update_status(const char *txt, lv_color_t col)
{
  lv_label_set_text(g_r.status, txt);
  lv_obj_set_style_text_color(g_r.status, col, 0);
}

static void ruler_sync_dots(void)
{
  int i;

  for (i = 0; i < RULER_PAGES; i++)
    {
      lv_obj_set_style_bg_color(g_r.dots[i],
                                (i == g_r.idx) ? R_ACC : PW_COL_CARD_LT, 0);
    }
}

static void ruler_set_page(int idx)
{
  if (idx < 0)
    {
      idx = 0;
    }

  if (idx >= RULER_PAGES)
    {
      idx = RULER_PAGES - 1;
    }

  g_r.idx = idx;
  ruler_sync_dots();
}

static void ruler_scroll_to(int idx, bool anim)
{
  if (idx < 0 || idx >= RULER_PAGES)
    {
      return;
    }

  ruler_set_page(idx);
  lv_obj_scroll_to_x(g_r.scroller, idx * PAGE_W,
                     anim ? LV_ANIM_ON : LV_ANIM_OFF);
}

static void ruler_arrow_cb(lv_event_t *e)
{
  FAR const int *dp = lv_event_get_user_data(e);

  ruler_scroll_to(g_r.idx + *dp, true);
}

static void ruler_scroll_end_cb(lv_event_t *e)
{
  int idx = (int)((lv_obj_get_scroll_x(g_r.scroller) + PAGE_W / 2) /
                  PAGE_W);

  ruler_set_page(idx);
}

/****************************************************************************
 * Name: ruler_set_th
 ****************************************************************************/

static void ruler_set_th(float th)
{
  if (th < TH_MIN)
    {
      th = TH_MIN;
    }

  if (th > TH_MAX)
    {
      th = TH_MAX;
    }

  g_r.th = th;
  lv_label_set_text_fmt(g_r.th_lab, "%.0f", (double)g_r.th);
}

static void ruler_th_step_cb(lv_event_t *e)
{
  FAR const int *dp = lv_event_get_user_data(e);

  ruler_set_th(g_r.th + (*dp) * TH_STEP);
}

/****************************************************************************
 * Name: ruler_recount
 *
 * Description:
 *   整段重算平滑 + 峰计数（n≤512 成本低），刷新计数与曲线。
 *
 ****************************************************************************/

static void ruler_recount(void)
{
  unsigned count = 0;

  if (g_r.n > 1)
    {
      pw_moving_average(g_r.bbuf, (unsigned)g_r.n, g_r.sbuf, MA_WIN);
      count = pw_count_peaks(g_r.sbuf, (unsigned)g_r.n,
                             g_r.th, g_r.th - HYST, MIN_GAP);
    }

  lv_label_set_text_fmt(g_r.cnt_lab, "%u", count);
  lv_label_set_text_fmt(g_r.cnt2_lab, "%u peaks", count);

  if (g_r.idx == 1 && g_r.n > 1)
    {
      pw_graph_set_data(g_r.graph, g_r.tbuf, g_r.sbuf, g_r.n);
    }

  ruler_update_status(count > 0 ? PW_STR(RULER_STATUS_LIVE) :
                                 PW_STR(RULER_STATUS_SWEEP),
                       count > 0 ? R_ACC : PW_COL_TEXT);
}

/****************************************************************************
 * Name: ruler_reset_cb
 ****************************************************************************/

static void ruler_reset_cb(lv_event_t *e)
{
  g_r.n = 0;
  g_r.tnow = 0.0f;
  ruler_recount();
}

/****************************************************************************
 * Name: ruler_tick_cb
 ****************************************************************************/

static void ruler_tick_cb(lv_timer_t *timer)
{
  float m;

  if (lv_screen_active() != g_r.scr)
    {
      return;
    }

  if (g_bench_on)
    {
      /* 合成：600mG 地磁 + 每 ~3s 一个扫峰（幅值 800mG） */
      float t = (float)g_bench_sample * TICK_MS / 1000.0f;
      float bump = 800.0f * expf(-((fmodf(t, 3.0f) - 1.0f) *
                                  (fmodf(t, 3.0f) - 1.0f)) / 0.05f);

      m = 600.0f + bump + 10.0f * sinf(2.0f * (float)M_PI * 0.9f * t);
      g_bench_sample++;

      if (g_bench_interval > 0 &&
          (g_bench_sample % (long)(g_bench_interval * 50)) == 0)
        {
          ruler_scroll_to((g_r.idx + 1) % RULER_PAGES, false);
        }
    }
  else
    {
      struct pw_mag_s mag;

      if (pw_sensors_read_mag(&mag) < 0)
        {
          return;
        }

      m = sqrtf((float)mag.x * mag.x +
                (float)mag.y * mag.y +
                (float)mag.z * mag.z);
    }

  if (++g_r.sdiv >= SAMPLE_TK)
    {
      g_r.sdiv = 0;

      if (g_r.n >= BUF_MAX)
        {
          memmove(g_r.tbuf, g_r.tbuf + 1, (BUF_MAX - 1) * sizeof(float));
          memmove(g_r.bbuf, g_r.bbuf + 1, (BUF_MAX - 1) * sizeof(float));
          g_r.n = BUF_MAX - 1;
        }

      g_r.tbuf[g_r.n] = g_r.tnow;
      g_r.bbuf[g_r.n] = m;
      g_r.n++;
      g_r.tnow += (float)TICK_MS * SAMPLE_TK / 1000.0f;

      ruler_recount();
    }
}

/****************************************************************************
 * (migrated: graph controls use shared pw_graph_controls_add)
 ****************************************************************************/

/****************************************************************************
 * Name: ruler_gctl_status
 *
 * Description:
 *   图控条范围回传 → 状态行（共用 pw_graph_controls_add）。
 ****************************************************************************/

static void ruler_gctl_status(void *ud, const char *msg)
{
  (void)ud;
  ruler_update_status(msg, PW_COL_DIM);
}

/****************************************************************************
 * Private page builders
 ****************************************************************************/

static void ruler_build_page_count(void)
{
  static const int delta_m1 = -1;
  static const int delta_p1 = 1;
  lv_obj_t *pg = lv_obj_create(g_r.scroller);
  lv_obj_t *lab;
  lv_obj_t *card;
  lv_obj_t *btn;

  lv_obj_set_size(pg, PAGE_W, PAGE_H);
  lv_obj_set_pos(pg, 0, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  lab = pw_label_new(pg, PW_STR(RULER_COUNT), PW_FNT_MED, R_ACC);
  lv_obj_set_pos(lab, 20, 4);

  lab = pw_label_new(pg, PW_STR(RULER_PEAK_DESC),
                     PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 120, 8);

  /* 大号计数 */

  g_r.cnt_lab = pw_label_new(pg, "0", PW_FNT_XXL, R_ACC);
  lv_obj_set_pos(g_r.cnt_lab, 64, 76);

  lab = pw_label_new(pg, PW_STR(RULER_MAGNETS), PW_FNT_MED, PW_COL_DIM);
  lv_obj_set_pos(lab, 240, 110);

  /* 阈值参数卡：[-] 数值 [+] */

  card = pw_card_new(pg, 354, 76, PW_COL_CARD);
  lv_obj_set_pos(card, 18, 160);

  lab = pw_label_new(card, PW_STR(THRESH_MG), PW_FNT_MED, PW_COL_TEXT);
  lv_obj_set_pos(lab, 12, 22);

  btn = pw_card_new(card, 52, 38, PW_COL_CARD_LT);
  lv_obj_set_pos(btn, 150, 18);
  lv_obj_add_event_cb(btn, ruler_th_step_cb, LV_EVENT_CLICKED,
                      (void *)&delta_m1);
  lab = pw_label_new(btn, "-", PW_FNT_XL, PW_COL_DIM);
  lv_obj_center(lab);

  g_r.th_lab = pw_label_new(card, "900", PW_FNT_XL, PW_COL_TEXT);
  lv_obj_set_pos(g_r.th_lab, 212, 24);

  btn = pw_card_new(card, 52, 38, PW_COL_CARD_LT);
  lv_obj_set_pos(btn, 288, 18);
  lv_obj_add_event_cb(btn, ruler_th_step_cb, LV_EVENT_CLICKED,
                      (void *)&delta_p1);
  lab = pw_label_new(btn, "+", PW_FNT_XL, PW_COL_DIM);
  lv_obj_center(lab);

  /* Reset */

  btn = pw_card_new(pg, 160, 48, PW_COL_CARD_LT);
  lv_obj_set_pos(btn, 115, 260);
  lv_obj_add_event_cb(btn, ruler_reset_cb, LV_EVENT_CLICKED, NULL);
  lab = pw_label_new(btn, PW_STR(LIFE_RESET), PW_FNT_MED, PW_COL_DIM);
  lv_obj_center(lab);
}

static void ruler_build_page_field(void)
{
  lv_obj_t *pg = lv_obj_create(g_r.scroller);
  lv_obj_t *lab;
  lv_obj_t *card;

  lv_obj_set_size(pg, PAGE_W, PAGE_H);
  lv_obj_set_pos(pg, PAGE_W, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  lab = pw_label_new(pg, PW_STR(RULER_FIELD), PW_FNT_MED, R_ACC);
  lv_obj_set_pos(lab, 20, 4);

  g_r.cnt2_lab = pw_label_new(pg, PW_STR(ZERO_PEAKS), PW_FNT_MED, PW_COL_TEXT);
  lv_obj_set_pos(g_r.cnt2_lab, 120, 4);

  card = pw_card_new(pg, 354, 268, PW_COL_CARD);
  lv_obj_set_pos(card, 18, 40);

  lab = pw_label_new(card, PW_STR(RULER_YLAB_MG), PW_FNT_BODY,
                     PW_COL_DIM);
  lv_obj_set_pos(lab, 12, 6);

  g_r.graph = pw_graph_create(card, 330, 180, R_ACC, PW_COL_CARD, 0);
  lv_obj_set_pos(pw_graph_obj(g_r.graph), 12, 24);
  pw_graph_controls_add(card, g_r.graph, ruler_gctl_status, NULL);

  lab = pw_label_new(card, PW_STR(RULER_XLAB_TIME), PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 12, 248);

  lab = pw_label_new(pg, PW_STR(LAST_20S_25HZ), PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 20, 314);
}

static void ruler_build_page_help(void)
{
  lv_obj_t *pg = lv_obj_create(g_r.scroller);
  lv_obj_t *lab;
  lv_obj_t *card;

  lv_obj_set_size(pg, PAGE_W, PAGE_H);
  lv_obj_set_pos(pg, PAGE_W * 2, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  lab = pw_label_new(pg, PW_STR(HELP), PW_FNT_MED, R_ACC);
  lv_obj_set_pos(lab, 20, 4);

  card = pw_card_new(pg, 354, 280, PW_COL_CARD);
  lv_obj_set_pos(card, 18, 40);

  lab = pw_label_new(card,
        "1. Lay a row of magnets on the table, or move one "
        "magnet past the watch repeatedly.\n"
        "2. Each time the field strength |B| rises above the "
        "threshold and falls back, one magnet is counted.\n"
        "3. Raise the threshold if it counts too often, lower "
        "it if it misses magnets. Reset clears the count.\n"
        "Earth field here is about 600 mG; a nearby magnet "
        "pushes |B| above 1000 mG.",
        PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 14, 10);
  lv_obj_set_width(lab, 326);
  lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void pw_ruler_bench(int on, int page_interval_s, int start_page)
{
  g_bench_on = on;
  g_bench_interval = page_interval_s;
  g_bench_sample = 0;

  if (on && start_page > 0)
    {
      ruler_scroll_to(start_page % RULER_PAGES, false);
    }
}

lv_obj_t *pw_ruler_screen(void)
{
  lv_obj_t *btn;
  lv_obj_t *lab;
  int i;

  memset(&g_r, 0, sizeof(g_r));
  g_r.th = TH_DEFAULT;

  g_r.scr = pw_scr_new();

  g_r.scroller = pw_topbar(g_r.scr, PW_STR(RULER_TITLE));
  lv_obj_set_size(g_r.scroller, PAGE_W, PAGE_H);
  lv_obj_set_scroll_dir(g_r.scroller, LV_DIR_HOR);
  lv_obj_set_scroll_snap_x(g_r.scroller, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scrollbar_mode(g_r.scroller, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(g_r.scroller, LV_OBJ_FLAG_SCROLL_ONE);
  lv_obj_add_event_cb(g_r.scroller, ruler_scroll_end_cb,
                      LV_EVENT_SCROLL_END, NULL);

  ruler_build_page_count();
  ruler_build_page_field();
  ruler_build_page_help();

  g_r.status = pw_label_new(g_r.scr, PW_STR(RULER_STATUS_SWEEP),
                            PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(g_r.status, 80, 398);
  lv_obj_set_width(g_r.status, 230);
  lv_obj_set_style_text_align(g_r.status, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(g_r.status, LV_LABEL_LONG_WRAP);

  btn = pw_card_new(g_r.scr, 64, 44, PW_COL_CARD);
  lv_obj_set_pos(btn, 4, 396);
  {
    static const int dm = -1;
    lv_obj_add_event_cb(btn, ruler_arrow_cb, LV_EVENT_CLICKED, (void *)&dm);
    lab = pw_label_new(btn, "<", PW_FNT_XL, PW_COL_DIM);
    lv_obj_center(lab);
  }

  for (i = 0; i < RULER_PAGES; i++)
    {
      lv_obj_t *d = lv_obj_create(g_r.scr);
      lv_obj_set_size(d, 12, 12);
      lv_obj_set_style_bg_color(d, PW_COL_CARD_LT, 0);
      lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(d, 0, 0);
      lv_obj_remove_flag(d, LV_OBJ_FLAG_SCROLLABLE);
      g_r.dots[i] = d;
      lv_obj_align(d, LV_ALIGN_BOTTOM_MID,
                   (i - (RULER_PAGES - 1) / 2) * 18, -24);
    }

  btn = pw_card_new(g_r.scr, 64, 44, PW_COL_CARD);
  lv_obj_set_pos(btn, PAGE_W - 4 - 64, 396);
  {
    static const int dp = 1;
    lv_obj_add_event_cb(btn, ruler_arrow_cb, LV_EVENT_CLICKED, (void *)&dp);
    lab = pw_label_new(btn, ">", PW_FNT_XL, PW_COL_DIM);
    lv_obj_center(lab);
  }

  ruler_set_page(0);
  ruler_set_th(TH_DEFAULT);

  pw_scr_set_tick(g_r.scr, ruler_tick_cb, TICK_MS);

  return g_r.scr;
}
