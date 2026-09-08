/****************************************************************************
 * apps/examples/phywear/phywear_time.h
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

#ifndef __APPS_EXAMPLES_PHYWEAR_TIME_H
#define __APPS_EXAMPLES_PHYWEAR_TIME_H

/* T5 计时器（phyphox 秒表类对齐，共用 threshold 状态机 + 事件对计时）：
 *
 *   pw_motion_stopwatch_screen()   运动秒表：|a|-g 冲击上升沿
 *      每次冲击事件切换 开始/停止；显示两次事件间隔 dt（如两次敲击）。
 *   pw_light_gate_screen()         光学秒表：光暗跳变下降沿（LTR-303）
 *      挡光一次=事件；两次遮光间隔 dt。阈值可调（lux）。
 *   pw_acoustic_gate_screen()      声学秒表：麦克风 RMS 响亮上升沿
 *      拍手/响声事件对间隔 dt。阈值可调（dB）。
 *
 * 事件对计时语义（与 phyphox motion_stopwatch 一致）：
 *   第 1 次触发 = 开始计时，第 2 次触发 = 停止并显示 dt；
 *   下一次触发又开始新测量（Clear 清零显示）。 */

lv_obj_t *pw_motion_stopwatch_screen(void);
lv_obj_t *pw_light_gate_screen(void);
lv_obj_t *pw_acoustic_gate_screen(void);

/* 性能 bench：timebench <motion|light|acoustic> [秒 [起始页]]
 * 注入周期脉冲并自动翻页。 */
void pw_time_bench(int on, int page_interval_s, int start_page);

#endif /* __APPS_EXAMPLES_PHYWEAR_TIME_H */
