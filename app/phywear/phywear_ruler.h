/****************************************************************************
 * apps/examples/phywear/phywear_ruler.h
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

#ifndef __APPS_EXAMPLES_PHYWEAR_RULER_H
#define __APPS_EXAMPLES_PHYWEAR_RULER_H

/* 磁性标尺（Tools，phyphox magnetic_ruler 对齐）：
 * 地磁 |B| @25Hz → 滑动平均平滑 → 阈值+滞回峰检测计数（磁铁阵列扫过）。 */

lv_obj_t *pw_ruler_screen(void);

/* 性能 bench：注入合成磁场扫峰并自动循环翻页。 */
void pw_ruler_bench(int on, int page_interval_s, int start_page);

#endif /* __APPS_EXAMPLES_PHYWEAR_RULER_H */
