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

/* 色板来源：third_party/ui-ux-pro-max/data/colors-lvgl.csv
 * （上游 Product Type = Smart Home/IoT Dashboard 的 Slate 暗色档，已转 RGB565 并核对对比度：
 *   TEXT/BG 17.06:1、DIM/BG 6.96:1、CARD/BG 1.14:1、BORDER/BG 2.36:1） */

#define PW_COL_BG       lv_color_hex(0x0F172A)  /* 深色底（colors.csv Background） */
#define PW_COL_CARD     lv_color_hex(0x1B2336)  /* 卡片底（colors.csv Card） */
#define PW_COL_CARD_LT  lv_color_hex(0x272F42)  /* 卡片高亮/按压（colors.csv Muted） */
#define PW_COL_TEXT     lv_color_hex(0xF8FAFC)
#define PW_COL_DIM      lv_color_hex(0x94A3B8)  /* 次级文字（colors.csv Muted Foreground，6.96:1） */
#define PW_COL_FAINT    lv_color_hex(0x5b6875)  /* 禁用/占位文字 */
#define PW_COL_ICON     lv_color_hex(0xCBD5E1)  /* 线性图标（可用项），见 MINIMAL-B §4 */

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
#define PW_SER_RED      lv_color_hex(0xff5c7a)
#define PW_SER_GREEN    lv_color_hex(0x5ce08a)
#define PW_SER_BLUE     lv_color_hex(0x5aa9ff)
#define PW_SER_ORANGE   lv_color_hex(0xffa94d)
#define PW_SER_MAGENTA  lv_color_hex(0xd07cf5)
#define PW_SER_YELLOW   lv_color_hex(0xffe066)

/* 活动/测量中 指示强调（小尺寸专用，非大文字） */
#define PW_ACC_ACTIVE   lv_color_hex(0xff9a3d)

/* 图表网格/刻度线（暗底低干扰） */
#define PW_COL_LIVE     lv_color_hex(0x22C55E)  /* 实时/正常状态点（colors.csv Accent） */
#define PW_COL_LINE     lv_color_hex(0x475569)  /* 卡片 hairline 描边（colors.csv Border） */
#define PW_COL_GRID     lv_color_hex(0x475569)  /* 网格/描边（colors.csv Border） */

/* 蓝牙链路状态色（2026-09-18）。
 * 为什么这里多一个蓝：全局纪律是"只 1 个强调色"（绿 = 实时/正常），
 * 而这一枚是**设备状态指示**而不是新强调色 —— 与宫格"一区一色"同类。
 * 用户明确要求：未连接 = 灰，已连接 = 蓝。 */
#define PW_COL_BT_OFF   lv_color_hex(0x5B6875)  /* 未连接（= PW_COL_FAINT 灰） */
#define PW_COL_BT_ON    lv_color_hex(0x3B82F6)  /* 已连接（蓝） */

/* 常用字体快捷宏。
 * BODY..XL = PhyWear 合并字体 pw_font_*（Montserrat Latin + Droid CJK 子集，
 * i18n 阶段2），XXL 保持 montserrat_48（纯数字大读数，无需 CJK）。
 * 字体定义见 pw_font_{14,16,20,24,28}.c。 */

/* 性能打点（定义在 phywear.c）：控件树收到 PRESSED 时调一次，
 * 屏上帧率表把"触摸 → 下一帧"的毫秒数显示成 TOUCH。 */

void pw_perf_note_touch(void);

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

/* 统一按压反馈（LV_STATE_PRESSED 样式：translate_y +3、底色 Muted、120ms C4）。
 * pw_card_new() 已自动调用；非卡片的可点对象（自建 lv_obj_create）请手动调用。 */
void pw_press_style(lv_obj_t *obj);

/* 装饰性 obj（图标块/色条/网格线）取消 CLICKABLE：否则会把按压状态从卡片上抢走 */
void pw_deco(lv_obj_t *obj);

/* 章节条：3px 强调色小竖条（放在页头/卡片标题左侧，配合标题标签用，v2 观感）。
 * 纯色块 + 半径 2 + 无边框，不参与触摸。 */
void pw_section_bar(lv_obj_t *parent, lv_color_t accent, int x, int y);

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

/* 声学：音频发生器（用板载扬声器发正弦） */

lv_obj_t *pw_tone_screen(void);

lv_obj_t *pw_settings_screen(void);

/* 关于页：版本 + 免责/许可说明。返回新屏对象。 */

lv_obj_t *pw_about_screen(void);

/* AI 教练页（端侧 Agent 面板：状态 + 最近回复 + 4 个快捷请求） */

lv_obj_t *pw_ai_coach_screen(void);

/* 构建并加载主菜单根屏（应用启动时调用一次；可重复调用以刷新语言） */

void pw_ui_root(void);

/* 息屏 / 亮屏（T-0002）：背光 0 + 全屏遮罩挡误触；亮屏撤遮罩并恢复背光。
 * 长按任意屏也会走同一条路径；串口 `phywear sleep/wake` 供取证用。 */

void pw_ui_sleep_now(void);
void pw_ui_wake_now(void);

/* 在屏中央显示蓝牙配对码（由 BT 线程设置，GUI 线程负责画） */
void pw_ui_show_passkey(unsigned int code);
int  pw_ui_is_asleep(void);

lv_obj_t *pw_voice_screen(void);   /* 语音助手页（应答文案） */

/* 蓝牙页（手表版蓝牙串口）：链路状态 + 收发文本日志 + 连通性测试按钮。
 * 主界面标题右侧的灰/蓝状态点与这一页读的是同一份状态（pw_btgatt.h）。 */
lv_obj_t *pw_bt_screen(void);

/* 界面线程已经推了多少帧（单调增）。
 * 2026-09-18 加：给 pw_bt.c 的"后台起蓝牙时界面到底有没有被冻住"取证用 ——
 * 起栈前后各读一次，差值为 0 就说明界面那 1.2 s 里一帧都没画。 */
uint32_t pw_ui_flush_count(void);

/* 直接打开某个板块列表页（诊断/截图用）：0=力学 1=声学 2=工具 3=计时器 4=生活 5=自定义
 * 返回 0 = 索引越界。真机 UI 与 cap 截图共用同一入口。 */
int pw_ui_open_board(int idx);

/* 自动演示（模拟验证/录屏用）：前台运行，自动"点击"依次打开各板块与实验页，
 * 播完自动回到根屏。pw_ui_demo_step() 推进一步（主循环按时间调用），
 * pw_ui_demo_done() 返回是否已播完（用于主循环退出）。 */
void pw_ui_demo_start(void);
int  pw_ui_demo_step(void);
bool pw_ui_demo_done(void);

#endif /* __APPS_EXAMPLES_PHYWEAR_UI_H */

/* 蓝牙启动入口见 pw_bt.h（含幂等说明） */
