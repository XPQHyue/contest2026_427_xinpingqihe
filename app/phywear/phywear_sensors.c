/****************************************************************************
 * apps/examples/phywear/phywear_sensors.c
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

#include <nuttx/config.h>

#include <sys/ioctl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <pthread.h>

#include <nuttx/sensors/lsm6dsl.h>
#include <nuttx/sensors/mmc5603.h>
#include <nuttx/sensors/ltr303.h>

#include "phywear_sensors.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static int g_imu_fd   = -1;
static int g_mag_fd   = -1;
static int g_light_fd = -1;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int pw_sensors_open(void)
{
  g_imu_fd = open("/dev/lsm6dsl0", O_RDONLY);
  if (g_imu_fd < 0)
    {
#ifdef CONFIG_EXAMPLES_PHYWEAR_SIM
      printf("WARN(sim): open /dev/lsm6dsl0 failed: %d - continue w/o IMU\n",
             errno);
#else
      printf("ERROR: open /dev/lsm6dsl0 failed: %d\n", errno);
      return -ENODEV;
#endif
    }

  /* 启动 IMU 连续转换（仅设备在位时） */

  if (g_imu_fd >= 0 &&
      ioctl(g_imu_fd, SNIOC_START, 0) < 0)
    {
      printf("WARN: IMU SNIOC_START failed: %d\n", errno);
    }

  g_mag_fd = open("/dev/mag0", O_RDONLY);
  if (g_mag_fd < 0)
    {
#ifdef CONFIG_EXAMPLES_PHYWEAR_SIM
      printf("WARN(sim): open /dev/mag0 failed: %d - continue w/o mag\n",
             errno);
#else
      printf("ERROR: open /dev/mag0 failed: %d\n", errno);
      return -ENODEV;
#endif
    }

  g_light_fd = open("/dev/light0", O_RDONLY);
  if (g_light_fd < 0)
    {
#ifdef CONFIG_EXAMPLES_PHYWEAR_SIM
      printf("WARN(sim): open /dev/light0 failed: %d - continue w/o light\n",
             errno);
#else
      printf("ERROR: open /dev/light0 failed: %d\n", errno);
      return -ENODEV;
#endif
    }

  return OK;
}

void pw_sensors_close(void)
{
  if (g_imu_fd >= 0)
    {
      close(g_imu_fd);
      g_imu_fd = -1;
    }

  if (g_mag_fd >= 0)
    {
      close(g_mag_fd);
      g_mag_fd = -1;
    }

  if (g_light_fd >= 0)
    {
      close(g_light_fd);
      g_light_fd = -1;
    }
}

int pw_sensors_read_imu(FAR struct pw_imu_s *out)
{
  struct lsm6dsl_sensor_data_s d;

  if (out == NULL || g_imu_fd < 0)
    {
      return -1;
    }

  if (ioctl(g_imu_fd, SNIOC_LSM6DSLSENSORREAD, (unsigned long)&d) < 0)
    {
      return -1;
    }

  out->ax = d.x_data;        /* mg */
  out->ay = d.y_data;
  out->az = d.z_data;
  out->gx = d.g_x_data;      /* mdps */
  out->gy = d.g_y_data;
  out->gz = d.g_z_data;

  return 0;
}

int pw_sensors_read_mag(FAR struct pw_mag_s *out)
{
  struct mmc5603_data_s d;

  if (out == NULL || g_mag_fd < 0)
    {
      return -1;
    }

  if (read(g_mag_fd, &d, sizeof(d)) != sizeof(d))
    {
      return -1;
    }

  out->x = (int)(d.x * MMC5603_MAG_SCALE_MG);   /* counts → mG */
  out->y = (int)(d.y * MMC5603_MAG_SCALE_MG);
  out->z = (int)(d.z * MMC5603_MAG_SCALE_MG);

  return 0;
}

int pw_sensors_read_light(FAR struct pw_light_s *out)
{
  struct ltr303_data_s d;

  if (out == NULL || g_light_fd < 0)
    {
      return -1;
    }

  if (read(g_light_fd, &d, sizeof(d)) != sizeof(d))
    {
      return -1;
    }

  /* CH0 含可见光 + 红外；减掉 CH1（红外）后才是可见光分量。
   * 0.6 ≈ 无感光窗遮挡、典型荧光/白光下的经验系数（phase 1 沿用）。 */

  out->ch0 = d.ch0;
  out->ch1 = d.ch1;
  out->lux = (d.ch0 > d.ch1) ? (int)((d.ch0 - d.ch1) * 0.6f) : 0;

  return 0;
}

/****************************************************************************
 * Name: mic_thread_main / pw_mic_thread_start
 *
 * Description:
 *   麦克风后台采集线程：循环 read() PW_MIC_SAMPLES 个 16-bit 样本
 *   写入 buf 并置 *ready=1（read 阻塞 ~64ms，不进 LVGL 定时器以免卡 UI）。
 *   *run=0 时线程退出。buf 须为静态/长期存储（线程在屏幕删除后仍可能
 *   完成最后一次 read）。pw_mic_thread_start 返回 pthread_t（0=失败，
 *   失败时 *run 置 0 供调用方检测）。
 *
 ****************************************************************************/

struct pw_mic_ctx_s
{
  int16_t       *buf;
  volatile int  *run;
  volatile int  *ready;
};

static void *mic_thread_main(void *arg)
{
  struct pw_mic_ctx_s *c = arg;
  int fd;

  fd = open("/dev/mic0", O_RDONLY);
  if (fd < 0)
    {
      printf("ERROR: open /dev/mic0 failed: %d\n", errno);
      *c->run = 0;
      free(c);
      return NULL;
    }

  while (*c->run)
    {
      ssize_t r = read(fd, c->buf, PW_MIC_SAMPLES * 2);

      if (r == PW_MIC_SAMPLES * 2)
        {
          *c->ready = 1;
        }
      else
        {
          usleep(10 * 1000);
        }
    }

  close(fd);
  free(c);
  return NULL;
}

pthread_t pw_mic_thread_start(int16_t *buf, volatile int *run,
                              volatile int *ready)
{
  struct pw_mic_ctx_s *c;
  pthread_t tid;

  if (buf == NULL || run == NULL || ready == NULL)
    {
      return 0;
    }

  c = malloc(sizeof(*c));
  if (c == NULL)
    {
      *run = 0;
      return 0;
    }

  c->buf = buf;
  c->run = run;
  c->ready = ready;

  if (pthread_create(&tid, NULL, mic_thread_main, c) != 0)
    {
      free(c);
      *run = 0;
      return 0;
    }

  return tid;
}
