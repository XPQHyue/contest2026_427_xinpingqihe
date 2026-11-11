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

/* 注册一个"文本出口"：pw_ai_note() 每收到一条文本就调它一次。
 * 用于把 Agent 的回复推给蓝牙文本特征（pw_bt_send_text）。NULL 表示注销。 */

void pw_ai_set_text_sink(void (*fn)(const char *text));

/* 消息日志读取（GUI 线程）。idx 0 = 最新一条；越界返回 NULL。 */

int         pw_ai_log_count(void);
const char *pw_ai_log_line(int idx);

/* 该条消息距今多少秒（-1 表示无该条）。 */

long        pw_ai_log_age_s(int idx);

/****************************************************************************
 * 实验结果登记（GUI 线程写 / Agent 任务读）
 *
 * 为什么需要它：Agent 的 phywear_run_experiment 原来在 **agent 任务里**按 50 Hz
 * 自己采一遍 IMU，而屏幕上那个实验页同时也在 50 Hz 采同一颗传感器 → 两路 50 Hz
 * 抢同一个 I2C（oneshot 还每样本 open/ioctl/close 一次），真机实测十几秒整机卡死，
 * 主动场景因此被关掉（pw_watch.c 里 PW_WATCH_PROACTIVE=0）。
 *
 * 现在改成：**只有 GUI 线程那条通路采样**；实验页算完把结果登记到这里，
 * Agent 只"等结果 + 读结果"。I2C 永远只有一路。
 *
 * 线程安全：发布方是 GUI 线程（实验页 tick），读取方是 Agent 任务，用互斥锁保护；
 * 双方都不碰 LVGL 对象。
 ****************************************************************************/

#define PW_AI_RESULT_N          4     /* 最近 4 个实验各留一份 */
#define PW_AI_RESULT_KIND_MAX   16
#define PW_AI_RESULT_UNIT_MAX   12
#define PW_AI_RESULT_TEXT_MAX   80

struct pw_ai_result_s
{
  char  kind[PW_AI_RESULT_KIND_MAX];   /* "pendulum" / "spring" / "incline" ... */
  char  unit[PW_AI_RESULT_UNIT_MAX];   /* "m/s^2" ... */
  char  detail[PW_AI_RESULT_TEXT_MAX]; /* 页面自己显示的那一行（人类可读） */
  int   seq;                           /* 同一 kind 每发布一次 +1；等待方靠它判断新旧 */
  float value;                         /* 主结果 */
  float aux;                           /* 次结果（如周期 T），无则 0 */
};

/* 发布一条实验结果（GUI 线程调用；kind 相同则覆盖，seq 自增）。 */

void pw_ai_publish_result(const char *kind, float value, const char *unit,
                          float aux, const char *detail);

/* 取某实验当前结果序号；没有该 kind 返回 0。等待方先读一次、再轮询比较。 */

int pw_ai_result_seq(const char *kind);

/* 读某实验当前结果；成功返回 true 并填充 out。 */

bool pw_ai_result_get(const char *kind, FAR struct pw_ai_result_s *out);

#endif /* __APPS_EXAMPLES_PHYWEAR_PW_AI_H */

/* [PW_AI_AUTORUN] AI 主动实验开关（默认关）：检测到持续摆动时是否自动跑单摆实验。
 * 关的时候事件只进 AI 日志，不触发 agent，也不会跳页。 */

bool pw_ai_auto_get(void);
void pw_ai_auto_set(bool on);
