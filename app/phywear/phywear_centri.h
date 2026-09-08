/****************************************************************************
 * apps/examples/phywear/phywear_centri.h
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

#ifndef __APPS_EXAMPLES_PHYWEAR_CENTRI_H
#define __APPS_EXAMPLES_PHYWEAR_CENTRI_H

/* 向心加速度实验（T2 采集-分析模板，phyphox centripetal_acceleration 对齐）：
 * acc+gyr @2Hz 同步采样 → a_c=√(|a|²−g²)（去重力，g 单位）与 ω=|gyr|(rad/s)
 * → 存 (ω², a_c) 点对 → r = 过原点最小二乘斜率 Σ(a·x)/Σ(x²)。
 * 页 1 散点 + 拟合线同图（pw_graph 多序列）。 */

lv_obj_t *pw_centri_screen(void);

/* 性能 bench：注入合成转圈（r=0.15m 变频）并自动循环翻页。
 * on=1 开始，page_interval_s 翻页间隔（0=不翻页），start_page 起始页。 */
void pw_centri_bench(int on, int page_interval_s, int start_page);

#endif /* __APPS_EXAMPLES_PHYWEAR_CENTRI_H */
