/****************************************************************************
 * apps/examples/phywear/phywear_raw.c
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

/* 原始传感器板块：4 页横滑（Accelerometer / Gyroscope / Magnetometer /
 * Light），每页一个大数值实时显示，底部翻页指示点 + 左右箭头。
 * 数据经 phywear_sensors 层换算为物理单位（g / dps / mG / lux）。
 *
 * 布局（390×450）：
 *   0..54       顶栏（pw_topbar：返回 + "Raw Sensors"）
 *   54..396     横滑内容区（每页 390×342）
 *   396..450    指示点 + 左右箭头
 */

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>

#include <lvgl/lvgl.h>

#include "phywear_ui.h"
#include "phywear_sensors.h"
#include "phywear_raw.h"
#include "phywear_i18n.h"

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define RAW_PAGES      4
#define RAW_PAGE_W     390
#define RAW_PAGE_H     342

#define RAW_CONTENT_X  0
#define RAW_CONTENT_Y  PW_TOPBAR_H
#define RAW_CONTENT_H  (PW_SCREEN_H - PW_TOPBAR_H - 54)   /* 342 */

/* 数值行参数（3 轴页共用） */

#define RAW_ROW_H      90
#define RAW_ROW_Y0     58
#define RAW_VAL_X      72
#define RAW_TAG_X      18

/* 文本格式串 */

#define FMT_G   "%+.2f g"      /* accel：mg → g */
#define FMT_DPS "%+.1f dps"    /* gyro：mdps → dps */
#define FMT_MG  "%+d mG"       /* mag：mG 整数 */

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct raw_page_s
{
  lv_obj_t *tag[3];     /* X/Y/Z 轴标签 */
  lv_obj_t *val[3];     /* 大数值标签（3 轴页：轴值；光页：主数值） */
  lv_obj_t *sub[2];     /* 次级小字（光页：CH0/CH1） */
};

struct raw_ui_s
{
  lv_obj_t   *scr;      /* 整屏 */
  lv_obj_t   *scroller; /* 横滑容器 */
  lv_obj_t   *dots[RAW_PAGES];
  int         idx;      /* 当前页 */
  struct raw_page_s pg[RAW_PAGES];
};

/****************************************************************************
 * Private Data
 ****************************************************************************/

static struct raw_ui_s g_raw;

static const int g_delta_prev = -1;
static const int g_delta_next = 1;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: raw_sync_dots
 ****************************************************************************/

static void raw_sync_dots(void)
{
  int i;
  lv_color_t on  = PW_ACC_RAW;
  lv_color_t off = PW_COL_CARD_LT;

  for (i = 0; i < RAW_PAGES; i++)
    {
      lv_obj_set_style_bg_color(g_raw.dots[i], (i == g_raw.idx) ? on : off, 0);
    }
}

/****************************************************************************
 * Name: raw_set_page
 ****************************************************************************/

static void raw_set_page(int idx)
{
  if (idx < 0)
    {
      idx = 0;
    }

  if (idx >= RAW_PAGES)
    {
      idx = RAW_PAGES - 1;
    }

  g_raw.idx = idx;
  raw_sync_dots();
}

/****************************************************************************
 * Name: raw_scroll_to
 ****************************************************************************/

static void raw_scroll_to(int idx, bool anim)
{
  if (idx < 0 || idx >= RAW_PAGES)
    {
      return;
    }

  raw_set_page(idx);
  lv_obj_scroll_to_x(g_raw.scroller, idx * RAW_PAGE_W,
                     anim ? LV_ANIM_ON : LV_ANIM_OFF);
}

/****************************************************************************
 * Name: raw_arrow_cb
 ****************************************************************************/

static void raw_arrow_cb(lv_event_t *e)
{
  FAR const int *dp = lv_event_get_user_data(e);

  raw_scroll_to(g_raw.idx + *dp, true);
}

/****************************************************************************
 * Name: raw_scroll_end_cb
 ****************************************************************************/

static void raw_scroll_end_cb(lv_event_t *e)
{
  int32_t x = lv_obj_get_scroll_x(g_raw.scroller);
  int idx = (int)((x + RAW_PAGE_W / 2) / RAW_PAGE_W);

  raw_set_page(idx);
}

/****************************************************************************
 * Name: raw_build_axis_page
 *
 * Description:
 *   构建 3 轴数值页（加速度/陀螺仪/地磁共用）。
 *
 ****************************************************************************/

static void raw_build_axis_page(int pi, int x, const char *name,
                                const char *unit, lv_color_t accent)
{
  lv_obj_t *pg = lv_obj_create(g_raw.scroller);
  lv_obj_t *sub;
  lv_obj_t *uni;
  static const char ax_char[3] = {'X', 'Y', 'Z'};
  int i;

  lv_obj_set_size(pg, RAW_PAGE_W, RAW_PAGE_H);
  lv_obj_set_pos(pg, x, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  /* 页眉：传感器名 + 单位 */

  sub = pw_label_new(pg, name, PW_FNT_LARGE, accent);
  lv_obj_set_pos(sub, 20, 4);

  uni = pw_label_new(pg, unit, PW_FNT_SMALL, PW_COL_DIM);
  lv_obj_align(uni, LV_ALIGN_TOP_RIGHT, -20, 8);

  /* 3 个轴行：轴字母(28) + 大数值(48) */

  for (i = 0; i < 3; i++)
    {
      int rowy = RAW_ROW_Y0 + i * RAW_ROW_H;
      char tagtxt[2];

      tagtxt[0] = ax_char[i];
      tagtxt[1] = '\0';

      g_raw.pg[pi].tag[i] = pw_label_new(pg, tagtxt, PW_FNT_XL,
                                         PW_COL_FAINT);
      lv_obj_set_pos(g_raw.pg[pi].tag[i], RAW_TAG_X, rowy + 34);

      g_raw.pg[pi].val[i] = pw_label_new(pg, "--", PW_FNT_XXL,
                                         PW_COL_TEXT);
      lv_obj_set_pos(g_raw.pg[pi].val[i], RAW_VAL_X, rowy + 8);
    }
}

/****************************************************************************
 * Name: raw_build_light_page
 ****************************************************************************/

static void raw_build_light_page(int pi, int x)
{
  lv_obj_t *pg = lv_obj_create(g_raw.scroller);
  lv_obj_t *sub;
  lv_obj_t *uni;
  lv_obj_t *hint;

  lv_obj_set_size(pg, RAW_PAGE_W, RAW_PAGE_H);
  lv_obj_set_pos(pg, x, 0);
  lv_obj_set_style_bg_opa(pg, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(pg, 0, 0);
  lv_obj_set_style_pad_all(pg, 0, 0);
  lv_obj_remove_flag(pg, LV_OBJ_FLAG_SCROLLABLE);

  sub = pw_label_new(pg, PW_STR(RAW_LIGHT), PW_FNT_LARGE, PW_ACC_LIGHT);
  lv_obj_set_pos(sub, 20, 4);

  uni = pw_label_new(pg, PW_STR(RAW_LUX), PW_FNT_SMALL, PW_COL_DIM);
  lv_obj_align(uni, LV_ALIGN_TOP_RIGHT, -20, 8);

  /* 主数值 48 */

  g_raw.pg[pi].val[0] = pw_label_new(pg, "--", PW_FNT_XXL, PW_COL_TEXT);
  lv_obj_set_pos(g_raw.pg[pi].val[0], 72, 84);

  /* CH0/CH1 次级行 */

  g_raw.pg[pi].sub[0] = pw_label_new(pg, PW_STR(RAW_CH0_HINT),
                                      PW_FNT_MED, PW_COL_DIM);
  lv_obj_set_pos(g_raw.pg[pi].sub[0], 24, 216);

  g_raw.pg[pi].sub[1] = pw_label_new(pg, PW_STR(RAW_CH1_HINT),
                                      PW_FNT_MED, PW_COL_DIM);
  lv_obj_set_pos(g_raw.pg[pi].sub[1], 24, 256);

  hint = pw_label_new(pg, PW_STR(RAW_LIGHT_HINT),
                      PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(hint, 24, 310);
}

/****************************************************************************
 * Name: raw_tick_cb
 *
 * Description:
 *   每 100ms 只刷新当前页对应的传感器（省 I2C 流量）。
 *
 ****************************************************************************/

static void raw_tick_cb(lv_timer_t *timer)
{
  struct pw_imu_s imu;
  struct pw_mag_s mag;
  struct pw_light_s light;
  int pi = g_raw.idx;

  /* 仅活动屏刷新 */

  if (lv_screen_active() != g_raw.scr)
    {
      return;
    }

  switch (pi)
    {
      case 0:   /* Accelerometer */
        if (pw_sensors_read_imu(&imu) == 0)
          {
            lv_label_set_text_fmt(g_raw.pg[pi].val[0], FMT_G,
                                  imu.ax / 1000.0f);
            lv_label_set_text_fmt(g_raw.pg[pi].val[1], FMT_G,
                                  imu.ay / 1000.0f);
            lv_label_set_text_fmt(g_raw.pg[pi].val[2], FMT_G,
                                  imu.az / 1000.0f);
          }
        break;

      case 1:   /* Gyroscope */
        if (pw_sensors_read_imu(&imu) == 0)
          {
            lv_label_set_text_fmt(g_raw.pg[pi].val[0], FMT_DPS,
                                  imu.gx / 1000.0f);
            lv_label_set_text_fmt(g_raw.pg[pi].val[1], FMT_DPS,
                                  imu.gy / 1000.0f);
            lv_label_set_text_fmt(g_raw.pg[pi].val[2], FMT_DPS,
                                  imu.gz / 1000.0f);
          }
        break;

      case 2:   /* Magnetometer */
        if (pw_sensors_read_mag(&mag) == 0)
          {
            lv_label_set_text_fmt(g_raw.pg[pi].val[0], FMT_MG, mag.x);
            lv_label_set_text_fmt(g_raw.pg[pi].val[1], FMT_MG, mag.y);
            lv_label_set_text_fmt(g_raw.pg[pi].val[2], FMT_MG, mag.z);
          }
        break;

      case 3:   /* Light */
        if (pw_sensors_read_light(&light) == 0)
          {
            lv_label_set_text_fmt(g_raw.pg[pi].val[0], "%d lux",
                                  light.lux);
            lv_label_set_text_fmt(g_raw.pg[pi].sub[0], PW_STR(RAW_CH0_VISIR_FMT),
                                  light.ch0);
            lv_label_set_text_fmt(g_raw.pg[pi].sub[1], PW_STR(RAW_CH1_IR_FMT),
                                  light.ch1);
          }
        break;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

lv_obj_t *pw_raw_screen(void)
{
  lv_obj_t *cont;
  lv_obj_t *btn;
  lv_obj_t *lab;
  int i;

  memset(&g_raw, 0, sizeof(g_raw));

  g_raw.scr = pw_scr_new();

  /* 顶栏 + 内容容器（横滑区） */

  cont = pw_topbar(g_raw.scr, PW_STR(UI_RAW_SENSORS));
  lv_obj_set_size(cont, PW_SCREEN_W, RAW_CONTENT_H);
  lv_obj_set_pos(cont, RAW_CONTENT_X, RAW_CONTENT_Y);
  g_raw.scroller = cont;

  lv_obj_set_scroll_dir(cont, LV_DIR_HOR);
  lv_obj_set_scroll_snap_x(cont, LV_SCROLL_SNAP_CENTER);
  lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_flag(cont, LV_OBJ_FLAG_SCROLL_ONE);
  lv_obj_add_event_cb(cont, raw_scroll_end_cb, LV_EVENT_SCROLL_END, NULL);

  /* 4 页 */

  raw_build_axis_page(0, 0,          PW_STR(RAW_ACCEL), "g",
                      PW_ACC_ACC);
  raw_build_axis_page(1, RAW_PAGE_W, PW_STR(RAW_GYRO), "dps",
                      PW_ACC_GYRO);
  raw_build_axis_page(2, RAW_PAGE_W * 2, PW_STR(RAW_MAG), "mG",
                      PW_ACC_MAG);
  raw_build_light_page(3, RAW_PAGE_W * 3);

  /* 底部：左箭头 + 指示点 + 右箭头 */

  btn = pw_card_new(g_raw.scr, 64, 44, PW_COL_CARD);
  lv_obj_set_pos(btn, 4, RAW_CONTENT_Y + RAW_CONTENT_H + 4);
  lv_obj_add_event_cb(btn, raw_arrow_cb, LV_EVENT_CLICKED,
                      (void *)&g_delta_prev);
  lab = pw_label_new(btn, "<", PW_FNT_XL, PW_COL_DIM);
  lv_obj_center(lab);

  for (i = 0; i < RAW_PAGES; i++)
    {
      lv_obj_t *d = lv_obj_create(g_raw.scr);

      lv_obj_set_size(d, 12, 12);
      lv_obj_set_style_bg_color(d, PW_COL_CARD_LT, 0);
      lv_obj_set_style_radius(d, LV_RADIUS_CIRCLE, 0);
      lv_obj_set_style_border_width(d, 0, 0);
      lv_obj_remove_flag(d, LV_OBJ_FLAG_SCROLLABLE);
      g_raw.dots[i] = d;
      lv_obj_align(d, LV_ALIGN_BOTTOM_MID, (i - (RAW_PAGES - 1) / 2) * 18,
                   -34);
    }

  btn = pw_card_new(g_raw.scr, 64, 44, PW_COL_CARD);
  lv_obj_set_pos(btn, PW_SCREEN_W - 4 - 64,
                 RAW_CONTENT_Y + RAW_CONTENT_H + 4);
  lv_obj_add_event_cb(btn, raw_arrow_cb, LV_EVENT_CLICKED,
                      (void *)&g_delta_next);
  lab = pw_label_new(btn, ">", PW_FNT_XL, PW_COL_DIM);
  lv_obj_center(lab);

  /* 初始状态 + 周期刷新 */

  raw_set_page(0);
  pw_scr_set_tick(g_raw.scr, raw_tick_cb, 100);

  return g_raw.scr;
}
