/****************************************************************************
 * vendor/sifli/boards/sf32lb52/drivers/audio/sf32lb52_mic.h
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

#ifndef __BOARDS_SF32LB52_DRIVERS_AUDIO_SF32LB52_MIC_H
#define __BOARDS_SF32LB52_DRIVERS_AUDIO_SF32LB52_MIC_H

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

#ifdef __cplusplus
#define EXTERN extern "C"
extern "C"
{
#else
#define EXTERN extern
#endif

/****************************************************************************
 * Name: sf32lb52_mic_register
 *
 * Description:
 *   注册板载模拟 MEMS 麦克风字符设备（如 "/dev/mic0"）。
 *   每次 read() 返回 1024 个 16-bit 单声道采样（16kHz）。
 *
 * Input Parameters:
 *   devpath - 设备路径，例如 "/dev/mic0"
 *
 * Returned Value:
 *   成功返回 OK(0)，失败返回负的 errno。
 *
 ****************************************************************************/

int sf32lb52_mic_register(FAR const char *devpath);

#undef EXTERN
#ifdef __cplusplus
}
#endif

#endif /* __BOARDS_SF32LB52_DRIVERS_AUDIO_SF32LB52_MIC_H */
