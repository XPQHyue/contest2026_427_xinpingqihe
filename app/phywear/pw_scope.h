/****************************************************************************
 * apps/examples/phywear/pw_scope.h
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

#ifndef __APPS_EXAMPLES_PHYWEAR_SCOPE_H
#define __APPS_EXAMPLES_PHYWEAR_SCOPE_H

/* 自定义实时曲线控件：绕过 lv_chart 的软件画线路径（本板 EPIC 未加速
 * LINE，逐帧重绘奇贵），改为 CPU 光栅化到一块 RGB565 缓冲 → 以 lv_image
 * 呈现 → 交给 LVGL EPIC 的 IMAGE 任务硬件 blit（43fps 快速路）。
 *
 * 用法：
 *   lv_obj_t *s = pw_scope_create(parent, w, h, line_color, bg_color);
 *   lv_obj_set_pos(s, x, y);
 *   pw_scope_set_data(s, ybuf, n);   // ybuf[i] 归一化到 [-1, 1]
 * 刷新频率可高（数十 Hz），曲线顺滑且 CPU 占用低。
 */

lv_obj_t *pw_scope_create(lv_obj_t *parent, int w, int h,
                          lv_color_t line, lv_color_t bg);

void pw_scope_set_data(lv_obj_t *scope, FAR const float *y, int n);

/* 散点模式：绘制 n 个点，x[]/y[] 均归一化到 [-1,1]（共振图等用） */
void pw_scope_set_points(lv_obj_t *scope, FAR const float *x,
                         FAR const float *y, int n);

/* 绘制坐标轴（水平零线 + 左右/上下边框），color 为轴线颜色 */
void pw_scope_axes(lv_obj_t *scope, lv_color_t color);

#endif /* __APPS_EXAMPLES_PHYWEAR_SCOPE_H */
