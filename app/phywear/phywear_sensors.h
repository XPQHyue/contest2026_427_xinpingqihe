/****************************************************************************
 * apps/examples/phywear/phywear_sensors.h
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

#ifndef __APPS_EXAMPLES_PHYWEAR_SENSORS_H
#define __APPS_EXAMPLES_PHYWEAR_SENSORS_H

#include <pthread.h>

/****************************************************************************
 * 传感器访问层：打开 /dev 设备并统一换算为物理单位。
 *
 *   加速度   /dev/lsm6dsl0  ioctl 读出，单位 mg
 *            （LSM6DS3TR-C ±16g，0.488 mg/LSB —— 见 lsm6dsl.c）
 *   陀螺仪   /dev/lsm6dsl0  ioctl 读出，单位 mdps
 *            （FS 2000dps，70 mdps/LSB —— 见 lsm6dsl.c）
 *   地磁     /dev/mag0      read()，20 位 counts ×0.0625 = mG
 *   环境光   /dev/light0    read()，CH0=可见+红外 / CH1=红外
 *
 * 所有换算在读取时完成，UI 只消费物理单位整数。
 ****************************************************************************/

struct pw_imu_s
{
  int ax;               /* mg */
  int ay;               /* mg */
  int az;               /* mg */
  int gx;               /* mdps */
  int gy;               /* mdps */
  int gz;               /* mdps */
};

struct pw_mag_s
{
  int x;                /* mG */
  int y;                /* mG */
  int z;                /* mG */
};

struct pw_light_s
{
  int lux;              /* 估算照度 lux（CH0-CH1 可见光分量） */
  int ch0;              /* 全光谱 ADC 计数（可见 + 红外） */
  int ch1;              /* 红外 ADC 计数 */
};

/* 麦克风：每次 read() 返回 1024 个 16-bit 单声道样本 @16kHz */

#define PW_MIC_SAMPLES  1024
#define PW_MIC_RATE     16000

/* 麦克风后台采集线程（声学秒表/掌声计/频谱共用）：
 * 循环 read() PW_MIC_SAMPLES 样本写入 buf 并置 *ready=1；
 * *run=0 时线程退出。返回 pthread_t（0=创建失败）。 */
pthread_t pw_mic_thread_start(int16_t *buf, volatile int *run,
                              volatile int *ready);

/* 打开三个传感器设备（含 IMU 启动连续转换）。
 * 返回 0 成功；负值为失败 errno。任何一路失败返回错误。 */

int pw_sensors_open(void);

/* 关闭全部设备（退出/复位用）。 */

void pw_sensors_close(void);

/* 读 IMU：成功返回 0，失败返回 -1（设备未开/读失败）。 */

int pw_sensors_read_imu(FAR struct pw_imu_s *out);

/* 读地磁（mG 整数）：成功返回 0。 */

int pw_sensors_read_mag(FAR struct pw_mag_s *out);

/* 读环境光：成功返回 0。 */

int pw_sensors_read_light(FAR struct pw_light_s *out);

#endif /* __APPS_EXAMPLES_PHYWEAR_SENSORS_H */
