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

/****************************************************************************
 * AI 教练页：端侧 Agent 交互 + 消息日志（任意线程可调用）
 *
 * 目的：让手表 UI 上"看得见 AI"。按下 AI 教练页的按钮 → 向端侧 Agent 发一条
 * 自然语言请求（走 agent_loop 的离线意图表 / LLM）→ Agent 调工具（切页、读数、
 * 跑实验）→ 回复文本回落到消息日志 → 页面周期刷新显示。
 *
 * 线程安全：Agent 回复回调运行在其它任务里，只写下面的环形日志（加锁），
 * 不触碰任何 LVGL 对象；LVGL 更新一律由 GUI 线程的定时器完成。
 ****************************************************************************/

/* 向端侧 Agent 发一条自然语言请求（非阻塞；回复异步写入消息日志）。
 * 返回 0 = 已受理；-ENODEV = Agent 未运行/未编入；-EINVAL = 参数错。 */

int pw_ai_ask(const char *text);

/* Agent 上一次请求是否成功送达（用于页面上的状态点）。 */

bool pw_ai_agent_ready(void);

/* 记录一条文本到消息日志（Agent 回复、本地事件都走这里）。 */

void pw_ai_note(const char *text);

/* 消息日志读取（GUI 线程）。idx 0 = 最新一条；越界返回 NULL。 */

int         pw_ai_log_count(void);
const char *pw_ai_log_line(int idx);

/* 该条消息距今多少秒（-1 表示无该条）。 */

long        pw_ai_log_age_s(int idx);

#endif /* __APPS_EXAMPLES_PHYWEAR_PW_AI_H */
