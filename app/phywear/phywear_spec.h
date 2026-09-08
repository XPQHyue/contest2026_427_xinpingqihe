/****************************************************************************
 * apps/examples/phywear/phywear_spec.h
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

#ifndef __APPS_EXAMPLES_PHYWEAR_SPEC_H
#define __APPS_EXAMPLES_PHYWEAR_SPEC_H

/* 频谱实验页（T4 模板，UI-4 里程碑）——统一 FFT 引擎，两个实例：
 *
 *   pw_spec_accel_screen()  Tools → Accel spectrum
 *       加速度 3 轴 @50Hz，256 点窗（5.12s），df=0.195Hz，0..25Hz；
 *       pw_graph 多序列（X/Y/Z 三色叠加）+ 主频值卡 + 波形页 + 说明页。
 *   pw_spec_mic_screen()    Acoustics → Spectrum
 *       麦克风 @16kHz，1024 点窗（64ms），df=15.6Hz，0..8kHz；
 *       后台 pthread 连续采集（read 阻塞 64ms，不进 LVGL 定时器），
 *       UI 节流 ~5Hz 刷新；主频值卡 + 波形页 + 说明页。
 *   pw_spec_mag_screen()    Tools → Mag spectrum
 *       地磁 3 轴 @25Hz（每 2 拍 40ms），128 点窗（5.12s），
 *       df=0.195Hz，0..12.5Hz；波形=|B|−EMA 漂移。
 *
 * 分析链（phyphox 对齐）：去均值 → Hann 窗 → radix-2 FFT → 幅度谱，
 * 按全局最大幅度归一化（多序列共用 Y 0..1）；主频 = pw_fft_dominant_freq
 * （跳过直流 + 抛物线插值）。图表禁用 lv_chart，一律 pw_graph/pw_scope。
 */

lv_obj_t *pw_spec_accel_screen(void);
lv_obj_t *pw_spec_mic_screen(void);
lv_obj_t *pw_spec_mag_screen(void);

/* 性能 bench：注入合成多音信号并自动循环翻页（无需触摸/真人）。
 * on=1 开始，page_interval_s 翻页间隔（0=不翻页），start_page 起始页。 */
void pw_spec_bench(int on, int page_interval_s, int start_page);

#endif /* __APPS_EXAMPLES_PHYWEAR_SPEC_H */
