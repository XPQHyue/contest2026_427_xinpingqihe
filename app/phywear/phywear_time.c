/****************************************************************************
 * apps/examples/phywear/phywear_time.c
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

/* T5 计时器（Timers 板块，phyphox 秒表类对齐）。
 *
 * 共用核心：pw_thresh 阈值状态机（滞回 + 自动 re-arm）+ 事件对计时：
 *   第 1 次触发 = t0（开始），第 2 次触发 = t1（停止）→ dt = t1-t0；
 *   下一次触发开启新测量；Clear 清零显示。
 *
 * 三种触发源：
 *   motion   |a|-g 冲击（上升沿，on=0.5g off=0.2g）
 *   light    光暗跳变（下降沿，lux on=20 off=60，阈值可调 ±10）
 *   acoustic 麦克风 RMS 响亮（上升沿，dB on=-30 off=-45，阈值可调 ±3）
 *     —— 后台 pthread 采集 1024 样本块，RMS → dB，不进 LVGL 定时器。
 *
 * 页面（横滑 2 页）：0 Timer（大号 dt + 状态 + 阈值 + Clear）、1 Help。
 * 回归：phywear timebench motion|light|acoustic [秒 [起始页]]
 */

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <math.h>

#include <lvgl/lvgl.h>

#include "phywear_ui.h"
#include "phywear_sensors.h"
#include "pw_analysis.h"
#include "phywear_time.h"
#include "phywear_i18n.h"

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define TIME_PAGES      2
#define PAGE_W          390
#define PAGE_H          342

#define TICK_MS         20

#define TIME_KIND_MOTION  0
#define TIME_KIND_LIGHT   1
#define TIME_KIND_ACOU    2

#define MOT_ON          0.5f          /* g（|a|-1） */
#define MOT_OFF         0.2f

#define LUX_ON_DEF      20.0f         /* lux */
#define LUX_OFF_DEF     60.0f
#define LUX_STEP        10.0f
#define LUX_MIN         5.0f
#define LUX_MAX         200.0f

#define DB_ON_DEF       -30.0f        /* dBFS */
#define DB_OFF_DEF      -45.0f
#define DB_STEP         3.0f
#define DB_MIN          -60.0f
#define DB_MAX          -10.0f

#define T_ACC           PW_ACC_TIME

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct time_ui_s
{
  lv_obj_t *scr;
  lv_obj_t *scroller;
  int       idx;
  lv_obj_t *dots[TIME_PAGES];

  int       kind;

  lv_obj_t *dt_lab;        /* 大号 dt */
  lv_obj_t *state_lab;     /* armed/running/last */
  lv_obj_t *th_lab;        /* 阈值数值（light/acoustic） */
  lv_obj_t *cur_lab;       /* 当前传感器值回显 */

  struct pw_thresh_s th;
  uint32_t  t0;
  int       run;
  float     last_dt;       /* s，-1=无 */
  int       tick;

  /* acoustic */
  int16_t   mic_buf[PW_MIC_SAMPLES];
  volatile int mic_run;
  volatile int mic_ready;
  pthread_t mic_tid;

  lv_obj_t *status;
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct time_ui_s g_t;

static int  g_bench_on;
static int  g_bench_interval;
static long g_bench_sample;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void time_update_status(const char *txt)
{
  lv_label_set_text(g_t.status, txt);
}

static void time_sync_dots(void)
{
  int i;

  for (i = 0; i < TIME_PAGES; i++)
    {
      lv_obj_set_style_bg_color(g_t.dots[i],
                                (i == g_t.idx) ? T_ACC : PW_COL_CARD_LT, 0);
    }
}

static void time_set_page(int idx)
{
  if (idx < 0)
    {
      idx = 0;
    }

  if (idx >= TIME_PAGES)
    {
      idx = TIME_PAGES - 1;
    }

  g_t.idx = idx;
  time_sync_dots();
}

static void time_scroll_to(int idx, bool anim)
{
  if (idx < 0 || idx >= TIME_PAGES)
    {
      return;
    }

  time_set_page(idx);
  lv_obj_scroll_to_x(g_t.scroller, idx * PAGE_W,
                     anim ? LV_ANIM_ON : LV_ANIM_OFF);
}

static void time_arrow_cb(lv_event_t *e)
{
  FAR const int *dp = lv_event_get_user_data(e);

  time_scroll_to(g_t.idx + *dp, true);
}

static void time_scroll_end_cb(lv_event_t *e)
{
  int idx = (int)((lv_obj_get_scroll_x(g_t.scroller) + PAGE_W / 2) /
                  PAGE_W);

  time_set_page(idx);
}

/****************************************************************************
 * Name: time_event
 *
 * Description:
 *   触发事件：首沿开始计时，二沿停止并显示 dt，之后自动开始新测量。
 *
 ****************************************************************************/

static void time_event(void)
{
  uint32_t now = lv_tick_get();

  if (!g_t.run)
    {
      g_t.t0 = now;
      g_t.run = 1;
      lv_label_set_text(g_t.state_lab, PW_STR(TIME_RUNNING));
      lv_obj_set_style_text_color(g_t.state_lab, T_ACC, 0);
    }
  else
    {
      g_t.last_dt = (float)(now - g_t.t0) / 1000.0f;
      g_t.run = 0;
      lv_label_set_text_fmt(g_t.dt_lab, "%.3f", (double)g_t.last_dt);
      lv_label_set_text(g_t.state_lab, PW_STR(TIME_STOPPED));
      lv_obj_set_style_text_color(g_t.state_lab, PW_COL_DIM, 0);
    }
}

/****************************************************************************
 * Name: time_clear_cb
 ****************************************************************************/

static void time_clear_cb(lv_event_t *e)
{
  g_t.run = 0;
  g_t.last_dt = -1.0f;
  lv_label_set_text(g_t.dt_lab, "--");
  lv_label_set_text(g_t.state_lab, PW_STR(TIME_ARMED));
  lv_obj_set_style_text_color(g_t.state_lab, PW_COL_DIM, 0);
}

/****************************************************************************
 * Name: time_th_set / time_th_step_cb
 ****************************************************************************/

static void time_th_set(float on)
{
  if (g_t.kind == TIME_KIND_LIGHT)
    {
      if (on < LUX_MIN)
        {
          on = LUX_MIN;
        }

      if (on > LUX_MAX)
        {
          on = LUX_MAX;
        }

      g_t.th.on = on;
      g_t.th.off = on + (LUX_OFF_DEF - LUX_ON_DEF);
      lv_label_set_text_fmt(g_t.th_lab, "%.0f lux", (double)on);
    }
  else if (g_t.kind == TIME_KIND_ACOU)
    {
      if (on < DB_MIN)
        {
          on = DB_MIN;
        }

      if (on > DB_MAX)
        {
          on = DB_MAX;
        }

      g_t.th.on = on;
      g_t.th.off = on - (DB_ON_DEF - DB_OFF_DEF);
      lv_label_set_text_fmt(g_t.th_lab, "%.0f dB", (double)on);
    }
}

static void time_th_step_cb(lv_event_t *e)
{
  FAR const int *dp = lv_event_get_user_data(e);
  float step = (g_t.kind == TIME_KIND_ACOU) ? DB_STEP : LUX_STEP;

  time_th_set(g_t.th.on + (*dp) * step);
}

/****************************************************************************
 * Name: time_tick_cb
 ****************************************************************************/

static void time_tick_cb(lv_timer_t *timer)
{
  float v;

  if (lv_screen_active() != g_t.scr)
    {
      return;
    }

  if (g_t.kind == TIME_KIND_MOTION)
    {
      struct pw_imu_s imu;
      float m;

      if (g_bench_on)
        {
          /* 合成：每 2.5s 一次 1.2g 冲击（持续 100ms） */
          long ph = g_bench_sample % 125;

          m = (ph < 5) ? 2.2f : 1.0f;   /* |a| g */
        }
      else
        {
          if (pw_sensors_read_imu(&imu) < 0)
            {
              return;
            }

          m = sqrtf((float)imu.ax * imu.ax +
                    (float)imu.ay * imu.ay +
                    (float)imu.az * imu.az) / 1000.0f;
        }

      v = m - 1.0f;

      /* 当前值回显 10Hz 节流（50Hz 文字重排成本高） */
      if (++g_t.tick >= 5)
        {
          g_t.tick = 0;
          lv_label_set_text_fmt(g_t.cur_lab, "|a|-g %+.2f", (double)v);
        }
    }
  else if (g_t.kind == TIME_KIND_LIGHT)
    {
      struct pw_light_s light;

      if (g_bench_on)
        {
          long ph = g_bench_sample % 125;

          v = (ph < 5) ? 5.0f : 150.0f;  /* lux：脉冲期遮光 */
        }
      else
        {
          if (pw_sensors_read_light(&light) < 0)
            {
              return;
            }

          v = (float)light.lux;
        }

      /* 当前值回显 10Hz 节流 */
      if (++g_t.tick >= 5)
        {
          g_t.tick = 0;
          lv_label_set_text_fmt(g_t.cur_lab, "lux %4.0f", (double)v);
        }
    }
  else
    {
      /* acoustic：消费后台线程的 1024 样本块，算 RMS → dBFS */

      if (g_bench_on)
        {
          if ((g_bench_sample % 10) == 0)
            {
              /* 合成块：脉冲期响，否则静 */
              long blk = g_bench_sample / 10;
              long ph = blk % 125;
              float amp = (ph < 5) ? 12000.0f : 100.0f;
              int i;

              for (i = 0; i < PW_MIC_SAMPLES; i++)
                {
                  g_t.mic_buf[i] = (int16_t)(amp *
                    sinf(2.0f * (float)M_PI * 440.0f * (float)i /
                         (float)PW_MIC_RATE));
                }

              g_t.mic_ready = 1;
            }
        }

      if (g_t.mic_ready)
        {
          float sum = 0.0f;
          float rms;
          float db;
          int i;

          for (i = 0; i < PW_MIC_SAMPLES; i++)
            {
              float s = (float)g_t.mic_buf[i] / 32768.0f;

              sum += s * s;
            }

          rms = sqrtf(sum / (float)PW_MIC_SAMPLES);
          db = 20.0f * log10f(rms + 1e-6f);
          g_t.mic_ready = 0;
          lv_label_set_text_fmt(g_t.cur_lab, "%.0f dB", (double)db);

          if (pw_thresh_update(&g_t.th, db))
            {
              time_event();
            }
        }

      g_bench_sample++;
      if (g_bench_interval > 0 &&
          (g_bench_sample % (long)(g_bench_interval * 50)) == 0)
        {
          time_scroll_to((g_t.idx + 1) % TIME_PAGES, false);
        }

      return;
    }

  if (g_bench_on)
    {
      g_bench_sample++;
      if (g_bench_interval > 0 &&
          (g_bench_sample % (long)(g_bench_interval * 50)) == 0)
        {
          time_scroll_to((g_t.idx + 1) % TIME_PAGES, false);
        }
    }

  if (pw_thresh_update(&g_t.th, v))
    {
      time_event();
    }
}

/****************************************************************************
 * Name: time_del_cb
 ****************************************************************************/

static void time_del_cb(lv_event_t *e)
{
  if (g_t.kind == TIME_KIND_ACOU)
    {
      g_t.mic_run = 0;   /* 线程自行退出（静态缓冲，安全） */
    }
}

/****************************************************************************
 * Name: time_th_build_row
 *
 * Description:
 *   阈值行（light/acoustic 可调；motion 显示固定阈值文本）。
 *
 ****************************************************************************/

static void time_th_build_row(lv_obj_t *parent, int y)
{
  static const int delta_m1 = -1;
  static const int delta_p1 = 1;
  lv_obj_t *btn;
  lv_obj_t *lab;

  lab = pw_label_new(parent, (g_t.kind == TIME_KIND_ACOU) ?
                     PW_STR(TIME_THRESH_DB) : PW_STR(TIME_THRESH_LUX),
                     PW_FNT_MED, PW_COL_TEXT);
  lv_obj_set_pos(lab, 12, y + 22);

  if (g_t.kind == TIME_KIND_MOTION)
    {
      g_t.th_lab = pw_label_new(parent, PW_STR(TIME_THRESH_FIXED),
                                PW_FNT_XL, PW_COL_TEXT);
      lv_obj_set_pos(g_t.th_lab, 180, y + 24);
      return;
    }

  btn = pw_card_new(parent, 52, 38, PW_COL_CARD_LT);
  lv_obj_set_pos(btn, 170, y + 18);
  lv_obj_add_event_cb(btn, time_th_step_cb, LV_EVENT_CLICKED,
                      (void *)&delta_m1);
  lab = pw_label_new(btn, "-", PW_FNT_XL, PW_COL_DIM);
  lv_obj_center(lab);

  g_t.th_lab = pw_label_new(parent, "", PW_FNT_XL, PW_COL_TEXT);
  lv_obj_set_pos(g_t.th_lab, 230, y + 24);

  btn = pw_card_new(parent, 52, 38, PW_COL_CARD_LT);
  lv_obj_set_pos(btn, 308, y + 18);
  lv_obj_add_event_cb(btn, time_th_step_cb, LV_EVENT_CLICKED,
                      (void *)&delta_p1);
  lab = pw_label_new(btn, "+", PW_FNT_XL, PW_COL_DIM);
  lv_obj_center(lab);
}

/****************************************************************************
 * Private page builders
 ****************************************************************************/

static void time_build_page_timer(void)
{
  lv_obj_t *pg = lv_obj_create(g_t.scroller);
  lv_obj_t *lab;
  lv_obj_t *btn;

  lv_obj_set_size(pg, PAGE_W, PAGE_H);
  lv_obj_set_pos(pg, 0, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  lab = pw_label_new(pg, PW_STR(TIME_TITLE), PW_FNT_MED, T_ACC);
  lv_obj_set_pos(lab, 20, 4);

  lab = pw_label_new(pg, PW_STR(TIME_EVENT_DT), PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 150, 8);

  /* 大号 dt */

  g_t.dt_lab = pw_label_new(pg, "--", PW_FNT_XXL, T_ACC);
  lv_obj_set_pos(g_t.dt_lab, 64, 56);

  lab = pw_label_new(pg, "s", PW_FNT_MED, PW_COL_DIM);
  lv_obj_set_pos(lab, 320, 90);

  g_t.state_lab = pw_label_new(pg, PW_STR(TIME_ARMED),
                               PW_FNT_MED, PW_COL_DIM);
  lv_obj_set_pos(g_t.state_lab, 64, 128);

  g_t.cur_lab = pw_label_new(pg, "", PW_FNT_MED, PW_COL_TEXT);
  lv_obj_set_pos(g_t.cur_lab, 64, 160);

  /* 阈值行 + Clear */

  time_th_build_row(pg, 190);

  btn = pw_card_new(pg, 160, 48, PW_COL_CARD_LT);
  lv_obj_set_pos(btn, 115, 280);
  lv_obj_add_event_cb(btn, time_clear_cb, LV_EVENT_CLICKED, NULL);
  lab = pw_label_new(btn, PW_STR(TIME_CLEAR), PW_FNT_MED, PW_COL_DIM);
  lv_obj_center(lab);
}

static void time_build_page_help(void)
{
  lv_obj_t *pg = lv_obj_create(g_t.scroller);
  lv_obj_t *lab;
  lv_obj_t *card;

  lv_obj_set_size(pg, PAGE_W, PAGE_H);
  lv_obj_set_pos(pg, PAGE_W, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  lab = pw_label_new(pg, PW_STR(HELP), PW_FNT_MED, T_ACC);
  lv_obj_set_pos(lab, 20, 4);

  card = pw_card_new(pg, 354, 280, PW_COL_CARD);
  lv_obj_set_pos(card, 18, 40);

  if (g_t.kind == TIME_KIND_MOTION)
    {
      lab = pw_label_new(card,
        PW_STR(STOPWATCH_HELP_PAGE),
        PW_FNT_BODY, PW_COL_DIM);
    }
  else if (g_t.kind == TIME_KIND_LIGHT)
    {
      lab = pw_label_new(card,
        PW_STR(LIGHTGATE_HELP_PAGE),
        PW_FNT_BODY, PW_COL_DIM);
    }
  else
    {
      lab = pw_label_new(card,
        PW_STR(ACOUSTIC_HELP_PAGE),
        PW_FNT_BODY, PW_COL_DIM);
    }

  lv_obj_set_pos(lab, 14, 10);
  lv_obj_set_width(lab, 326);
  lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void pw_time_bench(int on, int page_interval_s, int start_page)
{
  g_bench_on = on;
  g_bench_interval = page_interval_s;
  g_bench_sample = 0;

  if (on && start_page > 0 && g_t.scroller != NULL)
    {
      time_scroll_to(start_page % TIME_PAGES, false);
    }
}

static lv_obj_t *time_screen_common(int kind, const char *title)
{
  lv_obj_t *btn;
  lv_obj_t *lab;
  int i;

  memset(&g_t, 0, sizeof(g_t));
  g_t.kind = kind;
  g_t.last_dt = -1.0f;

  if (kind == TIME_KIND_MOTION)
    {
      pw_thresh_init(&g_t.th, MOT_ON, MOT_OFF, 1);
    }
  else if (kind == TIME_KIND_LIGHT)
    {
      pw_thresh_init(&g_t.th, LUX_ON_DEF, LUX_OFF_DEF, 0);
    }
  else
    {
      pw_thresh_init(&g_t.th, DB_ON_DEF, DB_OFF_DEF, 1);
    }

  g_t.scr = pw_scr_new();
  lv_obj_add_event_cb(g_t.scr, time_del_cb, LV_EVENT_DELETE, NULL);

  g_t.scroller = pw_topbar(g_t.scr, title);
  lv_obj_set_size(g_t.scroller, PAGE_W, PAGE_H);
  lv_obj_set_scroll_dir(g_t.scroller, LV_DIR_HOR);
  lv_obj_set_scroll_snap_x(g_t.scroller, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scrollbar_mode(g_t.scroller, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(g_t.scroller, LV_OBJ_FLAG_SCROLL_ONE);
  lv_obj_add_event_cb(g_t.scroller, time_scroll_end_cb,
                      LV_EVENT_SCROLL_END, NULL);

  time_build_page_timer();
  time_build_page_help();

  g_t.status = pw_label_new(g_t.scr, PW_STR(TIME_STATUS_ARMED),
                            PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(g_t.status, 80, 398);
  lv_obj_set_width(g_t.status, 230);
  lv_obj_set_style_text_align(g_t.status, LV_TEXT_ALIGN_CENTER, 0);
  lv_label_set_long_mode(g_t.status, LV_LABEL_LONG_WRAP);

  btn = pw_card_new(g_t.scr, 64, 44, PW_COL_CARD);
  lv_obj_set_pos(btn, 4, 396);
  {
    static const int dm = -1;
    lv_obj_add_event_cb(btn, time_arrow_cb, LV_EVENT_CLICKED, (void *)&dm);
    lab = pw_label_new(btn, "<", PW_FNT_XL, PW_COL_DIM);
    lv_obj_center(lab);
  }

  for (i = 0; i < TIME_PAGES; i++)
    {
      lv_obj_t *d = lv_obj_create(g_t.scr);
      lv_obj_set_size(d, 12, 12);
      lv_obj_set_style_bg_color(d, PW_COL_CARD_LT, 0);
      lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(d, 0, 0);
      lv_obj_remove_flag(d, LV_OBJ_FLAG_SCROLLABLE);
      g_t.dots[i] = d;
      lv_obj_align(d, LV_ALIGN_BOTTOM_MID, (i - (TIME_PAGES - 1) / 2) * 18,
                   -24);
    }

  btn = pw_card_new(g_t.scr, 64, 44, PW_COL_CARD);
  lv_obj_set_pos(btn, PAGE_W - 4 - 64, 396);
  {
    static const int dp = 1;
    lv_obj_add_event_cb(btn, time_arrow_cb, LV_EVENT_CLICKED, (void *)&dp);
    lab = pw_label_new(btn, ">", PW_FNT_XL, PW_COL_DIM);
    lv_obj_center(lab);
  }

  time_set_page(0);
  time_th_set(g_t.th.on);   /* 回显默认阈值 */

  pw_scr_set_tick(g_t.scr, time_tick_cb, TICK_MS);

  return g_t.scr;
}

lv_obj_t *pw_motion_stopwatch_screen(void)
{
  return time_screen_common(TIME_KIND_MOTION, PW_STR(TIME_MOTION_TITLE));
}

lv_obj_t *pw_light_gate_screen(void)
{
  return time_screen_common(TIME_KIND_LIGHT, PW_STR(TIME_LIGHT_TITLE));
}

lv_obj_t *pw_acoustic_gate_screen(void)
{
  lv_obj_t *scr = time_screen_common(TIME_KIND_ACOU, PW_STR(TIME_ACOU_TITLE));

  /* 建屏后起采集线程（删除回调会清 mic_run 停线程；
   * mic_buf 为静态存储，线程退出前最后一次 read 仍安全） */
  if (!g_bench_on)
    {
      g_t.mic_run = 1;
      g_t.mic_tid = pw_mic_thread_start(g_t.mic_buf, &g_t.mic_run,
                                        &g_t.mic_ready);
    }

  return scr;
}
