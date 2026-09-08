/****************************************************************************
 * apps/examples/phywear/phywear_life.h
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

#ifndef __APPS_EXAMPLES_PHYWEAR_LIFE_H
#define __APPS_EXAMPLES_PHYWEAR_LIFE_H

/* 掌声计（Everyday，phyphox applause_meter 对齐）：
 * 麦克风 RMS → dBFS 实时值 + 阈值计数（每次响亮事件=一次掌声）。 */

lv_obj_t *pw_applause_screen(void);

/* 性能 bench：注入合成掌声（每 ~2s 一次响亮块）并自动循环翻页。 */
void pw_applause_bench(int on, int page_interval_s, int start_page);

#endif /* __APPS_EXAMPLES_PHYWEAR_LIFE_H */
