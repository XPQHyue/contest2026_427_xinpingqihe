/****************************************************************************
 * apps/examples/phywear/phywear_raw.h
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

#ifndef __APPS_EXAMPLES_PHYWEAR_RAW_H
#define __APPS_EXAMPLES_PHYWEAR_RAW_H

/* 原始传感器板块（phyphox Raw Sensors 对齐）：4 页横滑大数值实时页。
 *   Accelerometer (g) / Gyroscope (dps) / Magnetometer (mG) / Light (lux)
 * 屏幕对象构建好后由 pw_scr_open() 压栈显示；返回箭头弹回上一屏。 */

lv_obj_t *pw_raw_screen(void);

#endif /* __APPS_EXAMPLES_PHYWEAR_RAW_H */
