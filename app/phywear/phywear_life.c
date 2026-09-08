/****************************************************************************
 * apps/examples/phywear/phywear_life.c
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

/* 掌声计（Everyday → Applause meter，phyphox applause_meter 对齐）。
 *
 * 数据层：麦克风后台 pthread 采集 1024 样本块（64ms）→ RMS → dBFS；
 * pw_thresh 阈值状态机（on=-30dB off=-45dB 上升沿，滞回+自动 re-arm）
 * 计数掌声事件；dB 历史 200 点（~13s）。
 *
 * 页面（横滑 3 页）：
 *   0 Meter   —— 大号 dB + 掌声计数 + Reset
 *   1 History —— dB-时间曲线（pw_graph，可缩放）
 *   2 Help    —— 玩法
 *
 * 回归：phywear applausebench [秒 [起始页]]（合成掌声）
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
#include "phywear_life.h"
#include "phywear_i18n.h"

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define LIFE_PAGES      3
#define PAGE_W          390
#define PAGE_H          342

#define TICK_MS         20
#define HIST_MAX        200

#define DB_ON           -30.0f        /* dBFS 触发 */
#define DB_OFF          -45.0f        /* re-arm */

#define L_ACC           PW_ACC_EVERY

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct life_ui_s
{
  lv_obj_t *scr;
  lv_obj_t *scroller;
  int       idx;
  lv_obj_t *dots[LIFE_PAGES];

  lv_obj_t *db_lab;
  lv_obj_t *cnt_lab;
  struct pw_graph_s *graph;

  float     tbuf[HIST_MAX];
  float     dbuf[HIST_MAX];
  int       hn;
  float     tnow;

  struct pw_thresh_s th;
  unsigned  count;

  int16_t   mic_buf[PW_MIC_SAMPLES];
  volatile int mic_run;
  volatile int mic_ready;
  pthread_t mic_tid;

  lv_obj_t *status;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct life_ui_s g_l;

static int  g_bench_on;
static int  g_bench_interval;
static long g_bench_sample;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void life_update_status(const char *txt, lv_color_t col)
{
  lv_label_set_text(g_l.status, txt);
  lv_obj_set_style_text_color(g_l.status, col, 0);
}

static void life_sync_dots(void)
{
  int i;

  for (i = 0; i < LIFE_PAGES; i++)
    {
      lv_obj_set_style_bg_color(g_l.dots[i],
                                (i == g_l.idx) ? L_ACC : PW_COL_CARD_LT, 0);
    }
}

static void life_set_page(int idx)
{
  if (idx < 0)
    {
      idx = 0;
    }

  if (idx >= LIFE_PAGES)
    {
      idx = LIFE_PAGES - 1;
    }

  g_l.idx = idx;
  life_sync_dots();
}

static void life_scroll_to(int idx, bool anim)
{
  if (idx < 0 || idx >= LIFE_PAGES)
    {
      return;
    }

  life_set_page(idx);
  lv_obj_scroll_to_x(g_l.scroller, idx * PAGE_W,
                     anim ? LV_ANIM_ON : LV_ANIM_OFF);
}

static void life_arrow_cb(lv_event_t *e)
{
  FAR const int *dp = lv_event_get_user_data(e);

  life_scroll_to(g_l.idx + *dp, true);
}

static void life_scroll_end_cb(lv_event_t *e)
{
  int idx = (int)((lv_obj_get_scroll_x(g_l.scroller) + PAGE_W / 2) /
                  PAGE_W);

  life_set_page(idx);
}

/****************************************************************************
 * Name: life_reset_cb
 ****************************************************************************/

static void life_reset_cb(lv_event_t *e)
{
  g_l.count = 0;
  lv_label_set_text(g_l.cnt_lab, PW_STR(ZERO_CLAPS));
}

/****************************************************************************
 * Name: life_on_block
 *
 * Description:
 *   处理一个 1024 样本块：RMS → dBFS，阈值计数 + 历史曲线。
 *
 ****************************************************************************/

static void life_on_block(void)
{
  float sum = 0.0f;
  float rms;
  float db;
  int i;

  for (i = 0; i < PW_MIC_SAMPLES; i++)
    {
      float s = (float)g_l.mic_buf[i] / 32768.0f;

      sum += s * s;
    }

  rms = sqrtf(sum / (float)PW_MIC_SAMPLES);
  db = 20.0f * log10f(rms + 1e-6f);

  lv_label_set_text_fmt(g_l.db_lab, "%.0f", (double)db);

  if (pw_thresh_update(&g_l.th, db))
    {
      g_l.count++;
      lv_label_set_text_fmt(g_l.cnt_lab, "%u claps", g_l.count);
      life_update_status(PW_STR(LIFE_STATUS_CLAP), L_ACC);
    }

  /* 历史曲线（每块 64ms 一点，200 点 ≈ 13s） */

  g_l.tnow += (float)PW_MIC_SAMPLES / (float)PW_MIC_RATE;

  if (g_l.hn >= HIST_MAX)
    {
      memmove(g_l.tbuf, g_l.tbuf + 1, (HIST_MAX - 1) * sizeof(float));
      memmove(g_l.dbuf, g_l.dbuf + 1, (HIST_MAX - 1) * sizeof(float));
      g_l.hn = HIST_MAX - 1;
    }

  g_l.tbuf[g_l.hn] = g_l.tnow;
  g_l.dbuf[g_l.hn] = db;
  g_l.hn++;

  if (g_l.idx == 1)
    {
      pw_graph_set_data(g_l.graph, g_l.tbuf, g_l.dbuf, g_l.hn);
    }
}

/****************************************************************************
 * Name: life_tick_cb
 ****************************************************************************/

static void life_tick_cb(lv_timer_t *timer)
{
  if (lv_screen_active() != g_l.scr)
    {
      return;
    }

  if (g_bench_on)
    {
      /* 合成掌声：每 2s（≈31 块）一个响亮块 */
      if ((g_bench_sample % 10) == 0)
        {
          long blk = g_bench_sample / 10;
          float amp = ((blk % 31) == 0) ? 12000.0f : 150.0f;
          int i;

          for (i = 0; i < PW_MIC_SAMPLES; i++)
            {
              g_l.mic_buf[i] = (int16_t)(amp *
                sinf(2.0f * (float)M_PI * 700.0f * (float)i /
                     (float)PW_MIC_RATE));
            }

          g_l.mic_ready = 1;
        }

      g_bench_sample++;
      if (g_bench_interval > 0 &&
          (g_bench_sample % (long)(g_bench_interval * 50)) == 0)
        {
          life_scroll_to((g_l.idx + 1) % LIFE_PAGES, false);
        }
    }

  if (g_l.mic_ready)
    {
      g_l.mic_ready = 0;
      life_on_block();
    }
}

/****************************************************************************
 * Name: life_del_cb
 ****************************************************************************/

static void life_del_cb(lv_event_t *e)
{
  g_l.mic_run = 0;
}

/****************************************************************************
 * (migrated: graph controls use shared pw_graph_controls_add)
 ****************************************************************************/

/****************************************************************************
 * Name: life_gctl_status
 *
 * Description:
 *   图控条范围回传 → 状态行（共用 pw_graph_controls_add）。
 ****************************************************************************/

static void life_gctl_status(void *ud, const char *msg)
{
  (void)ud;
  life_update_status(msg, PW_COL_DIM);
}

/****************************************************************************
 * Private page builders
 ****************************************************************************/

static void life_build_page_meter(void)
{
  lv_obj_t *pg = lv_obj_create(g_l.scroller);
  lv_obj_t *lab;
  lv_obj_t *btn;

  lv_obj_set_size(pg, PAGE_W, PAGE_H);
  lv_obj_set_pos(pg, 0, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  lab = pw_label_new(pg, PW_STR(LIFE_LOUDNESS), PW_FNT_MED, L_ACC);
  lv_obj_set_pos(lab, 20, 4);

  lab = pw_label_new(pg, PW_STR(LIFE_RMS_DBFS), PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 160, 8);

  g_l.db_lab = pw_label_new(pg, "--", PW_FNT_XXL, L_ACC);
  lv_obj_set_pos(g_l.db_lab, 64, 76);

  lab = pw_label_new(pg, PW_STR(LIFE_DB), PW_FNT_MED, PW_COL_DIM);
  lv_obj_set_pos(lab, 280, 110);

  g_l.cnt_lab = pw_label_new(pg, PW_STR(ZERO_CLAPS), PW_FNT_XL, PW_COL_TEXT);
  lv_obj_set_pos(g_l.cnt_lab, 120, 190);

  lab = pw_label_new(pg, PW_STR(LIFE_CLAP_HINT), PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 140, 246);

  btn = pw_card_new(pg, 160, 48, PW_COL_CARD_LT);
  lv_obj_set_pos(btn, 115, 272);
  lv_obj_add_event_cb(btn, life_reset_cb, LV_EVENT_CLICKED, NULL);
  lab = pw_label_new(btn, PW_STR(LIFE_RESET), PW_FNT_MED, PW_COL_DIM);
  lv_obj_center(lab);
}

static void life_build_page_history(void)
{
  lv_obj_t *pg = lv_obj_create(g_l.scroller);
  lv_obj_t *lab;
  lv_obj_t *card;

  lv_obj_set_size(pg, PAGE_W, PAGE_H);
  lv_obj_set_pos(pg, PAGE_W, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  lab = pw_label_new(pg, PW_STR(LIFE_HISTORY), PW_FNT_MED, L_ACC);
  lv_obj_set_pos(lab, 20, 4);

  lab = pw_label_new(pg, PW_STR(LIFE_LOUDNESS_TIME), PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 160, 8);

  card = pw_card_new(pg, 354, 268, PW_COL_CARD);
  lv_obj_set_pos(card, 18, 40);

  lab = pw_label_new(card, PW_STR(LIFE_YLAB_DBFS), PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 12, 6);

  g_l.graph = pw_graph_create(card, 330, 180, L_ACC, PW_COL_CARD, 0);
  lv_obj_set_pos(pw_graph_obj(g_l.graph), 12, 24);
  pw_graph_controls_add(card, g_l.graph, life_gctl_status, NULL);

  lab = pw_label_new(card, PW_STR(LIFE_XLAB_TIME),
                     PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 12, 248);

  lab = pw_label_new(pg, PW_STR(LAST_13S), PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 20, 314);
}

static void life_build_page_help(void)
{
  lv_obj_t *pg = lv_obj_create(g_l.scroller);
  lv_obj_t *lab;
  lv_obj_t *card;

  lv_obj_set_size(pg, PAGE_W, PAGE_H);
  lv_obj_set_pos(pg, PAGE_W * 2, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  lab = pw_label_new(pg, PW_STR(HELP), PW_FNT_MED, L_ACC);
  lv_obj_set_pos(lab, 20, 4);

  card = pw_card_new(pg, 354, 280, PW_COL_CARD);
  lv_obj_set_pos(card, 18, 40);

  lab = pw_label_new(card,
        "Clap in front of the watch: every loud event above "
        "-30 dB is counted (re-arms below -45 dB).\n"
        "The meter shows live loudness in dBFS.\n"
        "Use it to compare applause loudness, or count claps "
        "during a performance.\n"
        "Reset clears the counter.",
        PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 14, 10);
  lv_obj_set_width(lab, 326);
  lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void pw_applause_bench(int on, int page_interval_s, int start_page)
{
  g_bench_on = on;
  g_bench_interval = page_interval_s;
  g_bench_sample = 0;

  if (on && start_page > 0 && g_l.scroller != NULL)
    {
      life_scroll_to(start_page % LIFE_PAGES, false);
    }
}

lv_obj_t *pw_applause_screen(void)
{
  lv_obj_t *btn;
  lv_obj_t *lab;
  int i;

  memset(&g_l, 0, sizeof(g_l));
  pw_thresh_init(&g_l.th, DB_ON, DB_OFF, 1);

  g_l.scr = pw_scr_new();
  lv_obj_add_event_cb(g_l.scr, life_del_cb, LV_EVENT_DELETE, NULL);

  g_l.scroller = pw_topbar(g_l.scr, PW_STR(LIFE_TITLE));
  lv_obj_set_size(g_l.scroller, PAGE_W, PAGE_H);
  lv_obj_set_scroll_dir(g_l.scroller, LV_DIR_HOR);
  lv_obj_set_scroll_snap_x(g_l.scroller, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scrollbar_mode(g_l.scroller, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(g_l.scroller, LV_OBJ_FLAG_SCROLL_ONE);
  lv_obj_add_event_cb(g_l.scroller, life_scroll_end_cb,
                      LV_EVENT_SCROLL_END, NULL);

  life_build_page_meter();
  life_build_page_history();
  life_build_page_help();

  g_l.status = pw_label_new(g_l.scr, PW_STR(LIFE_CLAP_NEAR),
                            PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(g_l.status, 80, 398);
  lv_obj_set_width(g_l.status, 230);
  lv_obj_set_style_text_align(g_l.status, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(g_l.status, LV_LABEL_LONG_WRAP);

  btn = pw_card_new(g_l.scr, 64, 44, PW_COL_CARD);
  lv_obj_set_pos(btn, 4, 396);
  {
    static const int dm = -1;
    lv_obj_add_event_cb(btn, life_arrow_cb, LV_EVENT_CLICKED, (void *)&dm);
    lab = pw_label_new(btn, "<", PW_FNT_XL, PW_COL_DIM);
    lv_obj_center(lab);
  }

  for (i = 0; i < LIFE_PAGES; i++)
    {
      lv_obj_t *d = lv_obj_create(g_l.scr);
      lv_obj_set_size(d, 12, 12);
      lv_obj_set_style_bg_color(d, PW_COL_CARD_LT, 0);
      lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(d, 0, 0);
      lv_obj_remove_flag(d, LV_OBJ_FLAG_SCROLLABLE);
      g_l.dots[i] = d;
      lv_obj_align(d, LV_ALIGN_BOTTOM_MID, (i - (LIFE_PAGES - 1) / 2) * 18,
                   -24);
    }

  btn = pw_card_new(g_l.scr, 64, 44, PW_COL_CARD);
  lv_obj_set_pos(btn, PAGE_W - 4 - 64, 396);
  {
    static const int dp = 1;
    lv_obj_add_event_cb(btn, life_arrow_cb, LV_EVENT_CLICKED, (void *)&dp);
    lab = pw_label_new(btn, ">", PW_FNT_XL, PW_COL_DIM);
    lv_obj_center(lab);
  }

  life_set_page(0);

  pw_scr_set_tick(g_l.scr, life_tick_cb, TICK_MS);

  /* 后台采集线程（删除回调会清 mic_run 停线程） */

  if (!g_bench_on)
    {
      g_l.mic_run = 1;
      g_l.mic_tid = pw_mic_thread_start(g_l.mic_buf, &g_l.mic_run,
                                        &g_l.mic_ready);
    }

  return g_l.scr;
}
