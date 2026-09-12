/****************************************************************************
 * apps/examples/phywear/pw_ai.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * PhyWear ←→ AI Agent 桥（线程安全）。
 *
 * 背景：PhyWear 的 GUI 主循环（main() 里的 while(1) + lv_timer_handler()）会
 * 一直占住控制台，而 AI Agent（packages/ai_agent）在前台跑自己的 vela> CLI；
 * 且 LVGL 默认**不是线程安全的**。因此 Agent 侧**不允许**直接调用 LVGL 接口。
 *
 * 做法：Agent 线程只把"要打开哪一页"写进一个邮箱；PhyWear 主循环每轮
 * 调用 pw_ai_poll() 取走并执行。这样所有 LVGL 调用都发生在 GUI 线程内。
 *
 * 传感器读取（pw_sensors_read_*）是普通字符设备 read()，不经 LVGL，
 * 可以直接从 Agent 线程调用（见 tool_phywear.c）。
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_PHYWEAR_PW_AI_H
#define __APPS_EXAMPLES_PHYWEAR_PW_AI_H

#include <stdbool.h>

/****************************************************************************
 * 页面目录（供 AI Agent 列出可选页面）
 ****************************************************************************/

int         pw_ai_screen_count(void);
const char *pw_ai_screen_name(int idx);
const char *pw_ai_screen_desc(int idx);

/****************************************************************************
 * 按名字打开页面（GUI 线程内实现，供 pw_ai_poll 调用）
 * 返回 1 表示名字有效并已打开，0 表示未知页名。
 ****************************************************************************/

int pw_cap_open(const char *name);

/****************************************************************************
 * 线程安全接口（Agent 线程调用）
 ****************************************************************************/

/* GUI 主循环是否在跑。false 时任何界面操作都不会被执行。 */

bool pw_ai_gui_running(void);

/* 请求打开某页。请求被记入邮箱，由 GUI 线程稍后执行。
 * 返回 0 表示受理，-ENODEV 表示 GUI 没在跑，-EINVAL 表示页名未知，-EBUSY 邮箱满。
 */

int pw_ai_request_open(const char *screen);

/* 当前（或最近一次打开的）页名；GUI 未启动时返回 ""。 */

const char *pw_ai_current_screen(void);

/****************************************************************************
 * GUI 线程接口
 ****************************************************************************/

/* 在 PhyWear 主循环里每轮调用：执行挂起的打开请求。 */

void pw_ai_poll(void);

/* GUI 主循环进入/退出时调用，用于 pw_ai_gui_running()。 */

void pw_ai_set_gui_running(bool running);

/* 记录当前页名（pw_cap_open 成功时由 GUI 线程写入）。 */

void pw_ai_note_screen(const char *name);

#endif /* __APPS_EXAMPLES_PHYWEAR_PW_AI_H */
