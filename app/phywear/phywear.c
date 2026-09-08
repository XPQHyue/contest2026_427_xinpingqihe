/****************************************************************************
 * apps/examples/phywear/phywear.c
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

/* PhyWear 腕上智慧物理工坊 —— LVGL GUI 入口。
 *
 *   phywear                  启动 GUI（主菜单宫格 → 板块导航）
 *   phywear pendulum [L]     摆测 g 实验（串口文本输出，不经 GUI）
 *
 * GUI 模块划分（对应 UI 里程碑）：
 *   phywear_ui.c      屏幕管理器 + 主菜单 + 实验列表（UI-1）
 *   phywear_raw.c     原始传感器 4 页横滑（UI-2）
 *   phywear_sensors.c 传感器 /dev 访问层（物理单位换算）
 */

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <sys/ioctl.h>
#include <sys/mman.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <math.h>
#include <time.h>
#include <unistd.h>

#include <lvgl/lvgl.h>

#include <nuttx/sensors/lsm6dsl.h>
#include <nuttx/sensors/mmc5603.h>

#include "pw_analysis.h"
#include "phywear_sensors.h"
#include "phywear_ui.h"
#include "phywear_pend.h"
#include "phywear_spec.h"
#include "phywear_spring.h"
#include "phywear_centri.h"
#include "phywear_incline.h"
#include "phywear_ruler.h"
#include "phywear_time.h"
#include "phywear_life.h"
#include "phywear_i18n.h"
#include "phywear_raw.h"

#include <nuttx/video/fb.h>

/****************************************************************************
 * Private Data: FPS 探针（替换 LVGL 显示 flush，统计每秒实际渲染帧数）
 ****************************************************************************/

static int             g_fps_fd = -1;
static unsigned char  *g_fps_mem;
static size_t          g_fps_frame;     /* 单帧字节数 */
static volatile uint32_t g_fps_cnt;

static void pw_fps_flush(lv_display_t *disp, const lv_area_t *area,
                         uint8_t *px)
{
  /* 把绘制缓冲写回 fb 基址（单帧），恢复显示；并计帧 */
  if (g_fps_mem != NULL && px != NULL && g_fps_frame > 0)
    {
      memcpy(g_fps_mem, px, g_fps_frame);
    }

  g_fps_cnt++;
  lv_display_flush_ready(disp);
}

static void pw_fps_init(lv_display_t *disp)
{
  struct fb_videoinfo_s vi;
  struct fb_planeinfo_s pi;

  g_fps_fd = open("/dev/fb0", O_RDWR);
  if (g_fps_fd < 0)
    {
      printf("WARN: fps probe: open /dev/fb0 failed: %d\n", errno);
      return;
    }

  if (ioctl(g_fps_fd, FBIOGET_VIDEOINFO, (unsigned long)&vi) < 0 ||
      ioctl(g_fps_fd, FBIOGET_PLANEINFO, (unsigned long)&pi) < 0)
    {
      close(g_fps_fd);
      g_fps_fd = -1;
      return;
    }

  g_fps_frame = (size_t)vi.yres * pi.stride;   /* 单帧 = yres*stride */
  g_fps_mem  = mmap(NULL, pi.fblen, PROT_READ | PROT_WRITE,
                    MAP_SHARED | MAP_FILE, g_fps_fd, 0);
  if (g_fps_mem == MAP_FAILED)
    {
      g_fps_mem = NULL;
      g_fps_frame = 0;
      close(g_fps_fd);
      g_fps_fd = -1;
      return;
    }

  lv_display_set_flush_cb(disp, pw_fps_flush);
  printf("FPS probe: fb %ux%u frame=%u\n",
         vi.xres, vi.yres, (unsigned)g_fps_frame);
}

#ifndef M_PI
#  define M_PI 3.14159265358979323846
#endif

/****************************************************************************
 * Private Function Prototypes
 ****************************************************************************/

static int phywear_pendulum(int argc, FAR char *argv[]);
static int phywear_magread(int argc, FAR char *argv[]);
static int phywear_spring(int argc, FAR char *argv[]);
static int phywear_centripetal(int argc, FAR char *argv[]);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: phywear_pendulum
 *
 * Description:
 *   摆测 g 实验（phyphox pendulum 移植）：采集陀螺仪 ~50Hz 持续 PEND_SAMPLES
 *   点（约 10s），三轴求和 → 自相关测周期 T → g = 4π²L/T²。
 *   用法：phywear pendulum [摆长L米，默认0.5]
 *   结果打印到串口（纯文本，供实验阶段读数）。
 *
 ****************************************************************************/

#define PEND_SAMPLES   500     /* 10s @ 50Hz */
#define PEND_DT        0.02f   /* 采样间隔秒 */

static int phywear_pendulum(int argc, FAR char *argv[])
{
  struct lsm6dsl_sensor_data_s imu;
  FAR float *gyr;
  float length = 0.5f;
  int fd;
  int i;
  float T;
  float f;
  float g;

  if (argc > 2)
    {
      length = strtof(argv[2], NULL);
    }

  if (length <= 0.01f)
    {
      printf("ERROR: 摆长 L 无效（需 >0.01m）\n");
      return -EINVAL;
    }

  gyr = malloc(PEND_SAMPLES * sizeof(float));
  if (gyr == NULL)
    {
      printf("ERROR: 内存不足\n");
      return -ENOMEM;
    }

  fd = open("/dev/lsm6dsl0", O_RDONLY);
  if (fd < 0)
    {
      printf("ERROR: 打开 /dev/lsm6dsl0 失败\n");
      free(gyr);
      return -ENODEV;
    }

  ioctl(fd, SNIOC_START, 0);

  printf("摆测g：采样 %d 点 @ ~%dHz（%.1fs）……请让板子作单摆摆动\n",
         PEND_SAMPLES, (int)(1.0f / PEND_DT), PEND_SAMPLES * PEND_DT);
  fflush(stdout);

  for (i = 0; i < PEND_SAMPLES; i++)
    {
      if (ioctl(fd, SNIOC_LSM6DSLSENSORREAD, (unsigned long)&imu) == 0)
        {
          /* 三轴陀螺求和（phyphox anyGyr 思路：保留符号，单轴摆动即可） */

          gyr[i] = (float)imu.g_x_data + imu.g_y_data + imu.g_z_data;
        }
      else
        {
          gyr[i] = 0.0f;
        }

      usleep((useconds_t)(PEND_DT * 1000000.0f));   /* ~20ms */
    }

  close(fd);

  /* 自相关测周期 */

  T = pw_signal_period(gyr, PEND_SAMPLES, PEND_DT);
  free(gyr);

  if (T <= 0.0f)
    {
      printf("摆测g：未检测到稳定周期（摆动幅度不够或采集异常）\n");
      return 0;
    }

  f = 1.0f / T;
  g = 4.0f * (float)M_PI * (float)M_PI * length / (T * T);

  printf("摆测g结果：周期 T=%.4fs  频率 f=%.3fHz\n", T, f);
  printf("  L=%.3fm → g = 4π²L/T² = %.3f m/s²（理论 9.81）\n", length, g);
  return 0;
}

/****************************************************************************
 * Name: phywear_magread
 *
 * Description:
 *   连续打印地磁原始读数（mG + |B|），用法：phywear magread [次数，默认20]
 *   供磁偏置验证用：无斩波、走正常连续测量路径。
 *
 ****************************************************************************/

#define MAGREAD_SCALE  0.0625f   /* counts → mG */

static int phywear_magread(int argc, FAR char *argv[])
{
  struct mmc5603_data_s d;
  int n = 20;
  int fd;
  int i;

  if (argc > 2)
    {
      n = atoi(argv[2]);
    }

  fd = open("/dev/mag0", O_RDONLY);
  if (fd < 0)
    {
      printf("ERROR: 打开 /dev/mag0 失败\n");
      return -ENODEV;
    }

  for (i = 0; i < n; i++)
    {
      float x;
      float y;
      float z;
      float mag;

      if (read(fd, &d, sizeof(d)) != sizeof(d))
        {
          printf("read fail\n");
          break;
        }

      x = d.x * MAGREAD_SCALE;
      y = d.y * MAGREAD_SCALE;
      z = d.z * MAGREAD_SCALE;
      mag = sqrtf(x * x + y * y + z * z);

      printf("%3d X%+8.1f Y%+8.1f Z%+8.1f |B|%6.0f mG\n",
             i, x, y, z, mag);
      usleep(30 * 1000);
    }

  close(fd);
  return 0;
}

/****************************************************************************
 * Name: phywear_spring
 *
 * Description:
 *   弹簧振子实验（phyphox spring 移植，串口文本版，供物理实测）：
 *   采集加速度矢量模 |a| @~50Hz 持续 N 秒 → EMA 去重力 → 自相关测周期
 *   T → f=1/T。用法：phywear spring [秒，默认10]
 *
 ****************************************************************************/

#define SPRING_DT        0.02f    /* 采样间隔秒 */

static int phywear_spring(int argc, FAR char *argv[])
{
  struct lsm6dsl_sensor_data_s imu;
  FAR float *buf;
  float ema = 0.0f;
  float secs = 10.0f;
  int nsamples;
  int fd;
  int i;
  float T;
  float f;

  if (argc > 2)
    {
      secs = strtof(argv[2], NULL);
    }

  if (secs < 2.0f)
    {
      secs = 2.0f;
    }

  if (secs > 60.0f)
    {
      secs = 60.0f;
    }

  nsamples = (int)(secs / SPRING_DT);

  buf = malloc(nsamples * sizeof(float));
  if (buf == NULL)
    {
      printf("ERROR: 内存不足\n");
      return -ENOMEM;
    }

  fd = open("/dev/lsm6dsl0", O_RDONLY);
  if (fd < 0)
    {
      printf("ERROR: 打开 /dev/lsm6dsl0 失败\n");
      free(buf);
      return -ENODEV;
    }

  ioctl(fd, SNIOC_START, 0);

  printf("弹簧振子：采样 %d 点 @ ~%dHz（%.1fs）……请让重物竖直振动\n",
         nsamples, (int)(1.0f / SPRING_DT), secs);
  fflush(stdout);

  for (i = 0; i < nsamples; i++)
    {
      float m;

      if (ioctl(fd, SNIOC_LSM6DSLSENSORREAD, (unsigned long)&imu) == 0)
        {
          m = sqrtf((float)imu.x_data * imu.x_data +
                    (float)imu.y_data * imu.y_data +
                    (float)imu.z_data * imu.z_data) / 1000.0f;
        }
      else
        {
          m = 0.0f;
        }

      /* |a| 单位 g；EMA 去重力直流 → 振荡分量 */

      ema = ema * 0.99f + m * 0.01f;
      buf[i] = m - ema;
      usleep((useconds_t)(SPRING_DT * 1000000.0f));
    }

  close(fd);

  T = pw_signal_period(buf, nsamples, SPRING_DT);
  free(buf);

  if (T <= 0.0f)
    {
      printf("弹簧振子：未检测到稳定周期（振动幅度不够或采集异常）\n");
      return 0;
    }

  f = 1.0f / T;
  printf("弹簧振子结果：周期 T=%.4fs  频率 f=%.3fHz\n", (double)T,
         (double)f);
  printf("  公式：f = 1/T（自相关法，phyphox 同款，非 FFT）\n");
  return 0;
}

/****************************************************************************
 * Name: phywear_centripetal
 *
 * Description:
 *   向心加速度实验（phyphox centripetal 移植，串口文本版）：
 *   acc+gyr @2Hz 同步采集 N 秒 → (ω², a_c) 点对（a_c=√(|a|²−g²) 去重力）
 *   → r = 过原点最小二乘斜率。用法：phywear centripetal [秒，默认20]
 *
 ****************************************************************************/

#define CENTRI_DT        0.5f     /* 采样间隔秒（2Hz） */

static int phywear_centripetal(int argc, FAR char *argv[])
{
  struct lsm6dsl_sensor_data_s imu;
  float secs = 20.0f;
  float num = 0.0f;
  float den = 0.0f;
  int npoints = 0;
  int nsamples;
  int fd;
  int i;

  if (argc > 2)
    {
      secs = strtof(argv[2], NULL);
    }

  if (secs < 3.0f)
    {
      secs = 3.0f;
    }

  if (secs > 120.0f)
    {
      secs = 120.0f;
    }

  nsamples = (int)(secs / CENTRI_DT);

  fd = open("/dev/lsm6dsl0", O_RDONLY);
  if (fd < 0)
    {
      printf("ERROR: 打开 /dev/lsm6dsl0 失败\n");
      return -ENODEV;
    }

  ioctl(fd, SNIOC_START, 0);

  printf("向心加速度：采样 %d 点 @2Hz（%.1fs）……请让手表绕竖直轴转圈\n",
         nsamples, secs);
  printf("（转盘或手腕画圈，边转边改变转速；轴保持竖直）\n");
  fflush(stdout);

  for (i = 0; i < nsamples; i++)
    {
      float am;
      float wr;
      float ac;

      if (ioctl(fd, SNIOC_LSM6DSLSENSORREAD, (unsigned long)&imu) != 0)
        {
          continue;
        }

      /* |a| mg；|gyr| rad/s（mdps → π/180/1000） */

      am = sqrtf((float)imu.x_data * imu.x_data +
                 (float)imu.y_data * imu.y_data +
                 (float)imu.z_data * imu.z_data);
      wr = sqrtf((float)imu.g_x_data * imu.g_x_data +
                 (float)imu.g_y_data * imu.g_y_data +
                 (float)imu.g_z_data * imu.g_z_data) *
           (float)M_PI / 180.0f / 1000.0f;

      if (wr < 0.3f)
        {
          usleep((useconds_t)(CENTRI_DT * 1000000.0f));
          continue;
        }

      ac = sqrtf(am * am - 1000.0f * 1000.0f);
      if (!(ac > 50.0f))
        {
          usleep((useconds_t)(CENTRI_DT * 1000000.0f));
          continue;
        }

      /* a_c m/s²：mg → g → m/s² */

      ac = ac / 1000.0f * 9.81f;

      {
        float x = wr * wr;

        num += ac * x;
        den += x * x;
        npoints++;
        printf("  pt %3d: w=%5.2f rad/s  a=%6.3f m/s^2  r=%6.3f m\n",
               npoints, (double)wr, (double)ac,
               den > 0.0f ? (double)(num / den) : 0.0);
      }

      usleep((useconds_t)(CENTRI_DT * 1000000.0f));
    }

  close(fd);

  if (npoints < 3 || den <= 0.0f)
    {
      printf("向心加速度：有效点不足（%d），请转得更快/更稳\n", npoints);
      return 0;
    }

  printf("向心加速度结果：r = Σ(a·ω²)/Σ(ω⁴) = %.4f m（%d 点）\n",
         (double)(num / den), npoints);
  printf("  公式：a = r·ω²，过原点最小二乘斜率\n");
  return 0;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int main(int argc, FAR char *argv[])
{
  lv_nuttx_dsc_t info;
  lv_nuttx_result_t result;
  int bench_interval = 0;
  int bench_page = 0;
  bool bench_mode = false;
  bool spec_bench_mode = false;
  bool spec_bench_mic = false;
  bool spec_bench_mag = false;
  bool spring_bench_mode = false;
  bool centri_bench_mode = false;
  bool incline_bench_mode = false;
  bool ruler_bench_mode = false;
  bool time_bench_mode = false;
  bool life_bench_mode = false;
  int  time_bench_kind = 0;   /* 0=motion 1=light 2=acoustic */
  bool open_raw = false;      /* 调试：直接打开 Raw Sensors 页 */
  bool demo_mode = false;     /* 自动演示：模拟点击验证 / 录屏 */
  const char *cap_screen = NULL;  /* 截图专用：phywear cap <name> 打开指定屏并驻留 */

#ifdef CONFIG_EXAMPLES_PHYWEAR_SIM_ZH_DEMO
  /* 模拟器截图专用：强制中文界面（所有页面均按中文 UI 渲染）。
   * 仅仿真器/模拟器验收用，真机禁用此配置。
   * 注意：仅在“无子命令”启动（argc==1）时才强制进入 demo 自导航；
   * 若带子命令（raw/bench/spring/...），必须尊重其参数，以便逐页截图。 */
  pw_i18n_set_zh(true);
  if (argc == 1)
    {
      demo_mode = true;
    }
#endif

  /* phywear lang en|zh → 预设 GUI 语言（i18n，默认英文），其余参数按正常
   * GUI 处理。供模拟器/无 UI 入口期验收用；设备端语言 UI 入口待接。 */
  if (argc >= 3 && strcmp(argv[1], "lang") == 0)
    {
      pw_i18n_set_zh(strcmp(argv[2], "zh") == 0);
      argv += 2;
      argc -= 2;
    }

  /* 调试/模拟器视觉验证：phywear raw → 直接打开 Raw Sensors 页（免触摸） */
  if (argc > 1 && strcmp(argv[1], "raw") == 0)
    {
      open_raw = true;
      argc = 1;
    }

  /* 模拟器截图专用：phywear cap <name> → 打开指定主屏/实验页并驻留，
   * 便于逐页截取中文 UI（不注入数据、不自动翻页）。 */
  if (argc > 2 && strcmp(argv[1], "cap") == 0)
    {
      cap_screen = argv[2];
      argc = 1;
    }

  /* 自动演示：phywear demo → 自动点击穿过菜单/板块/实验页（验证/录屏用） */
  if (argc > 1 && strcmp(argv[1], "demo") == 0)
    {
      demo_mode = true;
      argc = 1;
    }

  /* phywear pendbench [秒 [起始页]] → 性能测试：合成摆动 + 自动翻页
   * （秒=0 表示不自动翻页但保持 bench 注入与统计输出） */
  if (argc > 1 && strcmp(argv[1], "pendbench") == 0)
    {
      bench_mode = true;
      bench_interval = (argc > 2) ? atoi(argv[2]) : 12;
      bench_page = (argc > 3) ? atoi(argv[3]) : 0;
    }

  /* 频谱 bench：specbench=[加速度频谱] / micspecbench=[麦克风频谱] /
   * magspecbench=[地磁频谱]，合成多音注入 + 自动翻页 + perf 统计 */
  if (argc > 1 &&
      (strcmp(argv[1], "specbench") == 0 ||
       strcmp(argv[1], "micspecbench") == 0 ||
       strcmp(argv[1], "magspecbench") == 0))
    {
      spec_bench_mic = (strncmp(argv[1], "mic", 3) == 0);
      spec_bench_mag = (strncmp(argv[1], "mag", 3) == 0);
      spec_bench_mode = true;
      bench_mode = true;
      bench_interval = (argc > 2) ? atoi(argv[2]) : 12;
      bench_page = (argc > 3) ? atoi(argv[3]) : 0;
    }

  /* 弹簧 bench：springbench [秒 [起始页]] → 合成 2.0Hz 振荡 + 自动翻页 */

  if (argc > 1 && strcmp(argv[1], "springbench") == 0)
    {
      spring_bench_mode = true;
      bench_mode = true;
      bench_interval = (argc > 2) ? atoi(argv[2]) : 12;
      bench_page = (argc > 3) ? atoi(argv[3]) : 0;
    }

  /* 向心 bench：centribench [秒 [起始页]] → 合成转圈（r=0.15m） */

  if (argc > 1 && strcmp(argv[1], "centribench") == 0)
    {
      centri_bench_mode = true;
      bench_mode = true;
      bench_interval = (argc > 2) ? atoi(argv[2]) : 12;
      bench_page = (argc > 3) ? atoi(argv[3]) : 0;
    }

  /* 工具/计时器/生活 bench：
   *   inclinebench / rulerbench / applausebench [秒 [起始页]]
   *   timebench motion|light|acoustic [秒 [起始页]] */
  if (argc > 1 && strcmp(argv[1], "inclinebench") == 0)
    {
      incline_bench_mode = true;
      bench_mode = true;
      bench_interval = (argc > 2) ? atoi(argv[2]) : 12;
      bench_page = (argc > 3) ? atoi(argv[3]) : 0;
    }

  if (argc > 1 && strcmp(argv[1], "rulerbench") == 0)
    {
      ruler_bench_mode = true;
      bench_mode = true;
      bench_interval = (argc > 2) ? atoi(argv[2]) : 12;
      bench_page = (argc > 3) ? atoi(argv[3]) : 0;
    }

  if (argc > 1 && strcmp(argv[1], "timebench") == 0)
    {
      time_bench_mode = true;
      bench_mode = true;
      time_bench_kind = (argc > 2 && strcmp(argv[2], "light") == 0) ? 1 :
                        (argc > 2 && strcmp(argv[2], "acoustic") == 0) ? 2 : 0;
      bench_interval = (argc > 3) ? atoi(argv[3]) : 12;
      bench_page = (argc > 4) ? atoi(argv[4]) : 0;
    }

  if (argc > 1 && strcmp(argv[1], "applausebench") == 0)
    {
      life_bench_mode = true;
      bench_mode = true;
      bench_interval = (argc > 2) ? atoi(argv[2]) : 12;
      bench_page = (argc > 3) ? atoi(argv[3]) : 0;
    }

  /* 子命令：phywear pendulum [L] → 摆测 g 实验 */

  if (argc > 1 && strcmp(argv[1], "pendulum") == 0)
    {
      return phywear_pendulum(argc, argv);
    }

  /* 子命令：phywear spring [秒] → 弹簧振子实验（串口文本输出） */

  if (argc > 1 && strcmp(argv[1], "spring") == 0)
    {
      return phywear_spring(argc, argv);
    }

  /* 子命令：phywear centripetal [秒] → 向心加速度实验（串口文本输出） */

  if (argc > 1 && strcmp(argv[1], "centripetal") == 0)
    {
      return phywear_centripetal(argc, argv);
    }

  /* 子命令：phywear magread [次数,默认20] → 连续读地磁原始读数（mG） */

  if (argc > 1 && strcmp(argv[1], "magread") == 0)
    {
      return phywear_magread(argc, argv);
    }

  /* 打开传感器（失败即退出，避免 UI 无数据空转；模拟器模式仅告警继续） */

  if (pw_sensors_open() < 0)
    {
#ifndef CONFIG_EXAMPLES_PHYWEAR_SIM
      printf("ERROR: 传感器打开失败\n");
      return EXIT_FAILURE;
#else
      printf("WARN(sim): 传感器不可用 - 模拟器模式继续（无实时数据）\n");
#endif
    }

  /* 初始化 LVGL */

  lv_init();

  lv_nuttx_dsc_init(&info);

#ifdef CONFIG_LV_USE_NUTTX_LCD
  info.fb_path = "/dev/lcd0";
#endif

  lv_nuttx_init(&info, &result);
  if (result.disp == NULL)
    {
      printf("ERROR: LVGL 显示初始化失败\n");
      return EXIT_FAILURE;
    }

  /* FPS 探针：替换 flush 统计实际渲染帧率（性能验证用） */
  pw_fps_init(result.disp);

  /* 主菜单根屏（板块宫格）；各板块按需构建子屏 */

  if (cap_screen)
    {
      /* 截图专用：打开指定屏（无注入、不自动翻页） */
      lv_obj_t *scr = NULL;

      if      (strcmp(cap_screen, "root")    == 0) pw_ui_root();
      else if (strcmp(cap_screen, "raw")     == 0) scr = pw_raw_screen();
      else if (strcmp(cap_screen, "pendulum")== 0) scr = pw_pendulum_screen();
      else if (strcmp(cap_screen, "spring")  == 0) scr = pw_spring_screen();
      else if (strcmp(cap_screen, "centri")  == 0) scr = pw_centri_screen();
      else if (strcmp(cap_screen, "incline") == 0) scr = pw_incline_screen();
      else if (strcmp(cap_screen, "ruler")   == 0) scr = pw_ruler_screen();
      else if (strcmp(cap_screen, "spec_accel")== 0) scr = pw_spec_accel_screen();
      else if (strcmp(cap_screen, "spec_mic")== 0) scr = pw_spec_mic_screen();
      else if (strcmp(cap_screen, "spec_mag")== 0) scr = pw_spec_mag_screen();
      else if (strcmp(cap_screen, "stopwatch")== 0) scr = pw_motion_stopwatch_screen();
      else if (strcmp(cap_screen, "lightgate")== 0) scr = pw_light_gate_screen();
      else if (strcmp(cap_screen, "acousticgate")== 0) scr = pw_acoustic_gate_screen();
      else if (strcmp(cap_screen, "applause")== 0) scr = pw_applause_screen();
      else if (strcmp(cap_screen, "settings")== 0) scr = pw_settings_screen();
      else if (strcmp(cap_screen, "about")   == 0) scr = pw_about_screen();
      else printf("WARN(cap): 未知屏 '%s'\n", cap_screen);

      if (scr != NULL)
        {
          pw_scr_open(scr);
        }
    }
  else if (open_raw)
    {
      pw_scr_open(pw_raw_screen());
    }
  else
    {
      pw_ui_root();
    }

  /* 自动演示：开始自导航（前沿根屏运行，lv_timer 逐步打开/返回） */
  if (demo_mode)
    {
      pw_ui_demo_start();
    }

  /* bench：自动打开目标实验页并注入合成信号。
   * pendbench → Pendulum；specbench/micspecbench → 频谱页。 */

  if (bench_mode)
    {
      lv_obj_t *scr;

      /* 先开 bench 注入再建屏（频谱页据 g_bench_on 决定是否起 mic 线程） */
      if (spec_bench_mode)
        {
          pw_spec_bench(1, bench_interval, bench_page);
          scr = spec_bench_mic ? pw_spec_mic_screen() :
                spec_bench_mag ? pw_spec_mag_screen() :
                                 pw_spec_accel_screen();
        }
      else if (spring_bench_mode)
        {
          scr = pw_spring_screen();
          pw_spring_bench(1, bench_interval, bench_page);
        }
      else if (centri_bench_mode)
        {
          scr = pw_centri_screen();
          pw_centri_bench(1, bench_interval, bench_page);
        }
      else if (incline_bench_mode)
        {
          scr = pw_incline_screen();
          pw_incline_bench(1, bench_interval, bench_page);
        }
      else if (ruler_bench_mode)
        {
          scr = pw_ruler_screen();
          pw_ruler_bench(1, bench_interval, bench_page);
        }
      else if (time_bench_mode)
        {
          pw_time_bench(1, bench_interval, bench_page);
          scr = (time_bench_kind == 1) ? pw_light_gate_screen() :
                (time_bench_kind == 2) ? pw_acoustic_gate_screen() :
                                         pw_motion_stopwatch_screen();
        }
      else if (life_bench_mode)
        {
          pw_applause_bench(1, bench_interval, bench_page);
          scr = pw_applause_screen();
        }
      else
        {
          scr = pw_pendulum_screen();
          pw_pend_bench(1, bench_interval, bench_page);
        }

      pw_scr_open(scr);
    }

  /* 主循环：驱动 LVGL。pendbench 模式附带每 2s 帧节奏/空闲堆统计。 */

  {
    struct timespec tmark;
    struct timespec dmark;
    unsigned long loop = 0;

    clock_gettime(CLOCK_MONOTONIC, &tmark);
    clock_gettime(CLOCK_MONOTONIC, &dmark);

    while (1)
      {
        struct timespec tn;
        struct timespec dn;

        lv_timer_handler();
        loop++;

        /* demo：按时间逐步自导航（每 ~2.3s 推进一步），播完自动退出 */
        if (demo_mode)
          {
            clock_gettime(CLOCK_MONOTONIC, &dn);
            if ((dn.tv_sec - dmark.tv_sec) >= 2)
              {
                pw_ui_demo_step();
                clock_gettime(CLOCK_MONOTONIC, &dmark);
              }
            if (pw_ui_demo_done())
              {
                break;
              }
          }

        clock_gettime(CLOCK_MONOTONIC, &tn);
        if (bench_mode && (tn.tv_sec - tmark.tv_sec) >= 2)
          {
            struct mallinfo mi = mallinfo();
            uint32_t fps = g_fps_cnt / 2u;

            printf("perf loops=%lu idleheap=%u fps=%u\n", loop,
                   (unsigned)(mi.fordblks > 0 ? mi.fordblks : 0),
                   (unsigned)fps);
            g_fps_cnt = 0;
            fflush(stdout);
            tmark = tn;
            loop = 0;
          }

        usleep(10 * 1000);   /* ~100Hz 轮询，界面动画/触摸流畅 */
      }
  }
  return EXIT_SUCCESS;
}
