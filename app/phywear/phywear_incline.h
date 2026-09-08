/****************************************************************************
 * apps/examples/phywear/phywear_incline.h
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

#ifndef __APPS_EXAMPLES_PHYWEAR_INCLINE_H
#define __APPS_EXAMPLES_PHYWEAR_INCLINE_H

/* 斜面倾角（Tools，phyphox inclination 对齐）：
 * 角 = atan2(ax, √(ay²+az²))（x 轴与水平面夹角，0=平放、±90=竖直）。 */

lv_obj_t *pw_incline_screen(void);

/* 性能 bench：注入合成倾角（45°±20° 摆动）并自动循环翻页。 */
void pw_incline_bench(int on, int page_interval_s, int start_page);

#endif /* __APPS_EXAMPLES_PHYWEAR_INCLINE_H */
