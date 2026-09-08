/****************************************************************************
 * apps/examples/phywear/phywear_pend.h
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

#ifndef __APPS_EXAMPLES_PHYWEAR_PEND_H
#define __APPS_EXAMPLES_PHYWEAR_PEND_H

/* 力学实验页：摆测 g（T2 采集-分析模板首例）。
 * 陀螺仪 ~50Hz 采样 10s → 自相关测周期 T → g = 4π²L/T²。
 * 参数 L 内嵌页面（步进 +/- 与预设），保证操作 ≤3 步。 */

lv_obj_t *pw_pendulum_screen(void);

/* 性能 bench：注入合成摆动信号并自动循环翻页（无需触摸/真人）。
 * on=1 开始（数据流改为合成正弦），page_interval_s 翻页间隔（0=不翻页）。 */
void pw_pend_bench(int on, int page_interval_s, int start_page);

#endif /* __APPS_EXAMPLES_PHYWEAR_PEND_H */
