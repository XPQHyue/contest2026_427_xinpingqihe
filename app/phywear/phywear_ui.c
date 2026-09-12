/****************************************************************************
 * apps/examples/phywear/phywear_ui.c
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

/* UI 骨架：
 *   - 屏幕管理：栈式导航（根屏主菜单 → 板块列表/实验页），返回弹栈删除。
 *   - pw_topbar：顶栏（返回箭头 + 标题），返回下方内容区容器。
 *   - 主菜单：板块宫格（2×4），板块强调色 + 名称 + 状态说明。
 *   - pw_board_list：板块实验列表（实现项可点开，未实现项置灰标注）。
 *
 * 屏幕周期刷新统一走 pw_scr_set_tick() 挂 LVGL 定时器，屏幕删除时自动
 * 释放，回调内自行判断 lv_screen_active() 避免后台空转。
 */

#include <nuttx/config.h>

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include <lvgl/lvgl.h>

#include "phywear_ui.h"
#include "phywear_i18n.h"
#include "pw_graph.h"
#include "phywear_raw.h"
#include "phywear_pend.h"
#include "phywear_spec.h"
#include "phywear_spring.h"
#include "phywear_centri.h"
#include "phywear_incline.h"
#include "phywear_ruler.h"
#include "phywear_time.h"
#include "phywear_life.h"

/****************************************************************************
 * Private Definitions
 ****************************************************************************/

#define UI_STACK_MAX   8

/* 主菜单宫格几何 */

#define MENU_TILE_W    183
#define MENU_TILE_H    88
#define MENU_GAP       8
#define MENU_X0        8
#define MENU_Y0        70

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_obj_t *g_scr_stack[UI_STACK_MAX];
static int       g_scr_n;

/* 顶栏运行计时（phyphox 工具栏语义）：进入非根屏即开始计时，回到根屏归零；
 * 计时标签随顶栏创建，配栈内句柄避免悬垂指针。 */
static lv_obj_t  *g_pending_timer;                  /* 当前顶栏刚创建的计时标签 */
static lv_obj_t  *g_scr_timer[UI_STACK_MAX];        /* 各屏的计时标签 */
static uint32_t   g_meas_start;                     /* 0 = 未在测量 */
static bool       g_timer_started;

/* 板块内容：各实验行（未实现项 open == NULL，列表页置灰）
 * 注：使用 PW_STR() 运行时获取，不能放 static 初始化器。
 */

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static lv_obj_t *ui_mech_open(void);
static lv_obj_t *ui_acou_open(void);
static lv_obj_t *ui_tool_open(void);
static lv_obj_t *ui_time_open(void);
static lv_obj_t *ui_every_open(void);
static lv_obj_t *ui_custom_open(void);
static lv_obj_t *ui_ai_open(void);

/* 设置/语言/关于（phyphox 极简设置思想） */
static void ui_settings_btn_cb(lv_event_t *e);
static void ui_about_open_cb(lv_event_t *e);
static void ui_lang_cb(lv_event_t *e);
static void pw_ui_rebuild_root(void *ud);

/****************************************************************************
 * Private Data (continued)
 ****************************************************************************/

struct pw_board_s
{
  const char *name;
  const char *info;                 /* 菜单副行 */
  uint32_t    color;
  bool        live;                 /* true=白字可进；false=灰字规划中 */
  lv_obj_t *(*open)(void);
};

/* g_boards[] 已改为 pw_ui_root() 内运行时构建（需要 PW_STR()） */

/* NBOARDS 已移除：改用各函数局部 nboards 变量 */

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: ui_del_cb
 *
 * Description:
 *   屏幕删除时回收其周期刷新定时器（user_data = lv_timer_t*）。
 *
 ****************************************************************************/

static void ui_del_cb(lv_event_t *e)
{
  lv_timer_t *t = lv_event_get_user_data(e);

  if (t != NULL)
    {
      lv_timer_del(t);
    }
}

/****************************************************************************
 * Name: ui_back_cb
 ****************************************************************************/

static void ui_back_cb(lv_event_t *e)
{
  pw_scr_back();
}

/****************************************************************************
 * Name: ui_tile_cb
 ****************************************************************************/

static void ui_tile_cb(lv_event_t *e)
{
  lv_obj_t *(*open)(void) = lv_event_get_user_data(e);

  if (open != NULL)
    {
      pw_scr_open(open());
    }
}

/****************************************************************************
 * Name: ui_item_cb
 ****************************************************************************/

static void ui_item_cb(lv_event_t *e)
{
  FAR const struct pw_exp_s *it = lv_event_get_user_data(e);

  if (it != NULL && it->open != NULL)
    {
      pw_scr_open(it->open());
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

lv_obj_t *pw_scr_new(void)
{
  lv_obj_t *scr = lv_obj_create(NULL);

  lv_obj_set_style_bg_color(scr, PW_COL_BG, 0);
  lv_obj_set_style_pad_all(scr, 0, 0);
  lv_obj_set_style_border_width(scr, 0, 0);
  lv_obj_set_style_radius(scr, 0, 0);
  lv_obj_remove_flag(scr, LV_OBJ_FLAG_SCROLLABLE);

  return scr;
}

void pw_scr_open(lv_obj_t *scr)
{
  if (scr == NULL)
    {
      return;
    }

  if (g_scr_n >= UI_STACK_MAX)
    {
      /* 栈满：丢弃最早一屏（释放资源） */

      lv_obj_delete(g_scr_stack[0]);
      memmove(&g_scr_stack[0], &g_scr_stack[1],
              (UI_STACK_MAX - 1) * sizeof(lv_obj_t *));
      g_scr_n--;
    }

  g_scr_stack[g_scr_n] = scr;
  g_scr_timer[g_scr_n] = g_pending_timer;
  g_pending_timer = NULL;

  /* 首次离开根屏（压到根屏之上）开始测量计时 */
  if (g_scr_n == 1)
    {
      g_meas_start = lv_tick_get();
    }

  g_scr_n++;
  lv_screen_load(scr);
}

void pw_scr_back(void)
{
  if (g_scr_n <= 1)
    {
      return;   /* 根屏不弹 */
    }

  g_scr_n--;
  lv_screen_load(g_scr_stack[g_scr_n - 1]);
  lv_obj_delete(g_scr_stack[g_scr_n]);
  g_scr_timer[g_scr_n] = NULL;

  /* 回到根屏：停止测量计时 */
  if (g_scr_n == 1)
    {
      g_meas_start = 0;
    }
}

/* 顶栏运行计时刷新（1Hz）：仅非根屏显示 mm:ss */
static void ui_timer_tick(lv_timer_t *t)
{
  uint32_t el;
  char buf[16];

  (void)t;

  if (g_scr_n <= 1 || g_meas_start == 0 || g_scr_timer[g_scr_n - 1] == NULL)
    {
      return;
    }

  el = (lv_tick_get() - g_meas_start) / 1000u;
  snprintf(buf, sizeof(buf), "%u:%02u",
           (unsigned)(el / 60u), (unsigned)(el % 60u));
  lv_label_set_text(g_scr_timer[g_scr_n - 1], buf);
}

void pw_scr_set_tick(lv_obj_t *scr, lv_timer_cb_t cb, uint32_t period_ms)
{
  lv_timer_t *t;

  if (scr == NULL || cb == NULL)
    {
      return;
    }

  t = lv_timer_create(cb, period_ms, NULL);
  lv_obj_add_event_cb(scr, ui_del_cb, LV_EVENT_DELETE, t);
}

lv_obj_t *pw_topbar(lv_obj_t *scr, const char *title)
{
  lv_obj_t *bar;
  lv_obj_t *btn;
  lv_obj_t *lab;
  lv_obj_t *cont;

  /* 顶栏容器 */

  bar = lv_obj_create(scr);
  lv_obj_set_size(bar, PW_SCREEN_W, PW_TOPBAR_H);
  lv_obj_set_pos(bar, 0, 0);
  lv_obj_set_style_bg_opa(bar, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_set_style_pad_all(bar, 0, 0);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);

  /* 返回箭头（左侧 88px 触控区） */

  btn = lv_obj_create(bar);
  lv_obj_set_size(btn, 88, PW_TOPBAR_H);
  lv_obj_set_pos(btn, 0, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(btn, ui_back_cb, LV_EVENT_CLICKED, NULL);

  lab = pw_label_new(btn, "<", PW_FNT_XL, PW_COL_DIM);
  lv_obj_center(lab);

  /* 标题 */

  lab = pw_label_new(bar, title, PW_FNT_LARGE, PW_COL_TEXT);
  lv_obj_align(lab, LV_ALIGN_CENTER, 0, 0);

  /* 顶栏右侧运行计时（phyphox 工具栏语义；进入测量屏后 1Hz 刷新 mm:ss） */

  lab = pw_label_new(bar, "", PW_FNT_SMALL, PW_COL_DIM);
  lv_obj_align(lab, LV_ALIGN_RIGHT_MID, -18, 0);
  g_pending_timer = lab;

  /* 内容区（下方全部区域，调用方可改尺寸） */

  cont = lv_obj_create(scr);
  lv_obj_set_size(cont, PW_SCREEN_W, PW_SCREEN_H - PW_TOPBAR_H);
  lv_obj_set_pos(cont, 0, PW_TOPBAR_H);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

  return cont;
}

lv_obj_t *pw_card_new(lv_obj_t *parent, int w, int h, lv_color_t bg)
{
  lv_obj_t *card = lv_obj_create(parent);

  lv_obj_set_size(card, w, h);
  lv_obj_set_style_bg_color(card, bg, 0);
  lv_obj_set_style_border_width(card, 0, 0);
  lv_obj_set_style_radius(card, 14, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  return card;
}

lv_obj_t *pw_label_new(lv_obj_t *parent, const char *text,
                       const lv_font_t *font, lv_color_t color)
{
  lv_obj_t *lab = lv_label_create(parent);

  lv_label_set_text(lab, text);
  lv_obj_set_style_text_font(lab, font, 0);
  lv_obj_set_style_text_color(lab, color, 0);

  return lab;
}

/****************************************************************************
 * 共用图表工具条（X-/X+/Y-/Y+/Fit/Auto）
 ****************************************************************************/

#define GCTL_MAX   64

struct pw_gctl_s
{
  struct pw_graph_s *g;
  int               op;
  pw_gctl_status_cb status;
  void             *ud;
};

static struct pw_gctl_s g_gctl[GCTL_MAX];
static int              g_gctl_n;

static void ui_gctl_cb(lv_event_t *e)
{
  struct pw_gctl_s *c = lv_event_get_user_data(e);

  if (c == NULL || c->g == NULL)
    {
      return;
    }

  switch (c->op)
    {
      case 0:
        pw_graph_zoom(c->g, PW_GRAPH_AXIS_X, 0.7f);
        break;
      case 1:
        pw_graph_zoom(c->g, PW_GRAPH_AXIS_X, 1.4f);
        break;
      case 2:
        pw_graph_zoom(c->g, PW_GRAPH_AXIS_Y, 0.7f);
        break;
      case 3:
        pw_graph_zoom(c->g, PW_GRAPH_AXIS_Y, 1.4f);
        break;
      case 4:
        pw_graph_fit(c->g);
        break;
      case 5:
        pw_graph_set_auto(c->g, 1);
        break;
    }

  if (c->status != NULL)
    {
      float xmin;
      float xmax;
      float ymin;
      float ymax;
      char msg[64];

      pw_graph_get_range(c->g, &xmin, &xmax, &ymin, &ymax);
      snprintf(msg, sizeof(msg), "Range X %.2f..%.2f Y %.2f..%.2f",
               (double)xmin, (double)xmax, (double)ymin, (double)ymax);
      c->status(c->ud, msg);
    }
}

void pw_graph_controls_add(lv_obj_t *card, struct pw_graph_s *g,
                           pw_gctl_status_cb status, void *ud)
{
  /* 按钮文案走 i18n：X-/X+/Y-/Y+ 为符号，Fit/Auto 需翻译。
   * 注意不能做成 static 数组（PW_STR 是函数调用，非编译期常量）。 */
  static const int labid[] =
  {
    0, 0, 0, 0, PW_STR_GCTL_FIT, PW_STR_GCTL_AUTO
  };
  static const char *const labsym[] = {"X-", "X+", "Y-", "Y+", NULL, NULL};
  int i;

  for (i = 0; i < 6; i++)
    {
      struct pw_gctl_s *c;
      lv_obj_t *btn;
      lv_obj_t *lab;

      if (g_gctl_n >= GCTL_MAX)
        {
          return;
        }

      c = &g_gctl[g_gctl_n++];
      c->g = g;
      c->op = i;
      c->status = status;
      c->ud = ud;

      btn = pw_card_new(card, 50, 28, PW_COL_CARD_LT);
      lv_obj_set_pos(btn, 12 + i * 55, 212);
      lv_obj_add_event_cb(btn, ui_gctl_cb, LV_EVENT_CLICKED, c);
      lab = pw_label_new(btn,
                         labsym[i] != NULL ? labsym[i] : pw_str(labid[i]),
                         PW_FNT_BODY, PW_COL_DIM);
      lv_obj_center(lab);
    }
}

lv_obj_t *pw_board_list(const char *title, lv_color_t accent,
                        FAR const struct pw_exp_s *items, int nitems)
{
  lv_obj_t *scr;
  lv_obj_t *cont;
  int i;
  int y;

  scr = pw_scr_new();
  cont = pw_topbar(scr, title);

  y = MENU_GAP;

  for (i = 0; i < nitems; i++)
    {
      FAR const struct pw_exp_s *it = &items[i];
      bool ok = (it->open != NULL);
      lv_obj_t *row;
      lv_obj_t *strip;
      lv_obj_t *lab;
      lv_color_t namecol = ok ? PW_COL_TEXT : PW_COL_FAINT;
      lv_color_t desccol = ok ? PW_COL_DIM   : PW_COL_FAINT;

      row = pw_card_new(cont, PW_SCREEN_W - 2 * MENU_X0, 76,
                        PW_COL_CARD);
      lv_obj_set_pos(row, MENU_X0, y);
      lv_obj_set_style_bg_color(row, PW_COL_CARD_LT, LV_STATE_PRESSED);

      if (ok)
        {
          /* 左侧强调条 */

          strip = lv_obj_create(row);
          lv_obj_set_size(strip, 4, 48);
          lv_obj_set_pos(strip, 10, 14);
          lv_obj_set_style_bg_color(strip, accent, 0);
          lv_obj_set_style_radius(strip, 2, 0);
          lv_obj_set_style_border_width(strip, 0, 0);
          lv_obj_remove_flag(strip, LV_OBJ_FLAG_SCROLLABLE);

          lab = pw_label_new(row, it->name, PW_FNT_MED, namecol);
          lv_obj_set_pos(lab, 30, 12);

          lab = pw_label_new(row, it->desc, PW_FNT_BODY, desccol);
          lv_obj_set_pos(lab, 30, 48);

          lab = pw_label_new(row, ">", PW_FNT_MED, PW_COL_DIM);
          lv_obj_align(lab, LV_ALIGN_RIGHT_MID, -18, 0);

          lv_obj_add_event_cb(row, ui_item_cb, LV_EVENT_CLICKED,
                              (void *)it);
        }
      else
        {
          /* 未实现项：置灰 + 角标 */

          lab = pw_label_new(row, it->name, PW_FNT_MED, namecol);
          lv_obj_set_pos(lab, 22, 12);

          lab = pw_label_new(row, it->desc, PW_FNT_BODY, desccol);
          lv_obj_set_pos(lab, 22, 48);

          lab = pw_label_new(row, PW_STR(UI_PLANNED), PW_FNT_BODY, PW_COL_FAINT);
          lv_obj_align(lab, LV_ALIGN_RIGHT_MID, -14, 0);
        }

      y += 76 + MENU_GAP;
    }

  return scr;
}

/****************************************************************************
 * 板块打开函数
 ****************************************************************************/

static lv_obj_t *ui_mech_open(void)
{
  const struct pw_exp_s items[] =
  {
    { PW_STR(EXP_PENDULUM_NAME), PW_STR(EXP_PENDULUM_DESC), pw_pendulum_screen },
    { PW_STR(SPRING_TITLE),      PW_STR(EXP_SPRING_DESC),   pw_spring_screen },
    { PW_STR(CENTRI_TITLE),      PW_STR(EXP_CENTRI_DESC),   pw_centri_screen },
    { PW_STR(EXP_BOUNCE_NAME),   PW_STR(EXP_BOUNCE_DESC),   NULL },
  };
  return pw_board_list(PW_STR(UI_MECHANICS), PW_ACC_MECH,
                       items, sizeof(items) / sizeof(items[0]));
}

static lv_obj_t *ui_acou_open(void)
{
  const struct pw_exp_s items[] =
  {
    { PW_STR(SPEC_TITLE),       PW_STR(EXP_SPECTRUM_DESC), pw_spec_mic_screen },
    { PW_STR(EXP_PITCH_NAME),   PW_STR(EXP_PITCH_DESC),    NULL },
    { PW_STR(EXP_TONE_NAME),    PW_STR(EXP_TONE_DESC),     NULL },
    { PW_STR(EXP_DOPPLER_NAME), PW_STR(EXP_DOPPLER_DESC),  NULL },
  };
  return pw_board_list(PW_STR(UI_ACOUSTICS), PW_ACC_ACOU,
                       items, sizeof(items) / sizeof(items[0]));
}

static lv_obj_t *ui_tool_open(void)
{
  const struct pw_exp_s items[] =
  {
    { PW_STR(EXP_ACCEL_SPEC_NAME), PW_STR(EXP_ACCEL_SPEC_DESC), pw_spec_accel_screen },
    { PW_STR(INCLINE_TITLE),       PW_STR(EXP_INCLINE_DESC),    pw_incline_screen },
    { PW_STR(RULER_TITLE),         PW_STR(EXP_RULER_DESC),      pw_ruler_screen },
    { PW_STR(EXP_MAG_SPEC_NAME),   PW_STR(EXP_MAG_SPEC_DESC),   pw_spec_mag_screen },
  };
  return pw_board_list(PW_STR(UI_TOOLS), PW_ACC_TOOL,
                       items, sizeof(items) / sizeof(items[0]));
}

static lv_obj_t *ui_time_open(void)
{
  const struct pw_exp_s items[] =
  {
    { PW_STR(EXP_MOTION_SW_NAME),  PW_STR(EXP_MOTION_SW_DESC),  pw_motion_stopwatch_screen },
    { PW_STR(EXP_LIGHT_GATE_NAME), PW_STR(EXP_LIGHT_GATE_DESC), pw_light_gate_screen },
    { PW_STR(EXP_ACOU_GATE_NAME),  PW_STR(EXP_ACOU_GATE_DESC),  pw_acoustic_gate_screen },
  };
  return pw_board_list(PW_STR(UI_TIMERS), PW_ACC_TIME,
                       items, sizeof(items) / sizeof(items[0]));
}

static lv_obj_t *ui_every_open(void)
{
  const struct pw_exp_s items[] =
  {
    { PW_STR(LIFE_TITLE), PW_STR(EXP_APPLAUSE_DESC), pw_applause_screen },
  };
  return pw_board_list(PW_STR(UI_EVERYDAY), PW_ACC_EVERY,
                       items, sizeof(items) / sizeof(items[0]));
}

static lv_obj_t *ui_custom_open(void)
{
  const struct pw_exp_s items[] =
  {
    { PW_STR(EXP_BUILDER_NAME), PW_STR(EXP_BUILDER_DESC), NULL },
  };
  return pw_board_list(PW_STR(UI_CUSTOM), PW_ACC_CUSTOM,
                       items, sizeof(items) / sizeof(items[0]));
}

static lv_obj_t *ui_ai_open(void)
{
  const struct pw_exp_s items[] =
  {
    { PW_STR(UI_AI_COACH), PW_STR(EXP_COACH_DESC), NULL },
  };
  return pw_board_list(PW_STR(UI_AI_COACH), PW_ACC_AI,
                       items, sizeof(items) / sizeof(items[0]));
}

/****************************************************************************
 * 设置 / 语言 / 关于（phyphox 极简设置思想：语言 + 关于/许可）
 ****************************************************************************/

static void ui_lang_cb(lv_event_t *e)
{
  bool zh = (lv_event_get_user_data(e) != NULL);

  pw_i18n_set_zh(zh);
  /* 延迟到下一轮回调重建根屏，避免在对象删除回调里改树 *  */
  lv_async_call(pw_ui_rebuild_root, NULL);
}

static void ui_about_open_cb(lv_event_t *e)
{
  pw_scr_open(pw_about_screen());
}

static void ui_settings_btn_cb(lv_event_t *e)
{
  pw_scr_open(pw_settings_screen());
}

static void pw_ui_rebuild_root(void *ud)
{
  /* 清空导航栈（含当前根屏与任何上层屏）并重建根屏，刷新语言文案 */
  while (g_scr_n > 0)
    {
      g_scr_n--;
      if (g_scr_stack[g_scr_n] != NULL)
        {
          lv_obj_delete(g_scr_stack[g_scr_n]);
        }
    }

  pw_ui_root();
}

lv_obj_t *pw_settings_screen(void)
{
  lv_obj_t *scr;
  lv_obj_t *cont;
  lv_obj_t *card;
  lv_obj_t *lab;
  lv_obj_t *btn;
  bool zh;

  scr = pw_scr_new();
  cont = pw_topbar(scr, PW_STR(UI_SETTINGS));

  zh = pw_i18n_is_zh();

  /* --- 语言 --- */

  card = pw_card_new(cont, PW_SCREEN_W - 2 * MENU_X0, 118, PW_COL_CARD);
  lv_obj_set_pos(card, MENU_X0, MENU_GAP);

  lab = pw_label_new(card, PW_STR(UI_LANGUAGE), PW_FNT_MED, PW_COL_TEXT);
  lv_obj_set_pos(lab, 16, 12);

  lab = pw_label_new(card, PW_STR(LANGUAGE_HINT), PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 16, 46);

  /* 英文按钮 */
  btn = pw_card_new(card, 120, 40, zh ? PW_COL_CARD_LT : PW_COL_CARD);
  lv_obj_set_pos(btn, 16, 70);
  lv_obj_set_style_bg_color(btn, zh ? PW_COL_CARD : PW_COL_CARD_LT,
                            LV_STATE_PRESSED);
  lab = pw_label_new(btn, PW_STR(LANG_EN), PW_FNT_MED,
                     zh ? PW_COL_DIM : PW_ACC_ACTIVE);
  lv_obj_center(lab);
  lv_obj_add_event_cb(btn, ui_lang_cb, LV_EVENT_CLICKED, NULL);

  /* 中文按钮 */
  btn = pw_card_new(card, 120, 40, zh ? PW_COL_CARD_LT : PW_COL_CARD);
  lv_obj_set_pos(btn, 168, 70);
  lv_obj_set_style_bg_color(btn, zh ? PW_COL_CARD_LT : PW_COL_CARD,
                            LV_STATE_PRESSED);
  lab = pw_label_new(btn, PW_STR(LANG_ZH), PW_FNT_MED,
                     zh ? PW_ACC_ACTIVE : PW_COL_DIM);
  lv_obj_center(lab);
  lv_obj_add_event_cb(btn, ui_lang_cb, LV_EVENT_CLICKED, (void *)1);

  /* --- 关于 --- */

  card = pw_card_new(cont, PW_SCREEN_W - 2 * MENU_X0, 64, PW_COL_CARD);
  lv_obj_set_pos(card, MENU_X0, MENU_GAP + 130);

  lab = pw_label_new(card, PW_STR(UI_ABOUT), PW_FNT_MED, PW_COL_TEXT);
  lv_obj_set_pos(lab, 16, 20);

  lab = pw_label_new(card, ">", PW_FNT_MED, PW_COL_DIM);
  lv_obj_align(lab, LV_ALIGN_RIGHT_MID, -18, 0);
  lv_obj_add_event_cb(card, ui_about_open_cb, LV_EVENT_CLICKED, NULL);

  return scr;
}

lv_obj_t *pw_about_screen(void)
{
  lv_obj_t *scr;
  lv_obj_t *cont;
  lv_obj_t *card;
  lv_obj_t *lab;

  scr = pw_scr_new();
  cont = pw_topbar(scr, PW_STR(UI_ABOUT));

  card = pw_card_new(cont, PW_SCREEN_W - 2 * MENU_X0, 330, PW_COL_CARD);
  lv_obj_set_pos(card, MENU_X0, MENU_GAP);

  lab = pw_label_new(card, "PhyWear v0.1", PW_FNT_LARGE, PW_ACC_ACTIVE);
  lv_obj_set_pos(lab, 16, 14);

  lab = pw_label_new(card, PW_STR(ABOUT_BODY), PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 16, 54);
  lv_obj_set_width(lab, PW_SCREEN_W - 2 * MENU_X0 - 32);
  lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);

  return scr;
}

/****************************************************************************
 * Name: pw_ui_root
 *
 * Description:
 *   主菜单宫格根屏。
 *
 ****************************************************************************/

void pw_ui_root(void)
{
  const struct pw_board_s boards[] =
  {
    { PW_STR(UI_RAW_SENSORS), PW_STR(UI_RAW_DESC),    0x4fc3f7, true,  pw_raw_screen },
    { PW_STR(UI_MECHANICS),   PW_STR(UI_MECH_DESC),   0x81c784, true,  ui_mech_open },
    { PW_STR(UI_ACOUSTICS),   PW_STR(UI_ACOU_DESC),   0xffb74d, true,  ui_acou_open },
    { PW_STR(UI_TOOLS),       PW_STR(UI_TOOLS_DESC),  0xba68c8, true,  ui_tool_open },
    { PW_STR(UI_TIMERS),      PW_STR(UI_TIMERS_DESC), 0x4dd0e1, true,  ui_time_open },
    { PW_STR(UI_EVERYDAY),    PW_STR(UI_EVERY_DESC),  0xff8a65, true,  ui_every_open },
    { PW_STR(UI_CUSTOM),      PW_STR(UI_CUSTOM_DESC), 0x90a4ae, false, ui_custom_open },
    { PW_STR(UI_AI_COACH),    PW_STR(UI_AI_DESC),     0xf06292, false, ui_ai_open },
  };
  const int nboards = sizeof(boards) / sizeof(boards[0]);
  lv_obj_t *scr;
  lv_obj_t *lab;
  lv_obj_t *btn;
  int i;

  /* 可重复调用（语言切换后重建根屏）：清空现存栈再构建 */
  g_scr_n = 0;
  g_pending_timer = NULL;

  /* 一次性注册顶栏运行计时器（1Hz） */
  if (!g_timer_started)
    {
      g_timer_started = true;
      lv_timer_create(ui_timer_tick, 1000, NULL);
    }

  scr = pw_scr_new();

  /* 标题行（左边缘圆弧屏切角：文本需右移进安全区） */

  lab = pw_label_new(scr, PW_STR(UI_PHYWEAR), PW_FNT_XL, PW_COL_TEXT);
  lv_obj_set_pos(lab, MENU_X0 + 28, 14);

  lab = pw_label_new(scr, PW_STR(UI_SUBTITLE),
                     PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, MENU_X0 + 28, 46);

  /* 右上角设置入口（phyphox 溢出菜单思想的极简版） */

  btn = lv_obj_create(scr);
  lv_obj_set_size(btn, 60, 44);
  lv_obj_set_pos(btn, PW_SCREEN_W - 60 - 20, 10);
  lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(btn, ui_settings_btn_cb, LV_EVENT_CLICKED, NULL);

  lab = pw_label_new(btn, "...", PW_FNT_XL, PW_COL_DIM);
  lv_obj_center(lab);

  /* 板块宫格 2×4 */

  for (i = 0; i < nboards; i++)
    {
      int col = i % 2;
      int row = i / 2;
      int x = MENU_X0 + col * (MENU_TILE_W + MENU_GAP);
      int y = MENU_Y0 + row * (MENU_TILE_H + MENU_GAP);
      const struct pw_board_s *b = &boards[i];
      lv_obj_t *tile;
      lv_obj_t *chip;
      lv_obj_t *name;
      lv_obj_t *info;

      tile = pw_card_new(scr, MENU_TILE_W, MENU_TILE_H, PW_COL_CARD);
      lv_obj_set_pos(tile, x, y);
      lv_obj_set_style_bg_color(tile, PW_COL_CARD_LT, LV_STATE_PRESSED);
      lv_obj_add_event_cb(tile, ui_tile_cb, LV_EVENT_CLICKED,
                          (void *)b->open);

      /* 板块色块 */

      chip = lv_obj_create(tile);
      lv_obj_set_size(chip, 26, 26);
      lv_obj_set_pos(chip, 14, 14);
      lv_obj_set_style_bg_color(chip, lv_color_hex(b->color), 0);
      lv_obj_set_style_radius(chip, 8, 0);
      lv_obj_set_style_border_width(chip, 0, 0);
      lv_obj_remove_flag(chip, LV_OBJ_FLAG_SCROLLABLE);

      /* 名称 + 状态 */

      name = pw_label_new(tile, b->name, PW_FNT_MED,
                          b->live ? PW_COL_TEXT : PW_COL_FAINT);
      lv_obj_set_pos(name, 50, 12);

      info = pw_label_new(tile, b->info, PW_FNT_BODY,
                          b->live ? PW_COL_DIM : PW_COL_FAINT);
      lv_obj_set_pos(info, 50, 52);
    }

  pw_scr_open(scr);
}

/****************************************************************************
 * 自动演示（模拟点击验证 / 录屏）：自动打开板块与实验页，播完回根屏。
 ****************************************************************************/

enum
{
  DEMO_OPEN = 0,   /* 打开指定屏 */
  DEMO_BACK = 1    /* 返回上一屏 */
};

struct pw_demo_step_s
{
  int              kind;
  lv_obj_t *(*open)(void);     /* DEMO_OPEN 时的目标屏 */
};

static const struct pw_demo_step_s g_demo_seq[] =
{
  { DEMO_OPEN, pw_raw_screen },
  { DEMO_BACK, NULL },
  { DEMO_OPEN, ui_mech_open },
  { DEMO_OPEN, pw_pendulum_screen },
  { DEMO_BACK, NULL },
  { DEMO_BACK, NULL },
  { DEMO_OPEN, ui_tool_open },
  { DEMO_OPEN, pw_spec_accel_screen },
  { DEMO_BACK, NULL },
  { DEMO_BACK, NULL },
  { DEMO_OPEN, ui_time_open },
  { DEMO_OPEN, pw_motion_stopwatch_screen },
  { DEMO_BACK, NULL },
  { DEMO_BACK, NULL },
  { DEMO_OPEN, ui_every_open },
  { DEMO_OPEN, pw_applause_screen },
  { DEMO_BACK, NULL },
  { DEMO_BACK, NULL },
  { DEMO_BACK, NULL },   /* 回到根屏 */
};

static bool        g_demo_done;
static int         g_demo_idx;

int pw_ui_demo_step(void)
{
  FAR const struct pw_demo_step_s *st;

  if (g_demo_done)
    {
      return 1;
    }

  if (g_demo_idx >= (int)(sizeof(g_demo_seq) / sizeof(g_demo_seq[0])))
    {
      g_demo_done = true;
      return 1;
    }

  st = &g_demo_seq[g_demo_idx++];

  if (st->kind == DEMO_OPEN)
    {
      pw_scr_open(st->open());
    }
  else
    {
      pw_scr_back();
    }

  return g_demo_done;
}

void pw_ui_demo_start(void)
{
  g_demo_done = false;
  g_demo_idx  = 0;
}

bool pw_ui_demo_done(void)
{
  return g_demo_done;
}
