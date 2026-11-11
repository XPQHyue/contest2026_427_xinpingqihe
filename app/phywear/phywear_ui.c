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
#include <syslog.h>

#include <lvgl/lvgl.h>

#include "phywear_ui.h"
#include "pw_icons.h"
#include "pw_logos.h"
#include "pw_bounce.h"
#include "phywear_imu.h"
#include "phywear_i18n.h"
#include "pw_graph.h"
#include "pw_motion_lvgl.h"
#include "phywear_raw.h"
#include "phywear_pend.h"
#include "phywear_spec.h"
#include "phywear_spring.h"
#include "phywear_centri.h"
#include "phywear_incline.h"
#include "phywear_ruler.h"
#include "phywear_time.h"
#include "phywear_life.h"
#include "pw_ai.h"
#include "pw_bt.h"
#include "pw_btgatt.h"
#include "pw_power.h"
#include "pw_settings.h"

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
static int        g_bat_chg = -1;                  /* 上次画出来的充电态，内容没变就不重设 */
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
/* 板块图标：自绘小图形（LVGL 图元，不依赖任何字体/图标库）
 *
 * 为什么不用 LV_SYMBOL：内置字形里没有"摆/弹簧"也没有"对话气泡"，
 * 旋转箭头（REFRESH）表达力学与实际不符、信封表达 AI 教练也偏"邮件"。
 * 这里用最简单的图元拼出语义明确的图形。 */

static lv_obj_t *tile_dot(lv_obj_t *parent, int x, int y, int w, int h,
                          int radius, lv_color_t c, lv_opa_t opa)
{
  lv_obj_t *o = lv_obj_create(parent);

  lv_obj_set_size(o, w, h);
  lv_obj_set_pos(o, x, y);
  lv_obj_set_style_bg_color(o, c, 0);
  lv_obj_set_style_bg_opa(o, opa, 0);
  lv_obj_set_style_radius(o, radius, 0);
  lv_obj_set_style_border_width(o, 0, 0);
  lv_obj_set_style_pad_all(o, 0, 0);
  lv_obj_remove_flag(o, LV_OBJ_FLAG_SCROLLABLE);
  return o;
}

static void tile_draw_icon(lv_obj_t *chip, int kind, lv_color_t c)
{
  if (kind == 1)
    {
      /* 力学：摆 —— 竖直摆线 + 摆球（一眼看出是单摆/振动，而不是"旋转"） */

      tile_dot(chip, 14, 4, 2, 10, 0, c, LV_OPA_COVER);              /* 摆线 */
      tile_dot(chip, 11, 14, 9, 9, LV_RADIUS_CIRCLE, c, LV_OPA_COVER); /* 摆球 */
    }
  else if (kind == 2)
    {
      /* AI 教练：对话气泡 + 三个点（"会说话/会回答"，而不是"邮件"） */

      tile_dot(chip, 4, 5, 22, 15, 7, c, LV_OPA_30);                 /* 气泡 */
      tile_dot(chip, 10, 11, 4, 4, LV_RADIUS_CIRCLE, c, LV_OPA_COVER);
      tile_dot(chip, 15, 11, 4, 4, LV_RADIUS_CIRCLE, c, LV_OPA_COVER);
      tile_dot(chip, 20, 11, 4, 4, LV_RADIUS_CIRCLE, c, LV_OPA_COVER);
    }
}

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
  const lv_image_dsc_t *icon;       /* Lucide 线性图标（24x24，RGB565A8，运行时染色） */
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
 * 蓝牙链路状态指示（主界面标题右侧的灰/蓝点）
 *
 * ⚠️ 跨线程：状态由 zblue 回调在 **Zephyr 工作队列线程**上改，界面在
 * **phywear 主线程**上画。所以这里**只读** `pw_bt_link()`，并且只在
 * lv_timer（GUI 线程）里碰 LVGL —— 绝不在 BT 回调里直接改界面。
 * 详见 pw_btgatt.h 顶部。
 ****************************************************************************/

static lv_obj_t *g_bt_dot;          /* 主界面状态点；根屏重建后重新指向 */
static lv_obj_t *g_bt_lab;          /* 旁边那两个字 */
static uint8_t   g_bt_last;         /* 上次画出来的连接状态，避免每 250ms 白重绘 */
static uint8_t   g_bt_last_wait;    /* 上次画出来的"启动中"状态（同上） */

/* 第三种状态：host 栈正在后台起（~1.2 s）。用琥珀色，与"灰=未连接/蓝=已连接"
 * 区分开，免得用户以为点了没反应。 */
#define PW_COL_BT_WAIT 0xE0A458
static uint8_t   g_bt_dirty;        /* 1 = 还没画过（根屏刚建好） */
static uint8_t   g_bt_timer_started;

/* 根屏被删除（切语言重建 / 返回）时把指针清空：
 * 留着悬空指针虽然还有 lv_obj_is_valid() 兜底，但那是"事后补救"；
 * 这里直接让 tick 走"不在根屏"的早退分支，逻辑更干净。 */

static void ui_bt_scr_del_cb(lv_event_t *e)
{
  (void)e;
  g_bt_dot = NULL;
  g_bt_lab = NULL;
}

static void ui_bt_btn_cb(lv_event_t *e)
{
  (void)e;
  pw_scr_open(pw_bt_screen());
}

static void ui_bt_apply(uint8_t on, uint8_t waiting)
{
  lv_color_t col = on ? PW_COL_BT_ON :
                   (waiting ? lv_color_hex(PW_COL_BT_WAIT) : PW_COL_BT_OFF);

  if (g_bt_dot != NULL && lv_obj_is_valid(g_bt_dot))
    {
      lv_obj_set_style_bg_color(g_bt_dot, col, 0);
      /* 已连接时给一点外发光（阴影），远看也能一眼分辨；
       * 阴影是静态样式，不是动画，不影响帧率。 */
      lv_obj_set_style_shadow_color(g_bt_dot, col, 0);
      lv_obj_set_style_shadow_width(g_bt_dot, on ? 8 : 0, 0);
      lv_obj_set_style_shadow_opa(g_bt_dot, LV_OPA_60, 0);
    }

  if (g_bt_lab != NULL && lv_obj_is_valid(g_bt_lab))
    {
      lv_obj_set_style_text_color(g_bt_lab, col, 0);
    }
}

static void ui_bt_tick(lv_timer_t *t)
{
  FAR const struct pw_bt_link_s *lk = pw_bt_link();
  uint8_t on = lk->connected ? 1 : 0;
  uint8_t waiting = (!pw_bt_is_up() && pw_bt_is_starting()) ? 1 : 0;

  (void)t;

  if (g_bt_dot == NULL && g_bt_lab == NULL)
    {
      return;                     /* 当前不在根屏（已返回上一级），不用画 */
    }

  if (!g_bt_dirty && on == g_bt_last && waiting == g_bt_last_wait)
    {
      return;                     /* 状态没变，不重绘 */
    }

  g_bt_last      = on;
  g_bt_last_wait = waiting;
  g_bt_dirty     = 0;
  ui_bt_apply(on, waiting);
}

/* 在根屏上放一枚状态点 + 两个字。返回是否创建成功。 */

static void ui_bt_indicator_add(lv_obj_t *scr)
{
  lv_obj_t *box;
  lv_obj_t *dot;
  lv_obj_t *lab;

  /* 位置：标题行右半、两个入口按钮的左侧。
   * 几何：屏宽 390；「语」在 x=254（宽 56）、「…」在 x=310（宽 60）；
   * 标题 "PhyWear" 用 28px，从 x=36 起约到 145。所以 190~250 这段是空的。
   *
   * 做成**可点**而不是纯装饰：这一页（链路状态 + 收发原文）是现场验收时
   * 最想看的东西，一指可达才对得起它；也省掉在工具板块里再塞一行
   * （那个列表已经 5 行占满内容区，第 6 行要滚动才看得见）。 */

  box = lv_obj_create(scr);
  lv_obj_set_size(box, 62, 44);
  lv_obj_set_pos(box, 190, 10);
  lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(box, ui_bt_btn_cb, LV_EVENT_CLICKED, NULL);
  pw_press_style(box);

  dot = lv_obj_create(box);
  lv_obj_set_size(dot, 10, 10);
  lv_obj_set_pos(dot, 6, 16);
  lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
  lv_obj_set_style_border_width(dot, 0, 0);
  lv_obj_set_style_pad_all(dot, 0, 0);
  pw_deco(dot);                   /* 纯指示，点击要冒泡到 box */

  lab = pw_label_new(box, pw_i18n_is_zh() ? "蓝牙" : "BT",
                     PW_FNT_BODY, PW_COL_BT_OFF);
  lv_obj_set_pos(lab, 20, 9);
  pw_deco(lab);

  g_bt_dot = dot;
  g_bt_lab = lab;
  g_bt_dirty = 1;
  ui_bt_tick(NULL);               /* 立即画一次，别等 250ms */
}

/****************************************************************************
 * Name: ui_bat_*
 *
 * Description:
 *   电池指示（T-0002）：顶栏与根屏共用一个画法 —— 文本 "87%" / "87%+"，
 *   充电时变绿并带 "+"。数据走 pw_power_state()（内部 5 s 缓存），
 *   所以这里 1 Hz 刷新不会反复开 /dev/adc0。
 ****************************************************************************/

static void ui_sleep_cb(lv_event_t *e);
static void ui_wake_cb(lv_event_t *e);
static void ui_touch_cb(lv_event_t *e);

static void ui_bat_apply(lv_obj_t *lab)
{
  char buf[16];
  int pct;
  int chg;

  if (lab == NULL)
    {
      return;
    }

  if (pw_power_state(&pct, &chg) < 0)
    {
      lv_label_set_text(lab, "--");
      return;
    }

  snprintf(buf, sizeof(buf), chg ? "%d%%+" : "%d%%", pct);

  /* [AGENT-VM] 内容没变就别碰控件：lv_label_set_text() 会无条件
   * lv_obj_invalidate()，而 vendor 的 render_start() 会把"一个像素脏"
   * 放大成整屏重绘（3 个 chunk ≈ 45 ms）。每秒都重设一次相同的文字，
   * 就等于每秒卡一下——静止页面也会。
   */

  if (g_bat_chg == chg && strcmp(lv_label_get_text(lab), buf) == 0)
    {
      return;
    }

  g_bat_chg = chg;
  lv_label_set_text(lab, buf);
  lv_obj_set_style_text_color(lab, chg ? lv_color_hex(0x7ED321) : PW_COL_DIM, 0);
}

static void ui_bat_tick(lv_timer_t *t)
{
  ui_bat_apply(lv_timer_get_user_data(t));
}

static void ui_bat_add(lv_obj_t *parent, lv_align_t align, int x, int y)
{
  lv_obj_t *lab;
  lv_timer_t *t;

  /* 电量 = 16px 电池图标 + 百分比文字（MINIMAL-B §3）。
   * 放进一个透明的"盒"里一起定位，这样图标与数字永远贴在一起，
   * 不需要为两种对齐各写一套坐标。 */

  lv_obj_t *box = lv_obj_create(parent);
  lv_obj_set_size(box, 74, 20);
  lv_obj_align(box, align, x, y);
  lv_obj_set_style_bg_opa(box, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(box, 0, 0);
  lv_obj_set_style_pad_all(box, 0, 0);
  lv_obj_remove_flag(box, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_move_foreground(box);    /* 后建的卡片不许把它盖住 */
  pw_deco(box);                   /* 纯指示：点击要冒泡给下面的按钮/屏 */

  {
    lv_obj_t *bi = lv_image_create(box);
    lv_image_set_src(bi, &pw_icon_bat_24);
    lv_image_set_scale(bi, 171);   /* 16/24 ≈ 0.67 → 171/256，绕自身中心缩放 */
    lv_obj_set_style_image_recolor(bi, PW_COL_DIM, 0);
    lv_obj_set_style_image_recolor_opa(bi, LV_OPA_COVER, 0);
    lv_obj_align(bi, LV_ALIGN_RIGHT_MID, -42, 2);
  }

  lab = pw_label_new(box, "--", PW_FNT_BODY, PW_COL_DIM);
  lv_obj_align(lab, LV_ALIGN_RIGHT_MID, 0, 0);

  t = lv_timer_create(ui_bat_tick, 1000, lab);
  lv_obj_add_event_cb(lab, ui_del_cb, LV_EVENT_DELETE, t);
  ui_bat_apply(lab);
}

/****************************************************************************
 * Name: ui_sleep_cb / ui_wake_cb
 *
 * Description:
 *   长按任意屏 → 背光 0（息屏），同时铺一层全屏黑遮罩拦住误触；
 *   点一下遮罩 → 背光恢复（亮屏）并撤掉遮罩。
 *
 *   为什么用遮罩而不是只关背光：背光关掉时触摸屏还在上报，用户"点亮"的那
 *   一下会顺手点到下面的按钮（比如点进某个实验页）。遮罩把第一下吃掉，
 *   行为才和手机一致。
 *
 *   第一版只挂 CLICKED，实机反馈「亮了以后还是进了下面那一页」——原因是
 *   抬手那一刻遮罩已经被隐藏，LVGL 把同一次抬手又派发给了指针下面的控件。
 *   现在改成挂 PRESSED（按下即亮，手感更快），并在回调里先
 *   lv_indev_wait_release() 把本次手势从按下到抬起整段吃掉，再撤遮罩；
 *   这样"点一下只负责亮屏"是输入设备层面的保证，不依赖控件还在不在。
 ****************************************************************************/

static lv_obj_t *g_sleep_mask;

void pw_ui_wake_now(void)
{
  /* 只隐藏不删除：本函数常在遮罩自己的 CLICKED 回调里跑（点屏唤醒），
   * 当场 delete 自己这块对象在 LVGL 里属于"事件回调内删对象"，容易踩到
   * 事件还在栈上、对象已经没了的坑。遮罩只有一块，留着复用最省事。 */

  if (g_sleep_mask != NULL)
    {
      lv_obj_add_flag(g_sleep_mask, LV_OBJ_FLAG_HIDDEN);
    }

  pw_power_screen_on();
}

int pw_ui_is_asleep(void)
{
  return g_sleep_mask != NULL &&
         !lv_obj_has_flag(g_sleep_mask, LV_OBJ_FLAG_HIDDEN);
}

void pw_ui_sleep_now(void)
{
  if (g_sleep_mask != NULL)
    {
      lv_obj_remove_flag(g_sleep_mask, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(g_sleep_mask);
      pw_power_screen_off();
      return;
    }

  pw_power_screen_off();

  g_sleep_mask = lv_obj_create(lv_layer_top());
  lv_obj_set_size(g_sleep_mask, PW_SCREEN_W, PW_SCREEN_H);
  lv_obj_set_pos(g_sleep_mask, 0, 0);
  lv_obj_set_style_bg_color(g_sleep_mask, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_sleep_mask, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(g_sleep_mask, 0, 0);
  lv_obj_set_style_radius(g_sleep_mask, 0, 0);
  lv_obj_set_style_pad_all(g_sleep_mask, 0, 0);
  lv_obj_remove_flag(g_sleep_mask, LV_OBJ_FLAG_SCROLLABLE);
  /* 按下即亮，并且把这一次手势整段吞掉（见文件头注释）。 */

  lv_obj_add_event_cb(g_sleep_mask, ui_wake_cb, LV_EVENT_PRESSED, NULL);
}

static void ui_wake_cb(lv_event_t *e)
{
  lv_indev_t *indev = lv_indev_active();

  /* 先把"从这次按下到抬手"整段标成待释放：抬手事件会被输入设备丢弃，
   * 不会变成下面那层控件的 CLICKED。必须在撤遮罩之前调用。 */

  if (indev != NULL)
    {
      lv_indev_wait_release(indev);
    }

  pw_ui_wake_now();
}

/* 触摸打点（只记时间，不做任何界面动作）：给屏上帧率表的 TOUCH 用。
 * 挂在每一屏上，子控件的按下事件会冒泡上来。 */

static void ui_touch_cb(lv_event_t *e)
{
  pw_perf_note_touch();
}

static void ui_sleep_cb(lv_event_t *e)
{
  lv_indev_t *indev = lv_indev_active();

  /* 长按触发的抬手同样吞掉：否则息屏那一瞬间手指抬起，CLICKED 会落到
   * 卡片上，等用户点屏唤醒时看到的已经是被点进去的另一页。 */

  if (indev != NULL)
    {
      lv_indev_wait_release(indev);
    }

  pw_ui_sleep_now();
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

/* P1 动效：tile 按压反馈（按下 y+2px，抬起回位；C4 snap 150ms）。
 * 只动 y 一个属性；user_data 传 tile 的基准 y（与 CLICKED 的 user_data 各用一套）。 */


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
  /* 只接收“函数指针”本身，不接收指向实验表的指针。
   *
   * 背景（真机 bug，2026-09-13 定位）：pw_board_list() 的调用方把实验表
   * 建在**栈**上（PW_STR() 需要运行期求值，无法做静态初始化）。若把
   * &items[i] 存进事件回调的 user_data，用户点击时该栈帧早已销毁 ——
   * 悬空指针使 it->open 变成垃圾值，随即跳到非法地址，表现为
   * 「从组页点入实验页时整机卡死、随后被看门狗复位」。
   * 直接传函数指针即可，语义相同且无生命周期问题。 */

  lv_obj_t *(*open)(void) = lv_event_get_user_data(e);

  if (open != NULL)
    {
      pw_scr_open(open());
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

  /* 屏本身要能收 press：长按息屏的事件从子控件冒泡上来，
   * 而不可点的对象根本不产生 press 事件链。 */

  lv_obj_add_flag(scr, LV_OBJ_FLAG_CLICKABLE);

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

  /* 长按息屏（T-0002）：挂在每一屏上，子控件的事件默认冒泡到屏，
   * 所以在任意页面长按都能睡。 */

  lv_obj_add_event_cb(scr, ui_sleep_cb, LV_EVENT_LONG_PRESSED, NULL);
  lv_obj_add_event_cb(scr, ui_touch_cb, LV_EVENT_PRESSED, NULL);

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
  pw_press_style(btn);

  lab = pw_label_new(btn, "<", PW_FNT_XL, PW_COL_DIM);
  lv_obj_center(lab);

  /* 标题 */

  lab = pw_label_new(bar, title, PW_FNT_LARGE, PW_COL_TEXT);
  lv_obj_align(lab, LV_ALIGN_CENTER, 0, 0);

  /* 顶栏右侧运行计时（phyphox 工具栏语义；进入测量屏后 1Hz 刷新 mm:ss） */

  lab = pw_label_new(bar, "", PW_FNT_SMALL, PW_COL_DIM);
  /* 右侧状态区：计时在左、电量在最右（手机状态栏的惯用顺序） */

  lv_obj_align(lab, LV_ALIGN_RIGHT_MID, -102, 0);  /* 给右侧电量图标让位 */
  g_pending_timer = lab;

  /* 顶栏右侧电源指示（电量 + 充电标记），所有子页共用 */

  ui_bat_add(bar, LV_ALIGN_RIGHT_MID, -16, 0);

  /* 内容区（下方全部区域，调用方可改尺寸） */

  cont = lv_obj_create(scr);
  lv_obj_set_size(cont, PW_SCREEN_W, PW_SCREEN_H - PW_TOPBAR_H);
  lv_obj_set_pos(cont, 0, PW_TOPBAR_H);
  lv_obj_set_style_bg_opa(cont, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(cont, 0, 0);
  lv_obj_set_style_pad_all(cont, 0, 0);
  lv_obj_set_scrollbar_mode(cont, LV_SCROLLBAR_MODE_OFF);

  /* 切屏转场：内容区从 +16px 落位（C2 260ms，单对象）。
   * 所有走 pw_topbar() 的页面自动获得同一条入场动效。 */

  pw_motion_slide_in_y_at(cont, PW_TOPBAR_H, 16, 260, 0);

  return cont;
}

/* 卡片 hairline 描边开关（0=不描边）。暗底上给卡片一条清晰的边，替代阴影
 * （EPIC 禁阴影/模糊）；1px、半径外直边，观感是"仪器面板"的锐利感。想 A/B 改这里。 */
#ifndef PW_CARD_LINE
#  define PW_CARD_LINE   1
#endif

/* P0：卡片圆角。**EPIC 不支持圆角** —— lv_draw_sifli_epic.c 的 FILL/BORDER 分支里
 * `if(radius != 0) return 0;`，即任何带圆角的填充/边框都会回退到 CPU 软件光栅（掩码+混合）。
 * 默认 0（走 EPIC 直通）；要看"圆角到底吃多少帧率"就把这里改回 14 做同口径 A/B。 */

#ifndef PW_CARD_RADIUS
#  define PW_CARD_RADIUS 14   /* P0 A/B 实测：改 0 在实时页/轨迹页都测不到收益（34/36、10/11 完全相同），
                               * 故保留观感。宏留着当实验旋钮，见 docs/evidence/p0-20260916/ */
#endif

/* 统一按压态（2026-09-17）：把"按下有反应"做成**样式**而不是逐个注册事件。
 * 装在 pw_card_new() 上 → 全 App 所有卡片/按钮/行/宫格自动获得反馈；
 * 子件若要自定义（如已有点击动画）可再叠自己的 LV_STATE_PRESSED 样式。
 * 只改 translate_y（位移）+ 底色，**不碰 transform/圆角/阴影**，因此不进 SW 变换路径。 */

static lv_style_t                g_st_press;
static lv_style_transition_dsc_t g_tr_press;
static bool                      g_press_ready;

void pw_press_style(lv_obj_t *obj)
{
#if PW_UI_MOTION
  static const lv_style_prop_t props[] =
  {
    LV_STYLE_TRANSLATE_Y, LV_STYLE_BG_COLOR, LV_STYLE_BG_OPA, 0
  };

  if (obj == NULL)
    {
      return;
    }

  if (!g_press_ready)
    {
      lv_style_init(&g_st_press);
      lv_style_set_translate_y(&g_st_press, 3);
      lv_style_set_bg_color(&g_st_press, PW_COL_CARD_LT);
      lv_style_set_bg_opa(&g_st_press, LV_OPA_COVER);
      lv_style_transition_dsc_init(&g_tr_press, props,
                                   pw_motion_path_c4, 120, 0, NULL);
      lv_style_set_transition(&g_st_press, &g_tr_press);
      g_press_ready = true;
    }

  lv_obj_add_style(obj, &g_st_press, LV_STATE_PRESSED);
#else
  LV_UNUSED(obj);
#endif
}

/* 装饰性 obj 不参与触摸。lv_obj_create() 默认带 CLICKABLE，
 * 图标块/色条/网格线会把按压状态从卡片上"抢走"——表现为"按在图标上没反应"。 */
void pw_section_bar(lv_obj_t *parent, lv_color_t accent, int x, int y)
{
  lv_obj_t *bar = lv_obj_create(parent);

  lv_obj_set_size(bar, 3, 16);
  lv_obj_set_pos(bar, x, y);
  lv_obj_set_style_bg_color(bar, accent, 0);
  lv_obj_set_style_radius(bar, 2, 0);
  lv_obj_set_style_border_width(bar, 0, 0);
  lv_obj_remove_flag(bar, LV_OBJ_FLAG_SCROLLABLE);
  pw_deco(bar);
}

void pw_deco(lv_obj_t *obj)
{
  if (obj != NULL)
    {
      lv_obj_remove_flag(obj, LV_OBJ_FLAG_CLICKABLE);
    }
}

lv_obj_t *pw_card_new(lv_obj_t *parent, int w, int h, lv_color_t bg)
{
  lv_obj_t *card = lv_obj_create(parent);

  lv_obj_set_size(card, w, h);
  lv_obj_set_style_bg_color(card, bg, 0);

#if PW_CARD_LINE
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, PW_COL_LINE, 0);
  lv_obj_set_style_border_opa(card, LV_OPA_50, 0);
#else
  lv_obj_set_style_border_width(card, 0, 0);
#endif
  lv_obj_set_style_radius(card, PW_CARD_RADIUS, 0);
  lv_obj_set_style_pad_all(card, 0, 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);
  pw_press_style(card);          /* 统一按压反馈：位移 + 底色，120ms C4 */

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

      row = pw_card_new(cont, PW_SCREEN_W - 2 * MENU_X0, 64,
                        PW_COL_CARD);
      lv_obj_set_pos(row, MENU_X0, y);
#if PW_UI_MOTION
      /* 错峰入场：**必须传显式 base y**（见 pw_motion_slide_in_y_at 注释） */
      pw_motion_slide_in_y_at(row, y, 12, 240, (uint32_t)i * 40);
#endif
      lv_obj_set_style_bg_color(row, PW_COL_CARD_LT, LV_STATE_PRESSED);

      if (ok)
        {
          /* 左侧强调条 */

          strip = lv_obj_create(row);
          lv_obj_set_size(strip, 3, 36);
          lv_obj_set_pos(strip, 10, 14);
          pw_deco(strip);
          lv_obj_set_style_bg_color(strip, accent, 0);
          lv_obj_set_style_radius(strip, 2, 0);
          lv_obj_set_style_border_width(strip, 0, 0);
          lv_obj_remove_flag(strip, LV_OBJ_FLAG_SCROLLABLE);

          lab = pw_label_new(row, it->name, PW_FNT_MED, namecol);
          lv_obj_set_pos(lab, 26, 9);

          lab = pw_label_new(row, it->desc, PW_FNT_BODY, desccol);
          lv_obj_set_pos(lab, 26, 34);

          lab = pw_label_new(row, ">", PW_FNT_MED, PW_COL_DIM);
          lv_obj_align(lab, LV_ALIGN_RIGHT_MID, -18, 0);

          lv_obj_add_event_cb(row, ui_item_cb, LV_EVENT_CLICKED,
                              (void *)it->open);
        }
      else
        {
          /* 未实现项：置灰 + 角标 */

          lab = pw_label_new(row, it->name, PW_FNT_MED, namecol);
          lv_obj_set_pos(lab, 22, 9);

          lab = pw_label_new(row, it->desc, PW_FNT_BODY, desccol);
          lv_obj_set_pos(lab, 22, 34);

          lab = pw_label_new(row, PW_STR(UI_PLANNED), PW_FNT_BODY, PW_COL_FAINT);
          lv_obj_align(lab, LV_ALIGN_RIGHT_MID, -14, 0);
        }

      y += 64 + MENU_GAP;
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
    { PW_STR(EXP_BOUNCE_NAME),   PW_STR(EXP_BOUNCE_DESC),   pw_bounce_screen },
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
    { PW_STR(EXP_TONE_NAME),    PW_STR(EXP_TONE_DESC),     pw_tone_screen },
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
    { PW_STR(IMU_TITLE),           PW_STR(EXP_IMU_DESC),         pw_imu_screen },
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
  /* 自定义板块：本轮只做诚实标注（T-0016 §30.4），不加实验搭建器功能。
   * 入口做成可点，进去看到的是"为什么现在只有一条 + 去用哪个板块"，
   * 比一个点了没反应的置灰卡片更清楚。 */

  lv_obj_t *scr = pw_scr_new();
  lv_obj_t *cont = pw_topbar(scr, PW_STR(UI_CUSTOM));
  lv_obj_t *row;
  lv_obj_t *lab;

  row = pw_card_new(cont, PW_SCREEN_W - 2 * MENU_X0, 64, PW_COL_CARD);
  lv_obj_set_pos(row, MENU_X0, MENU_GAP);
  lv_obj_set_style_bg_color(row, PW_COL_CARD, LV_STATE_PRESSED);

  lab = pw_label_new(row, PW_STR(EXP_BUILDER_NAME), PW_FNT_MED, PW_COL_FAINT);
  lv_obj_set_pos(lab, 22, 9);

  lab = pw_label_new(row, PW_STR(EXP_BUILDER_DESC), PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 22, 34);

  lab = pw_label_new(row, PW_STR(UI_PLANNED), PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_align(lab, LV_ALIGN_RIGHT_MID, -14, 0);

  /* 一句话说明 + 替代路径（用字都在字库子集内） */

  lab = pw_label_new(cont, "当前请用工具板块的 IMU / 倾角 / 尺子",
                     PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, MENU_X0 + 4, MENU_GAP + 64 + 16);
  lv_obj_set_width(lab, PW_SCREEN_W - 2 * MENU_X0 - 8);
  lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);

  return scr;
}

int pw_ui_open_board(int idx)
{
  lv_obj_t *(*open)(void) = NULL;

  switch (idx)
    {
      case 0: open = ui_mech_open;   break;
      case 1: open = ui_acou_open;   break;
      case 2: open = ui_tool_open;   break;
      case 3: open = ui_time_open;   break;
      case 4: open = ui_every_open;  break;
      case 5: open = ui_custom_open; break;
      default: return 0;
    }

  pw_scr_open(open());
  return 1;
}

/****************************************************************************
 * AI 教练页
 *
 * 让手表 UI 上"看得见 AI"：按下按钮 = 向端侧 Agent 发一条自然语言请求
 * （命中 agent_loop 的离线意图表；有 LLM 时走 LLM）→ Agent 调工具（切页 /
 * 读数 / 跑实验）→ 回复文本回落到 pw_ai 的消息日志 → 本页周期刷新显示。
 *
 * 线程安全：所有 LVGL 操作都在 GUI 线程内；Agent 回复只写 pw_ai 的环形日志。
 ****************************************************************************/

#define AI_CARD_H    176
#define AI_BTN_W     ((PW_SCREEN_W - 2 * MENU_X0 - 8) / 2)
#define AI_BTN_H     54
#define AI_BTN_GAP   8

static lv_obj_t *g_ai_status;
static lv_obj_t *g_ai_latest;
static lv_obj_t *g_ai_more[2];

/* 4 个快捷请求：文案同时作为发给 Agent 的请求文本（中英文都能命中离线意图） */

static const int g_ai_btn_ids[] =
{
  PW_STR_AICOACH_BTN_LIST,
  PW_STR_AICOACH_BTN_PENDULUM,
  PW_STR_AICOACH_BTN_ACCEL,
  PW_STR_AICOACH_BTN_RUN,
};

static void ai_tick(lv_timer_t *t);
static void ai_btn_cb(lv_event_t *e);

static void ai_refresh(void)
{
  const char *line;
  int i;

  if (g_ai_status == NULL)
    {
      return;
    }

  lv_label_set_text(g_ai_status, pw_ai_agent_ready()
                    ? PW_STR(AICOACH_ONLINE) : PW_STR(AICOACH_OFFLINE));
  lv_obj_set_style_text_color(g_ai_status,
                              pw_ai_agent_ready() ? PW_ACC_AI : PW_COL_DIM, 0);

  line = pw_ai_log_line(0);
  lv_label_set_text(g_ai_latest, line != NULL ? line : PW_STR(AICOACH_EMPTY));

  for (i = 0; i < 2; i++)
    {
      line = pw_ai_log_line(i + 1);
      lv_label_set_text(g_ai_more[i], line != NULL ? line : "");
    }
}

static void ai_tick(lv_timer_t *t)
{
  ai_refresh();
}

static void ai_btn_cb(lv_event_t *e)
{
  int id = (int)(intptr_t)lv_event_get_user_data(e);
  const char *req = pw_str(id);
  char line[160];

  if (req == NULL)
    {
      return;
    }

  snprintf(line, sizeof(line), "> %s", req);
  pw_ai_note(line);                 /* 立刻给用户反馈，不必等 Agent */
  pw_ai_ask(req);
  ai_refresh();
}

static lv_obj_t *ai_button(lv_obj_t *parent, int x, int y, int str_id)
{
  lv_obj_t *btn = pw_card_new(parent, AI_BTN_W, AI_BTN_H, PW_COL_CARD);
  lv_obj_t *lab;

  lv_obj_set_pos(btn, x, y);
  lv_obj_add_event_cb(btn, ai_btn_cb, LV_EVENT_CLICKED,
                      (void *)(intptr_t)str_id);
  lab = pw_label_new(btn, pw_str(str_id), PW_FNT_SMALL, PW_ACC_AI);
  lv_obj_center(lab);
  return btn;
}

static lv_obj_t *ui_ai_open(void)
{
  lv_obj_t *scr;
  lv_obj_t *cont;
  lv_obj_t *card;
  lv_obj_t *lab;
  int i;

  scr = pw_scr_new();
  cont = pw_topbar(scr, PW_STR(UI_AI_COACH));

  /* 状态 + 最近回复 */

  card = pw_card_new(cont, PW_SCREEN_W - 2 * MENU_X0, AI_CARD_H, PW_COL_CARD);
  lv_obj_set_pos(card, MENU_X0, MENU_GAP);

  lab = pw_label_new(card, PW_STR(AICOACH_STATUS), PW_FNT_SMALL, PW_COL_DIM);
  lv_obj_set_pos(lab, 14, 10);

  g_ai_status = pw_label_new(card, PW_STR(AICOACH_OFFLINE), PW_FNT_SMALL,
                             PW_COL_DIM);
  lv_obj_set_pos(g_ai_status, 120, 10);

  lab = pw_label_new(card, "设备无网络，需手机 PPP 链路启用", PW_FNT_BODY,
                     PW_COL_FAINT);
  lv_obj_set_pos(lab, 14, 62);

  lab = pw_label_new(card, PW_STR(AICOACH_LATEST), PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 14, 88);

  g_ai_latest = pw_label_new(card, PW_STR(AICOACH_EMPTY), PW_FNT_SMALL,
                             PW_ACC_ACTIVE);
  lv_obj_set_pos(g_ai_latest, 14, 62);
  lv_obj_set_width(g_ai_latest, PW_SCREEN_W - 2 * MENU_X0 - 28);
  lv_label_set_long_mode(g_ai_latest, LV_LABEL_LONG_WRAP);

  for (i = 0; i < 2; i++)
    {
      g_ai_more[i] = pw_label_new(card, "", PW_FNT_BODY, PW_COL_DIM);
      lv_obj_set_pos(g_ai_more[i], 14, 108 + i * 24);
      lv_obj_set_width(g_ai_more[i], PW_SCREEN_W - 2 * MENU_X0 - 28);
      lv_label_set_long_mode(g_ai_more[i], LV_LABEL_LONG_DOT);
    }

  /* 4 个快捷请求 */

  for (i = 0; i < 4; i++)
    {
      int col = i % 2;
      int row = i / 2;
      ai_button(cont, MENU_X0 + col * (AI_BTN_W + AI_BTN_GAP),
                MENU_GAP + AI_CARD_H + MENU_GAP + row * (AI_BTN_H + AI_BTN_GAP),
                g_ai_btn_ids[i]);
    }

  /* 如实说明 */

  lab = pw_label_new(cont, PW_STR(AICOACH_HINT), PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, MENU_X0, MENU_GAP + AI_CARD_H + MENU_GAP + 2 * AI_BTN_H +
                 AI_BTN_GAP + MENU_GAP);
  lv_obj_set_width(lab, PW_SCREEN_W - 2 * MENU_X0);
  lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);

  ai_refresh();
  pw_scr_set_tick(scr, ai_tick, 700);     /* 周期刷新 Agent 回复 */

  return scr;
}

lv_obj_t *pw_ai_coach_screen(void)
{
  return ui_ai_open();
}

/****************************************************************************
 * 设置 / 语言 / 关于（phyphox 极简设置思想：语言 + 关于/许可）
 ****************************************************************************/

static void ui_lang_cb(lv_event_t *e)
{
  bool zh = (lv_event_get_user_data(e) != NULL);

  pw_i18n_set_zh(zh);
  pw_settings_save();
  /* 延迟到下一轮回调重建根屏，避免在对象删除回调里改树 *  */
  lv_async_call(pw_ui_rebuild_root, NULL);
}

static void ui_about_open_cb(lv_event_t *e)
{
  pw_scr_open(pw_about_screen());
}

static void ui_voice_cb(lv_event_t *e)
{
  pw_scr_open(pw_voice_screen());
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

/* 蓝牙页：手表版"蓝牙串口" + 连通性测试。
 *
 * 为什么这一页值得单独做：
 *   在此之前"蓝牙通了没有"只能靠串口日志或手机屏幕判断 —— 手表自己是个黑盒。
 *   这一页把链路状态（连没连、对端是谁、MTU 多少、收发几条）和最近 10 条
 *   收发原文直接摆到表盘上，于是**不接电脑也能现场验收**。
 *
 * 数据来源全部是 pw_bt_link() 的只读快照；页面本身不碰 BT 栈，
 * 也不在 BT 回调里被碰（见 pw_btgatt.h 的跨线程约定）。 */

static lv_obj_t *g_btp_scr;
static lv_obj_t *g_btp_state;      /* 大字：未连接 / 已连接 */
static lv_obj_t *g_btp_peer;       /* 对端地址 */
static lv_obj_t *g_btp_stat;       /* RX/TX/MTU 一行 */
static lv_obj_t *g_btp_rows[PW_BT_LOG_LINES];
static uint32_t  g_btp_shown;      /* 已显示到第几条（UI 私有，不回写共享状态） */

static void btp_del_cb(lv_event_t *e)
{
  (void)e;
  g_btp_scr = NULL;
  g_btp_state = NULL;
  g_btp_peer = NULL;
  g_btp_stat = NULL;
  memset(g_btp_rows, 0, sizeof(g_btp_rows));
}

static void btp_send_cb(lv_event_t *e)
{
  char msg[PW_BT_TEXT_MAX + 1];
  int rc;

  (void)e;

  snprintf(msg, sizeof(msg), "hello from PhyWear #%u",
           (unsigned)(g_btp_shown + 1));
  rc = pw_bt_send_text(msg);

  /* 失败要把原因写在脸上，别让用户以为"点了没反应"：
   * -ENOTCONN = 还没连上；-EBUSY = 上一条还在发；-EINVAL = 文本不合法 */
  if (rc != 0)
    {
      lv_label_set_text_fmt(g_btp_peer, "send rc=%d (%s)", rc,
                            rc == -ENOTCONN ? "not connected" :
                            rc == -EBUSY    ? "busy" : "bad text");
    }
}

/* 清空：只重置**显示游标**，不动共享日志。
 * 为什么不去 memset 环形缓冲：那要和 BT 线程抢着写同一块内存，
 * 为了一个"清屏"按钮去在 HCI 路径上加锁不值得。 */

static void btp_clear_cb(lv_event_t *e)
{
  FAR const struct pw_bt_link_s *lk = pw_bt_link();

  (void)e;
  g_btp_shown = lk->seq;
}

/* 只在文本真的变了才 set。
 *
 * 为什么较真这一下：`lv_label_set_text()` 无论内容是否相同都会让标签失效并
 * 触发重绘。这一页是 300 ms 一跳的，不比较的话空闲时也在以 ~3 Hz 全屏重绘：
 *   * 白耗 CPU（本项目的帧率口径很敏感）；
 *   * 更直接的后果是**真机截图抓不稳** —— pwshot 要连续流式读上千行像素，
 *     期间任何一次重绘都会撕掉一行，校验就判"1/1829 line(s) missing"而拒收
 *     （我第一次截这一页就是这么失败的）。
 * 比较一次的代价远小于一次重绘。 */

static void btp_set_if_changed(lv_obj_t *lab, const char *text)
{
  if (strcmp(lv_label_get_text(lab), text) != 0)
    {
      lv_label_set_text(lab, text);
    }
}

static void btp_tick(lv_timer_t *t)
{
  FAR const struct pw_bt_link_s *lk = pw_bt_link();
  uint32_t n;
  char buf[80];
  int i;

  (void)t;

  if (g_btp_scr == NULL || !lv_obj_is_valid(g_btp_scr))
    {
      return;
    }

  /* 状态区 */
  btp_set_if_changed(g_btp_state,
                     (!pw_bt_is_up() && pw_bt_is_starting())
                         ? "启动中"
                     : lk->connected ? (lk->subscribed ? "已连接 · 已订阅"
                                                       : "已连接")
                                     : "未连接");
  lv_obj_set_style_text_color(
      g_btp_state,
      lk->connected ? PW_COL_BT_ON :
      ((!pw_bt_is_up() && pw_bt_is_starting()) ? lv_color_hex(PW_COL_BT_WAIT)
                                               : PW_COL_BT_OFF), 0);

  if (lk->connected)
    {
      snprintf(buf, sizeof(buf), "对端 %s",
               lk->peer[0] ? lk->peer : "(unknown)");
    }
  else
    {
      /* 未连接时把"为什么"也写清楚：这页很大一部分价值就是省掉"拿电脑看日志" */
      snprintf(buf, sizeof(buf), "广播中，等待手机/电脑连接");
    }

  btp_set_if_changed(g_btp_peer, buf);

  /* 连接间隔一起显示：这是链路层优化的**现场证据** —— 请求 15~30 ms 之后
   * 到底协商成多少，手表上直接能看见，不用回去翻串口日志。 */
  if (lk->interval)
    {
      snprintf(buf, sizeof(buf), "RX %u · TX %u · echo %u · MTU %u · %.1f ms",
               (unsigned)lk->rx, (unsigned)lk->tx, (unsigned)lk->echo,
               (unsigned)lk->mtu, (double)lk->interval * 1.25);
    }
  else
    {
      snprintf(buf, sizeof(buf), "RX %u · TX %u · echo %u · MTU %u",
               (unsigned)lk->rx, (unsigned)lk->tx, (unsigned)lk->echo,
               (unsigned)lk->mtu);
    }

  btp_set_if_changed(g_btp_stat, buf);

  /* 日志区：只在有新行时整体重画（seq 是单调递增的写入计数） */
  n = lk->seq;

  if (n != g_btp_shown)
    {
      uint32_t first = (n > PW_BT_LOG_LINES) ? n - PW_BT_LOG_LINES : 0;

      g_btp_shown = n;

      for (i = 0; i < PW_BT_LOG_LINES; i++)
        {
          uint32_t idx = first + (uint32_t)i;

          if (idx >= n)
            {
              btp_set_if_changed(g_btp_rows[i], "");
              continue;
            }

          {
            FAR const struct pw_bt_logline_s *ln =
              &lk->log[idx % PW_BT_LOG_LINES];
            const char *tag = ln->dir == PW_BT_DIR_RX ? "[RX]" :
                              ln->dir == PW_BT_DIR_TX ? "[TX]" : "[--]";

            snprintf(buf, sizeof(buf), "%s %s", tag, ln->text);
            btp_set_if_changed(g_btp_rows[i], buf);
            lv_obj_set_style_text_color(g_btp_rows[i],
                          ln->dir == PW_BT_DIR_RX ? PW_COL_TEXT :
                          ln->dir == PW_BT_DIR_TX ? PW_COL_BT_ON :
                                                    PW_COL_DIM, 0);
          }
        }
    }
}

lv_obj_t *pw_bt_screen(void)
{
  lv_obj_t *scr = pw_scr_new();
  lv_obj_t *cont = pw_topbar(scr, pw_i18n_is_zh() ? "蓝牙" : "Bluetooth");
  lv_obj_t *card;
  lv_obj_t *lab;
  lv_obj_t *btn;
  int i;

  g_btp_shown = 0;
  memset(g_btp_rows, 0, sizeof(g_btp_rows));
  g_btp_scr = scr;
  lv_obj_add_event_cb(scr, btp_del_cb, LV_EVENT_DELETE, NULL);

  /* 打开这一页时确保 host 栈已起。
   * 为什么放在这里而不是只靠 `phywear cap bt`：这一页是用户能从手表上
   * **唯一**看到蓝牙状态的地方；若打开它却永远是"未连接"（因为栈压根没起、
   * 也没在广播），这页就等于没用 —— 而手表上没有任何别的入口能把蓝牙打开。
   * pw_bt_init() 是幂等的（见 pw_bt.h），已经起过就立刻返回。 */
  /* ⚠️ 必须用**异步**入口：`bt_enable()` 实测 ~1.2 s，而这一页是在 LVGL
   * 主线程上建的 —— 同步调就是"点一下冻一秒"（用户 2026-09-18 反馈：
   * "点击到蓝牙会卡顿 1S 左右"）。起栈期间状态卡显示"启动中"，
   * 300 ms 的 tick 起来后自动换成真实状态。 */
  if (!pw_bt_is_up())
    {
      pw_bt_init_async();
    }

  /* ── 链路状态卡（390-16 宽，92 高）── */

  card = pw_card_new(cont, PW_SCREEN_W - 2 * MENU_X0, 92, PW_COL_CARD);
  lv_obj_set_pos(card, MENU_X0, MENU_GAP);

  g_btp_state = pw_label_new(card, "-", PW_FNT_MED, PW_COL_BT_OFF);
  lv_obj_set_pos(g_btp_state, 16, 10);

  g_btp_peer = pw_label_new(card, "-", PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(g_btp_peer, 16, 40);

  g_btp_stat = pw_label_new(card, "-", PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(g_btp_stat, 16, 64);

  /* ── 收发日志卡（212 高，10 行）──
   * 前缀 [RX]/[TX] 是本页的判据：RX 有行 = 手机真的写进来了；
   * TX 有行 = 手表真的 notify 出去了。两边都有 = 双向连通。 */

  card = pw_card_new(cont, PW_SCREEN_W - 2 * MENU_X0, 212, PW_COL_CARD);
  lv_obj_set_pos(card, MENU_X0, 108);

  pw_section_bar(card, PW_COL_BT_ON, 12, 10);
  lab = pw_label_new(card, pw_i18n_is_zh() ? "收发日志" : "TX/RX log",
                     PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 24, 6);

  for (i = 0; i < PW_BT_LOG_LINES; i++)
    {
      g_btp_rows[i] = pw_label_new(card, "", PW_FNT_BODY, PW_COL_DIM);
      lv_obj_set_pos(g_btp_rows[i], 12, 30 + i * 17);
    }

  /* ── 按钮：发送测试 / 清空 ── */

  btn = pw_card_new(cont, 183, 48, PW_COL_CARD_LT);
  lv_obj_set_pos(btn, MENU_X0, 328);
  lv_obj_add_event_cb(btn, btp_send_cb, LV_EVENT_CLICKED, NULL);
  lab = pw_label_new(btn, pw_i18n_is_zh() ? "发送测试" : "Send test",
                     PW_FNT_MED, PW_COL_BT_ON);
  lv_obj_center(lab);

  btn = pw_card_new(cont, 183, 48, PW_COL_CARD);
  lv_obj_set_pos(btn, MENU_X0 + 183 + MENU_GAP, 328);
  lv_obj_add_event_cb(btn, btp_clear_cb, LV_EVENT_CLICKED, NULL);
  lab = pw_label_new(btn, pw_i18n_is_zh() ? "清空" : "Clear",
                     PW_FNT_MED, PW_COL_DIM);
  lv_obj_center(lab);

  /* 300ms 刷新：够快（连接状态肉眼即时）又够省（没变时只做两次比较） */
  pw_scr_set_tick(scr, btp_tick, 300);
  btp_tick(NULL);

  return scr;
}

/* 语音助手页：应答文案（设备无离线 TTS/ASR，真识别需 Agent/网络，页面内如实标注） */

lv_obj_t *pw_voice_screen(void)
{
  lv_obj_t *scr = pw_scr_new();
  lv_obj_t *cont = pw_topbar(scr, "语音助手");
  lv_obj_t *card;
  lv_obj_t *lab;

  card = pw_card_new(cont, PW_SCREEN_W - 2 * MENU_X0, 150, PW_COL_CARD);
  lv_obj_set_pos(card, MENU_X0, MENU_GAP);

  lab = pw_label_new(card, "我是腕上物理工坊语音助手",
                     PW_FNT_MED, PW_COL_TEXT);
  lv_obj_set_pos(lab, 16, 16);
  lab = pw_label_new(card, "可说：打开单摆 / 回主页 / 打开设置",
                     PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, 16, 58);
  lab = pw_label_new(card, "离线：语音需 Agent 或网络",
                     PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 16, 100);
  return scr;
}

/* ── 主题与图标色相 ─────────────────────────────────────────────────
 *
 * 主题两套：方格（2×4 宫格，B 方案）/ 单排（分栏列表，A 方案）。
 * 只存在 RAM 里 —— 本板没有 NVS 后端（CONFIG_SETTINGS 未开），重启回方格。
 *
 * 图标色相：8 个低饱和高明度的颜色，靠 image_recolor 运行时染色，
 * 不占 flash。不用它时是单一中性色，用上之后仍能一眼分区。
 */

typedef enum
{
  PW_THEME_GRID = 0,
  PW_THEME_LIST = 1
} pw_theme_t;

static pw_theme_t g_theme = PW_THEME_GRID;

static const uint32_t k_icon_hue[8] =
{
  0x7DD3FC,   /* 原始传感器 天蓝 */
  0x86EFAC,   /* 力学       薄荷绿 */
  0xFCD34D,   /* 声学       琥珀 */
  0xC4B5FD,   /* 工具       淡紫 */
  0x67E8F9,   /* 计时       青 */
  0xFDBA74,   /* 生活       杏 */
  0x94A3B8,   /* 自定义     灰（未上线） */
  0xF9A8D4,   /* AI 教练    粉 */
};

void pw_ui_set_theme(int list_mode)
{
  g_theme = list_mode ? PW_THEME_LIST : PW_THEME_GRID;
}

int pw_ui_theme(void)
{
  return (int)g_theme;
}

static void ui_theme_async(void *unused)
{
  (void)unused;
  pw_ui_root();
}

static void ui_theme_cb(lv_event_t *e)
{
  g_theme = (lv_event_get_user_data(e) != NULL) ? PW_THEME_LIST
                                                : PW_THEME_GRID;

  /* 写 flash 会阻塞几十 ms，但只在用户点"方格/单排"时发生一次 */

  pw_settings_save();

  /* 不能在事件回调里直接删掉自己所在的屏，交给 LVGL 异步执行 */

  lv_async_call(ui_theme_async, NULL);
}

/* [PW_AI_AUTORUN] AI 主动实验开关：关 → 只记日志；开 → 检测到摆动自动跑单摆。 */

static void ui_ai_auto_async(void *arg)
{
  pw_scr_open(pw_settings_screen());
}

static void ui_ai_auto_cb(lv_event_t *e)
{
  pw_ai_auto_set(lv_event_get_user_data(e) != NULL);
  syslog(LOG_INFO, "[settings] ai auto-run = %d\n", pw_ai_auto_get() ? 1 : 0);
  lv_async_call(ui_ai_auto_async, NULL);
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

  pw_section_bar(card, PW_ACC_ACTIVE, 16, 14);
  lab = pw_label_new(card, PW_STR(UI_LANGUAGE), PW_FNT_MED, PW_COL_TEXT);
  lv_obj_set_pos(lab, 26, 12);

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

  /* --- 显示方式（主题） --- */

  card = pw_card_new(cont, PW_SCREEN_W - 2 * MENU_X0, 118, PW_COL_CARD);
  lv_obj_set_pos(card, MENU_X0, MENU_GAP + 130);

  pw_section_bar(card, PW_ACC_ACTIVE, 16, 14);
  lab = pw_label_new(card, "显示方式", PW_FNT_MED, PW_COL_TEXT);
  lv_obj_set_pos(lab, 26, 12);

  /* 提示行只用 ASCII：中文字库是 512 字子集，"宫/栏/列/界" 都不在，
   * 实测会渲染成豆腐块（同关于页那次）。 */

  lab = pw_label_new(card, "2x4 grid  /  1 column", PW_FNT_BODY,
                     PW_COL_FAINT);
  lv_obj_set_pos(lab, 16, 46);

  btn = pw_card_new(card, 120, 40,
                    g_theme == PW_THEME_GRID ? PW_COL_CARD_LT : PW_COL_CARD);
  lv_obj_set_pos(btn, 16, 70);
  lab = pw_label_new(btn, "方格", PW_FNT_MED,
                     g_theme == PW_THEME_GRID ? PW_ACC_ACTIVE : PW_COL_DIM);
  lv_obj_center(lab);
  lv_obj_add_event_cb(btn, ui_theme_cb, LV_EVENT_CLICKED, NULL);

  btn = pw_card_new(card, 120, 40,
                    g_theme == PW_THEME_LIST ? PW_COL_CARD_LT : PW_COL_CARD);
  lv_obj_set_pos(btn, 168, 70);
  lab = pw_label_new(btn, "单排", PW_FNT_MED,
                     g_theme == PW_THEME_LIST ? PW_ACC_ACTIVE : PW_COL_DIM);
  lv_obj_center(lab);
  lv_obj_add_event_cb(btn, ui_theme_cb, LV_EVENT_CLICKED, (void *)1);

  /* --- AI 主动实验（默认关） --- */

  card = pw_card_new(cont, PW_SCREEN_W - 2 * MENU_X0, 92, PW_COL_CARD);
  lv_obj_set_pos(card, MENU_X0, MENU_GAP + 348);

  pw_section_bar(card, PW_ACC_ACTIVE, 16, 14);
  lab = pw_label_new(card, "AI auto-run", PW_FNT_MED, PW_COL_TEXT);
  lv_obj_set_pos(lab, 26, 10);

  lab = pw_label_new(card, "swing -> run pendulum", PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 16, 38);

  btn = pw_card_new(card, 120, 34,
                    !pw_ai_auto_get() ? PW_COL_CARD_LT : PW_COL_CARD);
  lv_obj_set_pos(btn, 16, 54);
  lab = pw_label_new(btn, "Off", PW_FNT_MED,
                     !pw_ai_auto_get() ? PW_ACC_ACTIVE : PW_COL_DIM);
  lv_obj_center(lab);
  lv_obj_add_event_cb(btn, ui_ai_auto_cb, LV_EVENT_CLICKED, NULL);

  btn = pw_card_new(card, 120, 34,
                    pw_ai_auto_get() ? PW_COL_CARD_LT : PW_COL_CARD);
  lv_obj_set_pos(btn, 168, 54);
  lab = pw_label_new(btn, "On", PW_FNT_MED,
                     pw_ai_auto_get() ? PW_ACC_ACTIVE : PW_COL_DIM);
  lv_obj_center(lab);
  lv_obj_add_event_cb(btn, ui_ai_auto_cb, LV_EVENT_CLICKED, (void *)1);

  /* --- 关于 --- */

  card = pw_card_new(cont, PW_SCREEN_W - 2 * MENU_X0, 64, PW_COL_CARD);
  lv_obj_set_pos(card, MENU_X0, MENU_GAP + 260);

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
  lv_obj_t *lab;
  lv_obj_t *img;
  int i;

  /* 关于页按 WORK 端 UI-SPEC §四 D 排：双标识 → 字标 → 副标 → 版本 → 键值行。
   * 这一页是**唯一使用真实标识**的地方（MI 与 openvela 都是官方素材），
   * 所以它不用卡片容器，直接排在页面底色上，留白按规格给。 */

  /* 标签用字必须落在 pw_font_14 的 512 字子集里：芯(0x82AF)、玩(0x73A9)
   * 都不在，实测会渲染成豆腐块；换成 主板(0x4E3B/0x677F)、可用(0x53EF/0x7528)
   * 后正常。以后新增文案先查子集再写。 */

  static const char *const kv_k[] =
  {
    "设备名", "固件", "主板", "板块",
  };
  static const char *const kv_v[] =
  {
    "PhyWear", "v0.1.0", "SF32LB52", "13 个可用",
  };

  scr = pw_scr_new();
  cont = pw_topbar(scr, PW_STR(UI_ABOUT));

  /* 双标识：各 48px，间距 20，整体居中（116 宽 → x=137） */

  img = lv_image_create(cont);
  lv_image_set_src(img, &pw_logo_mi_48);
  lv_obj_set_pos(img, 137, 66 - PW_TOPBAR_H);

  img = lv_image_create(cont);
  lv_image_set_src(img, &pw_logo_ov_48);
  lv_obj_set_pos(img, 137 + 48 + 20, 66 - PW_TOPBAR_H);

  lab = pw_label_new(cont, "PhyWear", PW_FNT_LARGE, PW_COL_TEXT);
  lv_obj_align(lab, LV_ALIGN_TOP_MID, 0, 128 - PW_TOPBAR_H);

  lab = pw_label_new(cont, PW_STR(UI_SUBTITLE), PW_FNT_BODY, PW_COL_DIM);
  lv_obj_align(lab, LV_ALIGN_TOP_MID, 0, 160 - PW_TOPBAR_H);

  lab = pw_label_new(cont, "v0.1.0", PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_align(lab, LV_ALIGN_TOP_MID, 0, 182 - PW_TOPBAR_H);

  /* 键值行：y=216 起，行距 34，分隔线 x 20→370 */

  for (i = 0; i < 4; i++)
    {
      int y = 216 - PW_TOPBAR_H + i * 34;

      lab = pw_label_new(cont, kv_k[i], PW_FNT_BODY, PW_COL_DIM);
      lv_obj_set_pos(lab, 20, y);

      lab = pw_label_new(cont, kv_v[i], PW_FNT_BODY, PW_COL_TEXT);
      lv_obj_align(lab, LV_ALIGN_TOP_RIGHT, -20, y);

      if (i < 3)
        {
          lv_obj_t *line = lv_obj_create(cont);
          lv_obj_set_size(line, PW_SCREEN_W - 40, 1);
          lv_obj_set_pos(line, 20, y + 26);
          lv_obj_set_style_bg_color(line, PW_COL_LINE, 0);
          lv_obj_set_style_bg_opa(line, LV_OPA_40, 0);
          lv_obj_set_style_border_width(line, 0, 0);
          lv_obj_set_style_radius(line, 0, 0);
          lv_obj_remove_flag(line, LV_OBJ_FLAG_SCROLLABLE);
        }
    }

  return scr;
}

/****************************************************************************
 * Name: pw_ui_root
 *
 * Description:
 *   主菜单宫格根屏。
 *
 ****************************************************************************/

/****************************************************************************
 * Name: root_build_list
 *
 * Description:
 *   分栏列表布局（UI-SPEC §四 A）。八行，行高 46，图标 24 @ x=20，
 *   标题 16 / 副标 14 @ x=56，右侧 chevron，行间发丝线 x=56→376。
 *
 *   行本身是 390x46 的透明容器：整行可点（点击区远大于 44x44），
 *   按压反馈用统一的 pw_press_style()。
 *
 ****************************************************************************/

static void root_build_list(lv_obj_t *scr, const struct pw_board_s *boards,
                            int nboards)
{
  int i;

  for (i = 0; i < nboards; i++)
    {
      const struct pw_board_s *b = &boards[i];
      int y = 74 + i * 46;
      lv_obj_t *row = lv_obj_create(scr);
      lv_obj_t *img;
      lv_obj_t *lab;

      lv_obj_set_size(row, PW_SCREEN_W, 46);
      lv_obj_set_pos(row, 0, y);
      lv_obj_set_style_bg_opa(row, LV_OPA_TRANSP, 0);
      lv_obj_set_style_border_width(row, 0, 0);
      lv_obj_set_style_pad_all(row, 0, 0);
      lv_obj_set_style_radius(row, 0, 0);
      lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);

      if (b->live)
        {
          lv_obj_add_event_cb(row, ui_tile_cb, LV_EVENT_CLICKED,
                              (void *)b->open);
          pw_press_style(row);
        }

      img = lv_image_create(row);
      lv_image_set_src(img, b->icon);
      lv_obj_set_pos(img, 20, 11);
      lv_obj_set_style_image_recolor(
          img, b->live ? lv_color_hex(k_icon_hue[i]) : PW_COL_FAINT, 0);
      lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);

      lab = pw_label_new(row, b->name, PW_FNT_SMALL,
                         b->live ? PW_COL_TEXT : PW_COL_FAINT);
      lv_obj_set_pos(lab, 56, 4);

      lab = pw_label_new(row, b->info, PW_FNT_BODY,
                         b->live ? PW_COL_DIM : PW_COL_FAINT);
      lv_obj_set_pos(lab, 56, 25);

      if (b->live)
        {
          img = lv_image_create(row);
          lv_image_set_src(img, &pw_icon_chev_24);
          lv_obj_set_pos(img, 352, 11);
          lv_obj_set_style_image_recolor(img, PW_COL_FAINT, 0);
          lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);
        }

      if (i < nboards - 1)
        {
          lv_obj_t *ln = lv_obj_create(scr);
          lv_obj_set_size(ln, PW_SCREEN_W - 56 - 14, 1);
          lv_obj_set_pos(ln, 56, y + 45);
          lv_obj_set_style_bg_color(ln, PW_COL_LINE, 0);
          lv_obj_set_style_bg_opa(ln, LV_OPA_40, 0);
          lv_obj_set_style_border_width(ln, 0, 0);
          lv_obj_set_style_radius(ln, 0, 0);
          lv_obj_remove_flag(ln, LV_OBJ_FLAG_SCROLLABLE);
        }
    }
}

void pw_ui_root(void)
{
  const struct pw_board_s boards[] =
  {
    { PW_STR(UI_RAW_SENSORS), PW_STR(UI_RAW_DESC),    0, true,  pw_raw_screen,  &pw_icon_raw_24 },
    { PW_STR(UI_MECHANICS),   PW_STR(UI_MECH_DESC),   0, true,  ui_mech_open,   &pw_icon_mech_24 },
    { PW_STR(UI_ACOUSTICS),   PW_STR(UI_ACOU_DESC),   0, true,  ui_acou_open,   &pw_icon_acou_24 },
    { PW_STR(UI_TOOLS),       PW_STR(UI_TOOLS_DESC),  0, true,  ui_tool_open,   &pw_icon_tools_24 },
    { PW_STR(UI_TIMERS),      PW_STR(UI_TIMERS_DESC), 0, true,  ui_time_open,   &pw_icon_timer_24 },
    { PW_STR(UI_EVERYDAY),    PW_STR(UI_EVERY_DESC),  0, true,  ui_every_open,  &pw_icon_daily_24 },
    { PW_STR(UI_CUSTOM),      PW_STR(UI_CUSTOM_DESC), 0, true,  ui_custom_open, &pw_icon_custom_24 },
    { PW_STR(UI_AI_COACH),    PW_STR(UI_AI_DESC),     0, true,  ui_ai_open,     &pw_icon_ai_24 },
  };
  const int nboards = sizeof(boards) / sizeof(boards[0]);
  lv_obj_t *scr;
  lv_obj_t *lab;
  lv_obj_t *btn;
  int i;

  /* 可重复调用（语言切换后重建根屏）：**先释放尚未出栈的旧屏**再重置栈。
   * 只把 g_scr_n 归零而不删除对象，会让旧屏的资源泄漏——尤其 pw_scope
   * 只有 6 个槽位，反复重建根屏（如 `phywear cap root`）会耗尽槽位，
   * 之后打开的曲线页静默没有曲线。 */

  while (g_scr_n > 0)
    {
      g_scr_n--;

      if (g_scr_stack[g_scr_n] != NULL)
        {
          lv_obj_delete(g_scr_stack[g_scr_n]);
          g_scr_stack[g_scr_n] = NULL;
        }
    }

  g_scr_n = 0;
  g_pending_timer = NULL;

  /* 一次性注册顶栏运行计时器（1Hz） */
  if (!g_timer_started)
    {
      g_timer_started = true;
      lv_timer_create(ui_timer_tick, 1000, NULL);
    }

  /* 一次性注册蓝牙状态轮询（4Hz）。
   * 为什么用轮询而不是"回调里直接改界面"：连接/断开发生在 Zephyr 工作队列
   * 线程上，那里碰 LVGL 会跨线程操作渲染器。250ms 的轮询对"插上充电器才变"
   * 这种状态量绰绰有余，而且状态没变时这一次回调只做两次比较就返回。 */
  if (!g_bt_timer_started)
    {
      g_bt_timer_started = true;
      lv_timer_create(ui_bt_tick, 250, NULL);
    }

  scr = pw_scr_new();
  lv_obj_add_event_cb(scr, ui_bt_scr_del_cb, LV_EVENT_DELETE, NULL);

  /* 标题行（左边缘圆弧屏切角：文本需右移进安全区） */

  lab = pw_label_new(scr, PW_STR(UI_PHYWEAR), PW_FNT_XL, PW_COL_TEXT);
  lv_obj_set_pos(lab, MENU_X0 + 28, 14);

  lab = pw_label_new(scr, PW_STR(UI_SUBTITLE),
                     PW_FNT_BODY, PW_COL_DIM);
  lv_obj_set_pos(lab, MENU_X0 + 28, 46);

  /* 蓝牙链路指示（灰色=未连接 / 蓝色=已连接）。
   * 放在"语""…"两个入口的左边 —— 见 ui_bt_indicator_add() 里的几何说明。 */

  ui_bt_indicator_add(scr);

  /* 根屏电源指示：放在标题与蓝牙指示之间的空档（x≈146..188），
   * 几何见 ui_bt_indicator_add() 的说明。 */

  /* 头部 0..54 被 标题/蓝牙/语/设置 占满，宫格从 y=70 开始：
   * 只剩 54..70 这条 16 px 的横带（正好一个 pw_font_16 的高度），
   * 右对齐贴齐"…"按钮右缘放电量，既不压控件也不压卡片。 */

  ui_bat_add(scr, LV_ALIGN_TOP_RIGHT, -14, 50);

  /* 右上角设置入口（phyphox 溢出菜单思想的极简版） */

  btn = lv_obj_create(scr);
  lv_obj_set_size(btn, 60, 44);
  lv_obj_set_pos(btn, PW_SCREEN_W - 60 - 20, 10);
  lv_obj_set_style_bg_opa(btn, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(btn, 0, 0);
  lv_obj_set_style_pad_all(btn, 0, 0);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_event_cb(btn, ui_settings_btn_cb, LV_EVENT_CLICKED, NULL);
  pw_press_style(btn);

  {
    lv_obj_t *vb = lv_obj_create(scr);

    lv_obj_set_size(vb, 56, 44);
    lv_obj_set_pos(vb, PW_SCREEN_W - 60 - 20 - 56, 10);
    lv_obj_set_style_bg_opa(vb, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(vb, 0, 0);
    lv_obj_set_style_pad_all(vb, 0, 0);
    lv_obj_remove_flag(vb, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_event_cb(vb, ui_voice_cb, LV_EVENT_CLICKED, NULL);
    pw_press_style(vb);
    /* 语音入口：麦克风线性图标（MINIMAL-B §3），比汉字"语"更统一 */

    {
      lv_obj_t *mic = lv_image_create(vb);
      lv_image_set_src(mic, &pw_icon_mic_24);
      lv_obj_center(mic);
      lv_obj_set_style_image_recolor(mic, PW_COL_DIM, 0);
      lv_obj_set_style_image_recolor_opa(mic, LV_OPA_COVER, 0);
    }
  }

  /* 设置入口：省略号线性图标（原为 28px "..." 文本） */

  {
    lv_obj_t *more = lv_image_create(btn);
    lv_image_set_src(more, &pw_icon_more_24);
    lv_obj_center(more);
    lv_obj_set_style_image_recolor(more, PW_COL_DIM, 0);
    lv_obj_set_style_image_recolor_opa(more, LV_OPA_COVER, 0);
  }

  /* 分栏列表主题：布局完全不同，直接走另一条构建路径 */

  if (g_theme == PW_THEME_LIST)
    {
      root_build_list(scr, boards, nboards);
      pw_scr_open(scr);
      return;
    }

  /* 板块宫格 2×4 */

  for (i = 0; i < nboards; i++)
    {
      int col = i % 2;
      int row = i / 2;
      int x = MENU_X0 + col * (MENU_TILE_W + MENU_GAP);
      int y = MENU_Y0 + row * (MENU_TILE_H + MENU_GAP);
      const struct pw_board_s *b = &boards[i];
      lv_obj_t *tile;
      lv_obj_t *name;
      lv_obj_t *info;

      tile = pw_card_new(scr, MENU_TILE_W, MENU_TILE_H, PW_COL_CARD);
      lv_obj_set_pos(tile, x, y);
      lv_obj_set_style_bg_color(tile, PW_COL_CARD_LT, LV_STATE_PRESSED);
      lv_obj_add_event_cb(tile, ui_tile_cb, LV_EVENT_CLICKED,
                          (void *)b->open);
#if PW_UI_MOTION
      /* 按压反馈走 pw_card_new() 里的统一按压样式（位移+底色，120ms C4）；
       * 这里只做**入场错峰**：8 块依次从 +12px 落位。EVENT_BUBBLE 让点在
       * 图标/文字上的点击仍冒泡到 tile。 */

      lv_obj_add_flag(tile, LV_OBJ_FLAG_EVENT_BUBBLE);
      pw_motion_slide_in_y_at(tile, y, 12, 240, (uint32_t)i * 40);
#endif

      /* 板块图标：Lucide 线性图标，统一中性色，靠形状而不是颜色区分版块。
       * 图标是 RGB565A8 白色遮罩，运行时 recolor，一份数组可染任意色。 */

      lv_obj_t *img = lv_image_create(tile);
      lv_image_set_src(img, b->icon);
      /* ⑤ 卡片内边距 = 卡片间距(8)：图标从 (12,14) 收到 (8,12)，
       * 文字相应左移到 40，视觉上四边留白一致。 */

      lv_obj_set_pos(img, 8, 12);
      lv_obj_set_style_image_recolor(
          img, b->live ? lv_color_hex(k_icon_hue[i]) : PW_COL_FAINT, 0);
      lv_obj_set_style_image_recolor_opa(img, LV_OPA_COVER, 0);

      /* 名称 + 状态 */

      name = pw_label_new(tile, b->name, PW_FNT_MED,
                          b->live ? PW_COL_TEXT : PW_COL_FAINT);
      lv_obj_set_pos(name, 40, 12);

      info = pw_label_new(tile, b->info, PW_FNT_BODY,
                          b->live ? PW_COL_DIM : PW_COL_FAINT);
      lv_obj_set_pos(info, 40, 52);
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

/****************************************************************************
 * 长按电源键菜单：关机 / 重启 / 取消
 *
 * 背景：这块板子长按 KEY2 由 PMU/LCPU 直接掉电，随后电源键释放又会自己起来，
 * 用户看到的就是"关机了但马上又自动重启"。所以在软件里先截住长按，让用户
 * 明确选一次：关机（真断电，保持关）、重启（立刻重启）、取消。
 *
 * 实现上放在 layer_top 上做模态层，不切屏、不重建 UI；菜单存在时吞掉点击，
 * 避免误触到下面的卡片。
 ****************************************************************************/

static lv_obj_t *g_pwr_menu;

static void pw_pwr_menu_close(void)
{
  if (g_pwr_menu != NULL)
    {
      lv_obj_delete(g_pwr_menu);
      g_pwr_menu = NULL;
    }
}

static void ui_pwr_cancel_cb(lv_event_t *e)
{
  LV_UNUSED(e);
  pw_pwr_menu_close();
}

static void ui_pwr_off_cb(lv_event_t *e)
{
  LV_UNUSED(e);

  /* 先落盘再断电：否则下次上电又回到默认主题/语言 */

  pw_pwr_menu_close();
  pw_power_shutdown();
}

static void ui_pwr_reboot_cb(lv_event_t *e)
{
  LV_UNUSED(e);
  pw_pwr_menu_close();
  pw_power_reboot();
}

static lv_obj_t *pw_pwr_btn(lv_obj_t *parent, const char *text, int y,
                            lv_color_t bg, lv_event_cb_t cb)
{
  lv_obj_t *btn = lv_obj_create(parent);

  lv_obj_remove_style_all(btn);
  lv_obj_set_size(btn, 190, 46);
  lv_obj_set_pos(btn, 20, y);   /* 卡片 230 宽，按钮 190：左右各 20 */
  lv_obj_set_style_radius(btn, 10, 0);
  lv_obj_set_style_bg_color(btn, bg, 0);
  lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
  lv_obj_remove_flag(btn, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *lab = pw_label_new(btn, text, PW_FNT_SMALL, PW_COL_TEXT);
  lv_obj_center(lab);

  return btn;
}

void pw_ui_power_menu(void)
{
  if (g_pwr_menu != NULL)
    {
      return;
    }

  g_pwr_menu = lv_obj_create(lv_layer_top());
  lv_obj_remove_style_all(g_pwr_menu);
  lv_obj_set_size(g_pwr_menu, 390, 450);
  lv_obj_set_pos(g_pwr_menu, 0, 0);
  lv_obj_set_style_bg_color(g_pwr_menu, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_pwr_menu, LV_OPA_70, 0);
  lv_obj_remove_flag(g_pwr_menu, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_add_flag(g_pwr_menu, LV_OBJ_FLAG_CLICKABLE);

  lv_obj_t *card = lv_obj_create(g_pwr_menu);
  lv_obj_remove_style_all(card);
  lv_obj_set_size(card, 230, 214);
  lv_obj_align(card, LV_ALIGN_CENTER, 0, 0);
  lv_obj_set_style_radius(card, 14, 0);
  lv_obj_set_style_bg_color(card, PW_COL_CARD, 0);
  lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(card, 1, 0);
  lv_obj_set_style_border_color(card, PW_COL_LINE, 0);
  lv_obj_remove_flag(card, LV_OBJ_FLAG_SCROLLABLE);

  lv_obj_t *title = pw_label_new(card,
                                 pw_i18n_is_zh() ? "请选择" : "Power",
                                 PW_FNT_MED, PW_COL_TEXT);
  lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 16);

  /* 说明一行：为什么长按不直接关机 */

  lv_obj_t *hint = pw_label_new(card,
                                pw_i18n_is_zh() ? "长按 KEY2" : "Long press KEY2",
                                PW_FNT_BODY, PW_COL_DIM);
  lv_obj_align(hint, LV_ALIGN_TOP_MID, 0, 46);

  pw_pwr_btn(card, pw_i18n_is_zh() ? "关机" : "Power off", 76,
             lv_color_hex(0x7F1D1D), ui_pwr_off_cb);
  pw_pwr_btn(card, pw_i18n_is_zh() ? "重启" : "Reboot", 130,
             lv_color_hex(0x1E3A8A), ui_pwr_reboot_cb);

  lv_obj_t *cancel = lv_obj_create(card);
  lv_obj_remove_style_all(cancel);
  lv_obj_set_size(cancel, 190, 30);
  lv_obj_set_pos(cancel, 20, 178);
  lv_obj_add_flag(cancel, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_event_cb(cancel, ui_pwr_cancel_cb, LV_EVENT_CLICKED, NULL);

  lv_obj_t *clab = pw_label_new(cancel,
                                pw_i18n_is_zh() ? "取消" : "Cancel",
                                PW_FNT_BODY, PW_COL_DIM);
  lv_obj_center(clab);

  lv_obj_move_foreground(g_pwr_menu);
}
