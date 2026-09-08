/****************************************************************************
 * apps/examples/phywear/phywear_ui.h
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

/* UI 骨架：主题 + 屏幕管理（栈式导航）+ 顶栏 + 板块宫格 + 实验列表。
 *
 * 导航模型（phyphox 对齐）：
 *   主菜单宫格（根屏，无返回）
 *     └─ 板块实验列表或实验页（顶栏带返回箭头，压栈）
 * 屏幕 = 由各模块 build 出的 lv_obj_t*，经 pw_scr_open() 压栈显示；
 * 返回 = pw_scr_back() 弹栈删除。
 *
 * i18n 备注：当前文案为英文（montserrat 无 CJK 字形）。文案集中在本文件
 * 各数组/字符串常量处，汉化时替换字符串表并启用 CJK 字体即可。
 */

#ifndef __APPS_EXAMPLES_PHYWEAR_UI_H
#define __APPS_EXAMPLES_PHYWEAR_UI_H

#include <lvgl/lvgl.h>

struct pw_graph_s;   /* 前向声明，避免强依赖 pw_graph.h */

/* ---- 屏幕尺寸 ---- */

#define PW_SCREEN_W  390
#define PW_SCREEN_H  450
#define PW_TOPBAR_H  54

/* ---- 主题 ---- */

#define PW_COL_BG       lv_color_hex(0x101418)  /* 深色底 */
#define PW_COL_CARD     lv_color_hex(0x1a222c)  /* 卡片底 */
#define PW_COL_CARD_LT  lv_color_hex(0x26333f)  /* 卡片高亮（按压） */
#define PW_COL_TEXT     lv_color_white()
#define PW_COL_DIM      lv_color_hex(0x8a97a5)  /* 次级文字 */
#define PW_COL_FAINT    lv_color_hex(0x5b6875)  /* 禁用/占位文字 */

/* 板块强调色 */
#define PW_ACC_RAW      lv_color_hex(0x4fc3f7)  /* 原始传感器：青蓝 */
#define PW_ACC_MECH     lv_color_hex(0x81c784)  /* 力学：绿 */
#define PW_ACC_ACOU     lv_color_hex(0xffb74d)  /* 声学：橙 */
#define PW_ACC_TOOL     lv_color_hex(0xba68c8)  /* 工具：紫 */
#define PW_ACC_TIME     lv_color_hex(0x4dd0e1)  /* 计时器：青 */
#define PW_ACC_EVERY    lv_color_hex(0xff8a65)  /* 生活：珊瑚 */
#define PW_ACC_CUSTOM   lv_color_hex(0x90a4ae)  /* 自定义：灰蓝 */
#define PW_ACC_AI       lv_color_hex(0xf06292)  /* AI：粉 */

/* 传感器页强调色 */
#define PW_ACC_ACC      lv_color_hex(0x4fc3f7)
#define PW_ACC_GYRO     lv_color_hex(0x64b5f6)
#define PW_ACC_MAG      lv_color_hex(0xaed581)
#define PW_ACC_LIGHT    lv_color_hex(0xffb74d)

/* phyphox 灵感：图表/数据系列命名饱和色（对齐 phyphox RGB.java 调色板）。
 * 只用于小尺寸数据元素（曲线/散点/指示），不做大文字，保证暗底可读。 */
#define PW_SER_RED      lv_color_hex(0xfe005d)
#define PW_SER_GREEN    lv_color_hex(0x2bfb4c)
#define PW_SER_BLUE     lv_color_hex(0x39a2ff)
#define PW_SER_ORANGE   lv_color_hex(0xff7e22)
#define PW_SER_MAGENTA  lv_color_hex(0xeb46f4)
#define PW_SER_YELLOW   lv_color_hex(0xedf668)

/* 活动/测量中 指示强调（小尺寸专用，非大文字） */
#define PW_ACC_ACTIVE   lv_color_hex(0xff9a3d)

/* 图表网格/刻度线（暗底低干扰） */
#define PW_COL_GRID     lv_color_hex(0x3a4550)

/* 常用字体快捷宏。
 * BODY..XL = PhyWear 合并字体 pw_font_*（Montserrat Latin + Droid CJK 子集，
 * i18n 阶段2），XXL 保持 montserrat_48（纯数字大读数，无需 CJK）。
 * 字体定义见 pw_font_{14,16,20,24,28}.c。 */

extern const lv_font_t pw_font_14;
extern const lv_font_t pw_font_16;
extern const lv_font_t pw_font_20;
extern const lv_font_t pw_font_24;
extern const lv_font_t pw_font_28;

#define PW_FNT_BODY     &pw_font_14
#define PW_FNT_SMALL    &pw_font_16
#define PW_FNT_MED      &pw_font_20
#define PW_FNT_LARGE    &pw_font_24
#define PW_FNT_XL       &pw_font_28
#define PW_FNT_XXL      &lv_font_montserrat_48

/****************************************************************************
 * 屏幕管理
 ****************************************************************************/

/* 新建一屏（深色背景） */

lv_obj_t *pw_scr_new(void);

/* 压栈并加载一屏（成为活动屏；其上的返回箭头弹回上一屏） */

void pw_scr_open(lv_obj_t *scr);

/* 返回上一屏（删除当前屏；根屏调用无效） */

void pw_scr_back(void);

/* 给屏幕挂周期刷新定时器：屏幕对象被删除时定时器自动删除。
 * 注意：定时器回调应自行判断 lv_scr_act() 是否为该屏，避免后台空转。 */

void pw_scr_set_tick(lv_obj_t *scr, lv_timer_cb_t cb, uint32_t period_ms);

/* 标准顶栏：返回箭头 + 居中标题；返回其下方的内容区容器
 * （位置 (0, PW_TOPBAR_H)，大小 390 × (450-PW_TOPBAR_H)）。 */

lv_obj_t *pw_topbar(lv_obj_t *scr, const char *title);

/****************************************************************************
 * 页面元素小工具
 ****************************************************************************/

/* 圆角卡片容器（无边框） */

lv_obj_t *pw_card_new(lv_obj_t *parent, int w, int h, lv_color_t bg);

/* 标签快捷创建 */

lv_obj_t *pw_label_new(lv_obj_t *parent, const char *text,
                       const lv_font_t *font, lv_color_t color);

/* 图表下方一排缩放/平移/适配按钮（单点触摸无法捏合，用按钮替代）。
 * 操作码：X-/X+/Y-/Y+/Fit/Auto；缩放后把窗口范围回传给 status 回调。
 * 统一各实验页的图控条；status==NULL 时不做范围回传。 */
typedef void (*pw_gctl_status_cb)(void *ud, const char *msg);

void pw_graph_controls_add(lv_obj_t *card, struct pw_graph_s *g,
                           pw_gctl_status_cb status, void *ud);

/****************************************************************************
 * 导航页（板块内容）
 ****************************************************************************/

struct pw_exp_s
{
  const char *name;         /* 实验名 */
  const char *desc;         /* 一句话说明 */
  lv_obj_t *(*open)(void);  /* 打开实验页；NULL = 未实现（置灰显示） */
};

/* 实验列表页（顶栏带返回）。items 需为静态存储。返回新屏对象。 */

lv_obj_t *pw_board_list(const char *title, lv_color_t accent,
                        FAR const struct pw_exp_s *items, int nitems);

/* 设置页：语言切换（EN/ZH）+ 关于/许可入口。返回新屏对象。 */

lv_obj_t *pw_settings_screen(void);

/* 关于页：版本 + 免责/许可说明。返回新屏对象。 */

lv_obj_t *pw_about_screen(void);

/* 构建并加载主菜单根屏（应用启动时调用一次；可重复调用以刷新语言） */

void pw_ui_root(void);

/* 自动演示（模拟验证/录屏用）：前台运行，自动"点击"依次打开各板块与实验页，
 * 播完自动回到根屏。pw_ui_demo_step() 推进一步（主循环按时间调用），
 * pw_ui_demo_done() 返回是否已播完（用于主循环退出）。 */
void pw_ui_demo_start(void);
int  pw_ui_demo_step(void);
bool pw_ui_demo_done(void);

#endif /* __APPS_EXAMPLES_PHYWEAR_UI_H */
