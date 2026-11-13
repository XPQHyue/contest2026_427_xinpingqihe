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
#include <nuttx/lcd/lcd_dev.h>
#include <string.h>
#include <errno.h>
#include <fcntl.h>
#include <malloc.h>
#include <math.h>
#include <time.h>
#include <unistd.h>
#include <syslog.h>

#ifdef CONFIG_EXAMPLES_PHYWEAR_AUTOSTART
#  include <nuttx/sched.h>
#endif


#include <lvgl/lvgl.h>

/* P0：主循环休眠上限（ms）。10 = 改动前的固定行为；1~2 = 低延迟模式。
 * 可回退开关：改回 10 即完全恢复原状。 */

/* P0 性能探针（2026-09-16）：按秒打印 fps、LVGL 渲染耗时均值/峰值、主循环次数。
 * 为什么需要：30 s 心跳的 fps 是**平均**，会把"图线 10 Hz 才重绘"的空闲时间平均进去，
 * 掩盖了小脏区连续更新时能达到的瞬时帧率（用户在轨迹页看到过 60 FPS）。
 * 设 0 可整体关掉（零开销）。 */

#ifndef PW_PERF_PROBE
#  define PW_PERF_PROBE 0   /* 默认关：置 1 打开每秒 [PERF] 行（fps/渲染耗时），优化时用 */
#endif

#ifndef PW_LOOP_SLEEP_MAX_MS
#  define PW_LOOP_SLEEP_MAX_MS 2   /* A/B 实测：0（忙等）与 2 ms 的 fps 完全相同（48~49），
                                     * 故取 2 ms 省 CPU；10 = 改动前的固定行为 */
#endif

/* 纯 GUI 模式下，开机后在**后台**把蓝牙 host 栈起起来（不阻塞界面）。
 * 置 0 可整体关掉（那时只有点开「蓝牙」页或跑 `phywear cap bt` 才会起栈）。 */
#ifndef PW_BT_AUTOSTART
#  define PW_BT_AUTOSTART 1
#endif

#include <nuttx/input/touchscreen.h>
#include <nuttx/sensors/lsm6dsl.h>
#include <nuttx/sensors/mmc5603.h>

#include "pw_analysis.h"
#include "phywear_sensors.h"
#include "phywear_ui.h"
#include "pw_bounce.h"
#include "pw_splash.h"
#include "pw_power.h"
#include "pw_btprobe.h"
#include "pw_bnep.h"
#if defined(__has_include)
#  if __has_include("pw_llm_default.h")
#    include "pw_llm_default.h"
#  endif
#endif


void cmd_set_llm(int argc, char **argv);

static void pw_llm_apply_builtin(void)
{
#if defined(PW_LLM_URL) && defined(PW_LLM_KEY)
  static int done;
  char *av[5];

  if (done)
    {
      return;
    }

  done = 1;
  av[0] = (char *)"set_llm";
  av[1] = (char *)PW_LLM_URL;
  av[2] = (char *)PW_LLM_MODEL;
  av[3] = (char *)PW_LLM_KEY;
  av[4] = NULL;
  cmd_set_llm(4, av);
  printf("[phywear] LLM backend\n");
#endif
}



int agent_cli_submit(const char *text);



#include "pw_net.h"
#include "pw_bt.h"
#include "pw_btgatt.h"
#include "pw_btppp.h"
#if defined(CONFIG_LV_USE_DEMO_BENCHMARK)
#  include <demos/benchmark/lv_demo_benchmark.h>
#endif
#include "pw_motion.h"
#include "pw_ahrs.h"
#include "pw_calib.h"
#include "phywear_imu.h"
#include "phywear_pend.h"
#include "phywear_spec.h"
#include "phywear_spring.h"
#include "phywear_centri.h"
#include "phywear_incline.h"
#include "phywear_ruler.h"
#include "phywear_time.h"
#include "phywear_life.h"
#include "phywear_i18n.h"
#include "pw_ai.h"
#include "pw_relay.h"
#include "pw_shot.h"
#include "pw_skill.h"
#include "pw_watch.h"
#include "pw_store.h"
#include "pw_settings.h"
#include "pw_tone.h"
#include "phywear_raw.h"

#include <nuttx/video/fb.h>

/****************************************************************************
 * Private Data: FPS 探针（替换 LVGL 显示 flush，统计每秒实际渲染帧数）
 ****************************************************************************/

static int             g_fps_fd = -1;
static unsigned char  *g_fps_mem;
static size_t          g_fps_frame;     /* 单帧字节数 */
static volatile uint32_t g_fps_cnt;

/* 见 phywear_ui.h：界面线程推帧计数（单调增），给 pw_bt.c 的取证用。 */
uint32_t pw_ui_flush_count(void)
{
  return g_fps_cnt;
}
static volatile uint32_t g_loop_next_ms;   /* LVGL 建议的下次休眠上限（ms） */

/* 渲染耗时统计常开：屏上帧率表要用它（原来只在 PW_PERF_PROBE 下编，
 * 每帧两次 clock_gettime，实测量级 < 5 us，不值得再开关一次）。 */

static volatile uint32_t g_perf_us_sum;   /* 渲染耗时累加（us） */
static volatile uint32_t g_perf_us_max;   /* 渲染耗时峰值（us） */
static volatile uint32_t g_perf_n;        /* 渲染帧数 */
static struct timespec   g_perf_t0;       /* 本轮渲染起点 */

/* [AGENT-VM] 屏上帧率表（T-0002）。用户反馈"感觉变卡"，需要当场能看见的
 * 数字，而不是等 30 s 心跳里的平均值。三个口径分开给，别混着看：
 *   fps   = 最近 1 s 真正重绘了多少帧（LVGL 只画脏区，静止时本来就很低）
 *   rnd   = 单帧渲染耗时（ms，含峰值）—— 卡不卡主要看这个和 loop 频率
 *   loop  = GUI 主循环每秒转了多少圈（LVGL 建议休眠时间决定）
 */

/* 默认开着：用户现在就要用眼睛验证"卡不卡"，而他没有终端敲命令。
 * 提交前改回 0（或用 `phywear fps off` 临时关掉）。 */
static volatile int g_fps_show = 0;

/* 长按电源键：按键线程只置请求，菜单必须由 GUI 线程创建 */

static volatile int g_power_req;   /* 默认关：仪表只在 `phywear fps on` 时出现 */
static lv_obj_t    *g_fps_lab;
static uint32_t     g_stat_win_ms;      /* 本统计窗口起点 */
static uint32_t     g_stat_win_frames;  /* 窗口起点时的帧计数 */
static uint32_t     g_stat_loops;       /* 窗口内循环次数 */
static uint32_t     g_stat_win_us;      /* 窗口内渲染耗时累加 */
static uint32_t     g_stat_win_n;       /* 窗口内渲染帧数 */
static uint32_t     g_stat_fps_x10;     /* 对外：fps*10 */
static uint32_t     g_stat_loop_rate;   /* 对外：loop/s */
static uint32_t     g_stat_rnd_us;      /* 对外：平均渲染耗时 */
static uint32_t     g_stat_rnd_max_us;  /* 对外：窗口内峰值 */

/* 触摸→下一帧 的延迟：这是"卡不卡"最直接的数。
 * 打点放在控件树收到 PRESSED 的那一刻，终点是下一次 RENDER_READY。 */

static volatile uint32_t g_touch_ms;
static volatile int      g_touch_pending;
static volatile uint32_t g_lat_ms;

void pw_perf_note_touch(void)
{
  g_touch_ms      = lv_tick_get();
  g_touch_pending = 1;
}


/* 计帧：挂 LVGL 显示事件，而不是替换 flush 回调。
 * lv_refr.c 只在"本轮真的有区域重绘"时才发 LV_EVENT_RENDER_READY，
 * 所以这就是这一页真正刷新了多少帧；且模拟器/真机都成立
 * （真机没有 /dev/fb0，原来那套 flush 探针在真机上装不上，fps 恒为 0）。 */

static void pw_fps_render_cb(lv_event_t *e)
{
  lv_event_code_t code = lv_event_get_code(e);

  if (code == LV_EVENT_RENDER_START)
    {
      struct timespec t0;

      clock_gettime(CLOCK_MONOTONIC, &t0);
      g_perf_t0 = t0;
      return;
    }

  if (code == LV_EVENT_RENDER_READY)
    {
      struct timespec t1;
      uint32_t us;

      clock_gettime(CLOCK_MONOTONIC, &t1);
      us = (uint32_t)((t1.tv_sec - g_perf_t0.tv_sec) * 1000000L +
                      (t1.tv_nsec - g_perf_t0.tv_nsec) / 1000L);

      g_perf_us_sum += us;
      g_perf_n++;
      g_stat_win_us += us;
      g_stat_win_n++;
      if (us > g_perf_us_max)
        {
          g_perf_us_max = us;
        }

      if (g_touch_pending)
        {
          g_lat_ms        = lv_tick_get() - g_touch_ms;
          g_touch_pending = 0;
        }

      g_fps_cnt++;
    }
}

static void pw_fps_flush(lv_display_t *disp, const lv_area_t *area,
                         uint8_t *px)
{
  /* 把绘制缓冲写回 fb 基址（单帧），恢复显示。
   * 计帧不在这里做，交给 pw_fps_render_cb（避免双计）。 */
  if (g_fps_mem != NULL && px != NULL && g_fps_frame > 0)
    {
      memcpy(g_fps_mem, px, g_fps_frame);
    }

  lv_display_flush_ready(disp);
}

/****************************************************************************
 * Name: pw_fps_overlay_*
 *
 * Description:
 *   屏上帧率表：`phywear fps on` 打开，右上角浮一小块，每秒刷新。
 *   放 lv_layer_top() 上，换页不会被删；截图里也会带上（取证时正好）。
 ****************************************************************************/

static void pw_fps_overlay_make(void)
{
  if (g_fps_lab != NULL)
    {
      return;
    }

  g_fps_lab = lv_label_create(lv_layer_top());
  lv_obj_set_style_bg_color(g_fps_lab, lv_color_black(), 0);
  lv_obj_set_style_bg_opa(g_fps_lab, LV_OPA_70, 0);
  lv_obj_set_style_pad_hor(g_fps_lab, 6, 0);
  lv_obj_set_style_pad_ver(g_fps_lab, 2, 0);
  lv_obj_set_style_radius(g_fps_lab, 4, 0);
  lv_obj_set_style_text_font(g_fps_lab, PW_FNT_SMALL, 0);
  lv_obj_set_style_text_color(g_fps_lab, lv_color_white(), 0);
  lv_label_set_text(g_fps_lab, "FPS --\n--");
  lv_obj_align(g_fps_lab, LV_ALIGN_TOP_LEFT, 6, 96);
}

static void pw_fps_overlay_paint(void)
{
  if (!g_fps_show)
    {
      return;
    }

  pw_fps_overlay_make();

  lv_label_set_text_fmt(g_fps_lab,
                        "FPS %u.%u  RND %ums/%ums\nLOOP %u/s  TOUCH %ums",
                        (unsigned)(g_stat_fps_x10 / 10),
                        (unsigned)(g_stat_fps_x10 % 10),
                        (unsigned)(g_stat_rnd_us / 1000),
                        (unsigned)(g_stat_rnd_max_us / 1000),
                        (unsigned)g_stat_loop_rate,
                        (unsigned)g_lat_ms);
}

/* 命令跑在第二个 phywear 进程里，绝不能碰 LVGL（会撞运行中的显示驱动）。
 * 这里只翻一个共享标志，真正的建控件/画字由 GUI 线程的 tick 完成。 */

void pw_fps_show(int on)
{
  g_fps_show = on ? 1 : 0;
}

int pw_fps_is_shown(void)
{
  return g_fps_show;
}

/* 配对码显示（跨线程）：
 * BT 线程拿到 passkey 只能写共享变量，建控件/画字必须回到 GUI 线程做。 */

static volatile unsigned int g_pin_code;
static volatile int g_pin_seq;
static lv_obj_t *g_pin_lab;
static int g_pin_shown_seq;
static uint32_t g_pin_deadline_ms;

void pw_ui_show_passkey(unsigned int code)
{
  g_pin_code = code;
  g_pin_seq++;
}

static void pw_pin_sync(void)
{
  if (g_pin_seq != g_pin_shown_seq)
    {
      g_pin_shown_seq = g_pin_seq;

      if (g_pin_lab == NULL)
        {
          g_pin_lab = lv_label_create(lv_layer_top());
          lv_obj_set_style_bg_color(g_pin_lab, lv_color_black(), 0);
          lv_obj_set_style_bg_opa(g_pin_lab, LV_OPA_90, 0);
          lv_obj_set_style_radius(g_pin_lab, 10, 0);
          lv_obj_set_style_pad_all(g_pin_lab, 14, 0);
          lv_obj_set_style_text_color(g_pin_lab, lv_color_white(), 0);
          lv_obj_set_style_text_align(g_pin_lab, LV_TEXT_ALIGN_CENTER, 0);
          lv_obj_align(g_pin_lab, LV_ALIGN_CENTER, 0, 0);
        }

      lv_label_set_text_fmt(g_pin_lab, "手机蓝牙配对码\n%03u %03u",
                            (g_pin_code / 1000) % 1000, g_pin_code % 1000);
      lv_obj_set_style_text_font(g_pin_lab,
                                 g_pin_code > 999 ? PW_FNT_XL : PW_FNT_XL, 0);
      lv_obj_remove_flag(g_pin_lab, LV_OBJ_FLAG_HIDDEN);
      lv_obj_move_foreground(g_pin_lab);
      g_pin_deadline_ms = lv_tick_get() + 60000;
    }

  if (g_pin_lab != NULL &&
      !lv_obj_has_flag(g_pin_lab, LV_OBJ_FLAG_HIDDEN) &&
      lv_tick_get() > g_pin_deadline_ms)
    {
      lv_obj_add_flag(g_pin_lab, LV_OBJ_FLAG_HIDDEN);
    }
}


/****************************************************************************
 * 设置落盘（掉电保留）
 *
 * pw_store 只认一个 24 字节的结构；这里负责把"当前 UI 状态"收集起来，
 * 以及开机时把它套回去。fps 开关是本文件里的 static，所以放在这里做。
 ****************************************************************************/

volatile int g_store_keep_alive;

static void pw_settings_collect(struct pw_store_state_s *st)
{
  memset(st, 0, sizeof(*st));
  st->theme    = (uint8_t)(pw_ui_theme() != 0);
  st->zh       = (uint8_t)(pw_i18n_is_zh() ? 1 : 0);
  st->fps_show = (uint8_t)(g_fps_show ? 1 : 0);
}

void pw_settings_save(void)
{
  struct pw_store_state_s st;

  pw_settings_collect(&st);

  /* [AGENT-VM] 2026-11-05 00:50：先不写 flash。
   *
   * 用户实测：中文界面一切正常，切到英文后"关机"就没反应了 —— 差别就在
   * ui_lang_cb 里这次 pw_settings_save()。config0 的写入在 GUI 线程里
   * 会把主循环卡住（T-0017 的 ssave 实验：返回 0 但读回 none，且同进程
   * 访问极易挂死）。既然写不进去还会拖死 UI，就先彻底关掉写入，
   * 等 TF 卡方案落地再打开。读回那半边（load_async）保留，无副作用。
   */

  syslog(LOG_INFO, "[settings] save skipped (flash 写入未打通，等 TF 卡)\n");

  /* 但引用不能消失：一删干净，链接器 --gc-sections 会把 MTD/NOR 那一串也收走，
   * 结果整机起不来（串口停在 SFBL/AB，与 .l1_ret_text 那次同一个坑）。
   */

  if (g_store_keep_alive)
    {
      pw_store_save(&st);
    }
}

/* 后台读 flash：**不能在 main 里同步读**。
 * T-0017 实测：main 早期或稍后直接 open("/dev/config0") 都会把整机挂死
 * （串口只有 SFBL/ABCD，连 NSH 都起不来）；而从 NSH 另起一个进程读同一个
 * 设备却完全正常。差别在任务上下文，不在设备本身。故改为"延时 + 独立任务"，
 * 读到之后再交给 GUI 线程应用（LVGL 对象只许 GUI 线程碰）。
 */

static volatile int g_set_ready;      /* 后台任务已读到记录 */
static volatile int g_set_theme;      /* 读到主题（-1 = 无记录） */
static volatile int g_set_zh;
static volatile int g_set_fps;

static void pw_settings_async_task(void)
{
  struct pw_store_state_s st;

  usleep(2500 * 1000);

  if (pw_store_load(&st) < 0)
    {
      g_set_theme = -1;
      g_set_ready = 1;
      return;
    }

  g_set_theme = st.theme ? 1 : 0;
  g_set_zh    = st.zh ? 1 : 0;
  g_set_fps   = st.fps_show ? 1 : 0;
  g_set_ready = 1;
}

void pw_settings_load_async(void)
{
  nxtask_create("pwset", 110, NULL, 4096,
                (main_t)pw_settings_async_task, NULL, NULL);
}

/****************************************************************************
 * 长按电源键监视
 *
 * /dev/buttons 每次 read 返回一个 32 位按钮位图（btn_buttonset_t）。
 * 本板只注册了 KEY2 一颗，bit0。按住超过 PW_PWR_HOLD_MS 就请求弹菜单。
 * 之所以要抢在 PMU 之前：硬件长按会直接掉电、松手又起来，用户看到"自动重启"。
 ****************************************************************************/

#define PW_PWR_HOLD_MS  1200
#define PW_PWR_POLL_MS  50

static void *pw_key_monitor(void *arg)
{
  uint32_t mask = 0;
  int64_t press_ms = 0;
  bool pressed = false;
  int fd;

  (void)arg;

  fd = open("/dev/buttons", O_RDONLY | O_NONBLOCK);
  if (fd < 0)
    {
      syslog(LOG_WARNING, "[key] /dev/buttons 打开失败 %d，长按菜单不可用\n",
             -errno);
      return NULL;
    }

  for (;;)
    {
      uint32_t v = 0;
      ssize_t n = read(fd, &v, sizeof(v));

      if (n == (ssize_t)sizeof(v))
        {
          bool down = (v & 1u) != 0;

          if (down && !pressed)
            {
              pressed = true;
              struct timespec ts;
              clock_gettime(CLOCK_MONOTONIC, &ts);
              press_ms = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
            }
          else if (!down && pressed)
            {
              pressed = false;
            }
        }

      if (pressed && g_power_req == 0)
        {
          struct timespec ts;
          int64_t now;

          clock_gettime(CLOCK_MONOTONIC, &ts);
          now = (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;

          if (now - press_ms >= PW_PWR_HOLD_MS)
            {
              g_power_req = 1;
              pressed = false;  /* 一次长按只弹一次 */
            }
        }

      usleep(PW_PWR_POLL_MS * 1000);
    }

  (void)mask;
  return NULL;
}

static void pw_key_monitor_start(void)
{
  static bool started;
  pthread_t tid;

  if (started)
    {
      return;
    }

  if (pthread_create(&tid, NULL, pw_key_monitor, NULL) == 0)
    {
      pthread_detach(tid);
      started = true;
      syslog(LOG_INFO, "[key] 长按电源键监视已启动（%d ms）\n",
             PW_PWR_HOLD_MS);
    }
}

static void pw_fps_overlay_sync(void)
{
  static int last_show = -1;

  if (last_show == g_fps_show)
    {
      return;
    }

  last_show = g_fps_show;

  if (g_fps_show)
    {
      pw_fps_overlay_make();
      lv_obj_remove_flag(g_fps_lab, LV_OBJ_FLAG_HIDDEN);
      pw_fps_overlay_paint();
    }
  else if (g_fps_lab != NULL)
    {
      lv_obj_add_flag(g_fps_lab, LV_OBJ_FLAG_HIDDEN);
    }
}

/* 每圈喂一次；内部只在跨过 1 s 时做除法并刷新屏上文字。 */

/* 直接量 /dev/lcd0 的 PUTAREA 单次耗时。
 *
 * 目的：把"帧率低"拆成两半 —— 是 LVGL 渲染慢，还是驱动/面板每帧要等
 * 固定的一段时间（TE 同步、DMA 完成、任务往返）。这里绕过 LVGL，直接
 * 对 lcd 设备做若干次 PUTAREA 并计时，得出的就是纯驱动开销。 */

static void pw_lcdbench(int rounds)
{
  static const int row_y[] = { 0, 150, 300, 430 };
  struct fb_videoinfo_s vi;
  struct lcddev_area_s area;
  uint16_t *buf;
  struct timespec t0;
  struct timespec t1;
  int fd;
  int r;
  int i;

  fd = open("/dev/lcd0", O_RDONLY);
  if (fd < 0)
    {
      printf("[lcdbench] 打不开 /dev/lcd0: %d\n", errno);
      return;
    }

  if (ioctl(fd, LCDDEVIO_GETVIDEOINFO, (unsigned long)&vi) < 0)
    {
      printf("[lcdbench] GETVIDEOINFO 失败: %d\n", errno);
      close(fd);
      return;
    }

  printf("[lcdbench] 面板 %dx%d fmt=%d，每个位置 %d 次\n",
         (int)vi.xres, (int)vi.yres, (int)vi.fmt, rounds);

  buf = malloc(121 * 25 * 2);
  if (buf == NULL)
    {
      printf("[lcdbench] 内存不够\n");
      close(fd);
      return;
    }

  for (i = 0; i < 121 * 25; i++)
    {
      buf[i] = 0x07E0;
    }

  for (r = 0; r < 4; r++)
    {
      uint32_t best = 0xffffffffu;
      uint32_t worst = 0;
      uint64_t sum = 0;

      area.col_start = 8;
      area.col_end   = 128;
      area.row_start = (fb_coord_t)row_y[r];
      area.row_end   = (fb_coord_t)(row_y[r] + 24);
      area.stride    = 121 * 2;
      area.data      = (uint8_t *)buf;

      for (i = 0; i < rounds; i++)
        {
          uint32_t us;

          clock_gettime(CLOCK_MONOTONIC, &t0);
          ioctl(fd, LCDDEVIO_PUTAREA, (unsigned long)&area);
          clock_gettime(CLOCK_MONOTONIC, &t1);

          us = (uint32_t)((t1.tv_sec - t0.tv_sec) * 1000000 +
                          (t1.tv_nsec - t0.tv_nsec) / 1000);
          if (us < best)  best = us;
          if (us > worst) worst = us;
          sum += us;
        }

      printf("[lcdbench] y=%3d: 平均 %5u us，最快 %5u us，最慢 %5u us\n",
             row_y[r], (unsigned)(sum / rounds), (unsigned)best, (unsigned)worst);
    }

  free(buf);
  close(fd);
}

/* 帧率压力测试：把整屏（或一小块）连续标脏，量这块屏的真实上限。
 *
 * 用户看到"FPS 最高只有 20 左右"会以为软件慢。要分清两件事：
 *   - LVGL 只在有东西变的时候才画（静止页 FPS 低是正常的）；
 *   - 真要连续画整屏时，瓶颈是 LCD 链路：390x450x2 = 342 KB/帧，
 *     实测 36~38 ms/帧 -> 约 26~28 fps 封顶，跟 CPU 无关。
 *
 * 本命令先 3 s 全屏、再 2 s 小区域，把这两个数打成一行，用来证明瓶颈在哪。
 */

#define PW_BENCH_FULL_MS  3000
#ifndef PW_BENCH_FILL_MS
#  define PW_BENCH_FILL_MS 3000
#endif
#define PW_BENCH_SMALL_MS 2000

static volatile int g_bench_req;      /* 1 = 外部请求开始（另一个 phywear 进程写） */
static int      g_bench_mode;         /* 0 = 空闲，1 = 全屏，2 = 小区域 */
static uint32_t g_bench_t0;
static uint32_t g_bench_until;
static uint32_t g_bench_frames0;
static uint32_t g_bench_iters;        /* 基准测试期间累计圈数 */
static uint32_t g_bench_iters0;       /* 窗口起点圈数 */
static uint32_t g_bench_sum0;         /* 窗口内渲染耗时累计（us） */
static uint32_t g_bench_n0;           /* 窗口内渲染次数 */
static uint32_t g_bench_fl0;          /* 窗口内 flush 次数 */
static uint32_t g_bench_px0;          /* 窗口内 flush 像素数 */
static uint32_t g_bench_flus0;        /* 窗口内 flush 累计耗时（us） */
static lv_obj_t *g_bench_fill;        /* 纯填色阶段用的整屏不透明矩形 */

extern volatile uint32_t lv_nuttx_lcd_flush_cnt;   /* 未被使用的驱动，仅留作对照 */
extern volatile uint32_t lv_nuttx_lcd_flush_px;
extern volatile uint32_t pw_lcd_flush_cnt;         /* SiFli 自定义 init 真正走的 flush */
extern volatile uint32_t pw_lcd_flush_us;
extern volatile uint32_t pw_lcd_flush_px;

static void pw_bench_begin(int mode)
{
  g_bench_mode    = mode;

  if (mode == 3 && g_bench_fill == NULL)
    {
      /* 纯填色：只让 LVGL 铺满一屏不透明色块，用它测"光栅下限"，
       * 和整屏重绘对比就能分清慢在内容复杂还是慢在系统本身。 */

      g_bench_fill = lv_obj_create(lv_layer_top());
      lv_obj_remove_style_all(g_bench_fill);
      lv_obj_set_size(g_bench_fill, 390, 450);
      lv_obj_set_pos(g_bench_fill, 0, 0);
      lv_obj_set_style_bg_color(g_bench_fill, lv_color_hex(0x102030), 0);
      lv_obj_set_style_bg_opa(g_bench_fill, LV_OPA_COVER, 0);
      lv_obj_remove_flag(g_bench_fill, LV_OBJ_FLAG_SCROLLABLE);
      lv_obj_add_flag(g_bench_fill, LV_OBJ_FLAG_HIDDEN);
    }

  g_bench_t0      = lv_tick_get();
  g_bench_frames0 = g_fps_cnt;
  g_bench_iters0  = g_bench_iters;
  g_bench_sum0    = g_perf_us_sum;
  g_bench_n0      = g_perf_n;
  g_bench_fl0     = pw_lcd_flush_cnt;
  g_bench_px0     = pw_lcd_flush_px;
  g_bench_flus0   = pw_lcd_flush_us;
  g_bench_until   = g_bench_t0 + (mode == 1 ? PW_BENCH_FULL_MS :
                                  mode == 2 ? PW_BENCH_SMALL_MS : PW_BENCH_FILL_MS);

  if (mode == 3 && g_bench_fill)
    {
      lv_obj_remove_flag(g_bench_fill, LV_OBJ_FLAG_HIDDEN);
    }
}

static void pw_bench_report(const char *tag, uint32_t w, uint32_t h,
                            uint32_t frames, uint32_t ms)
{
  /* fps 用 x10 定点打印，避免浮点在 syslog 里变成 %f 不支持的问题 */

  uint32_t fps10 = ms ? (frames * 10000u) / ms : 0;
  uint32_t perus = frames ? (ms * 1000u) / frames : 0;

  syslog(LOG_INFO, "[bench] %s %lux%lu: %lu 帧 / %lu ms = %lu.%lu fps (%lu.%lu ms/帧)\n",
         tag, (unsigned long)w, (unsigned long)h,
         (unsigned long)frames, (unsigned long)ms,
         (unsigned long)(fps10 / 10), (unsigned long)(fps10 % 10),
         (unsigned long)(perus / 1000), (unsigned long)(perus % 1000));
}

static void pw_bench_tick(void)
{
  uint32_t now;
  uint32_t frames;
  uint32_t ms;

  if (g_bench_req)
    {
      g_bench_req = 0;
      pw_bench_begin(1);
      return;
    }

  if (g_bench_mode == 0)
    {
      return;
    }

  g_bench_iters++;

  now = lv_tick_get();
  if (now >= g_bench_until)
    {
      frames = g_fps_cnt - g_bench_frames0;
      ms     = now - g_bench_t0;

      {
        uint32_t iters = g_bench_iters - g_bench_iters0;
        uint32_t rn    = g_perf_n - g_bench_n0;
        uint32_t rus   = g_perf_us_sum - g_bench_sum0;

        uint32_t fl = pw_lcd_flush_cnt - g_bench_fl0;
        uint32_t px = pw_lcd_flush_px - g_bench_px0;
        uint32_t fu = pw_lcd_flush_us - g_bench_flus0;

        syslog(LOG_INFO, "[bench] 主循环 %lu 圈、渲染 %lu 次、平均渲染 %lu us；"
               "flush %lu 次（平均 %lu us/次）、共 %lu px（每帧 %lu px）\n",
               (unsigned long)iters, (unsigned long)rn,
               (unsigned long)(rn ? rus / rn : 0),
               (unsigned long)fl, (unsigned long)(fl ? fu / fl : 0),
               (unsigned long)px,
               (unsigned long)(frames ? px / frames : 0));
      }

      if (g_bench_mode == 1)
        {
          pw_bench_report("全屏", 390, 450, frames, ms);
          pw_bench_begin(2);          /* 接着测小区域，证明瓶颈是面积而不是 CPU */
        }
      else if (g_bench_mode == 2)
        {
          pw_bench_report("小区域", 121, 25, frames, ms);
          pw_bench_begin(3);
        }
      else
        {
          pw_bench_report("填色", 390, 450, frames, ms);

          if (g_bench_fill)
            {
              lv_obj_add_flag(g_bench_fill, LV_OBJ_FLAG_HIDDEN);
            }

          g_bench_mode = 0;
          syslog(LOG_INFO, "[bench] 结束（全屏一帧 ≈ 一次换页的画量）\n");
        }
      return;
    }

  if (g_bench_mode == 1)
    {
      lv_obj_invalidate(lv_screen_active());
    }
  else if (g_bench_mode == 3)
    {
      if (g_bench_fill)
        {
          lv_obj_invalidate(g_bench_fill);
        }
    }
  else
    {
      lv_area_t a;

      a.x1 = 8; a.y1 = 300;
      a.x2 = 128; a.y2 = 324;
      lv_obj_invalidate_area(lv_screen_active(), &a);
    }
}

static void pw_fps_stat_tick(void)
{
  uint32_t now_ms = lv_tick_get();
  uint32_t dt;

  pw_fps_overlay_sync();

  g_stat_loops++;

  if (g_stat_win_ms == 0)
    {
      g_stat_win_ms = now_ms;
      g_stat_win_frames = g_fps_cnt;
      return;
    }

  dt = now_ms - g_stat_win_ms;
  if (dt < 1000)
    {
      return;
    }

  g_stat_fps_x10    = (uint32_t)(((g_fps_cnt - g_stat_win_frames) * 10000u) / dt);
  g_stat_loop_rate  = (uint32_t)((g_stat_loops * 1000u) / dt);
  g_stat_rnd_us     = g_stat_win_n ? (g_stat_win_us / g_stat_win_n) : 0;
  g_stat_rnd_max_us = g_perf_us_max;

  g_stat_win_ms     = now_ms;
  g_stat_win_frames = g_fps_cnt;
  g_stat_loops      = 0;
  g_stat_win_us     = 0;
  g_stat_win_n      = 0;
  g_perf_us_max     = 0;

  pw_fps_overlay_paint();
}

static void pw_fps_init(lv_display_t *disp)
{
  struct fb_videoinfo_s vi;
  struct fb_planeinfo_s pi;

  /* 先挂计帧事件（与有没有 /dev/fb0 无关，真机也能统计帧率） */

  lv_display_add_event_cb(disp, pw_fps_render_cb, LV_EVENT_RENDER_READY, NULL);
  lv_display_add_event_cb(disp, pw_fps_render_cb, LV_EVENT_RENDER_START, NULL);

  /* [AGENT-WIN11] /dev/fb0 在本板由 LCD 驱动异步注册，这里早开一次必然
   * ENOENT（历史上会在启动日志里刷一条 WARN 假错误）。改成最多 300 ms 的
   * 短重试：既消掉误报，也尽量把 flush 快路径（直接写 framebuffer）挂上。
   */

  /* [AGENT-WIN11] 本板实测没有 /dev/fb0 节点（`ls /dev/fb0` 返回 stat
   * failed: 2），LCD 走驱动直刷，所以"直接 memcpy 到 framebuffer"这条快
   * 路径在本硬件上不可用。这里静默跳过：帧率仍然由 RENDER_READY 事件统
   * 计（不依赖 fb0），也不再往启动日志里刷一条假 WARN。
   */

  g_fps_fd = open("/dev/fb0", O_RDWR);
  if (g_fps_fd < 0)
    {
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
 * Private Functions
 ****************************************************************************/

/* imubench 起始页（pw_cap_open 与 main 都要用） */
static int g_imu_bench_page = 2;

/* 截图专用：按名字打开指定屏。root 走 pw_ui_root()（它自己 open），
 * 其余返回待打开的 screen 对象。返回 1 表示名字有效。
 * 真机不带 cap 子命令时这段代码不会被执行，保留不影响固件行为。 */

int pw_cap_open(const char *name)
{
  lv_obj_t *scr = NULL;

  if (strcmp(name, "bt") == 0) { pw_bt_init(); return 1; }
  if      (strcmp(name, "root")      == 0) { pw_ui_root(); return 1; }
  /* 取证：不新建、不重建任何屏，直接拍**当前显示内容**。
   * 用途：开机动画、息屏遮罩这类"只在某个瞬间存在"的画面，
   * 走 root/bt 这类入口会先把画面换掉，拍到的就不是它了。 */
  else if (strcmp(name, "cur")       == 0) { return 1; }
  /* 取证专用：主页 + 蓝牙已启动。
   * 为什么要单独一个名字：金标主页截图（shot root）必须保持"不依赖外部
   * 事件"，否则一键验收会去等一个永远不会来的 BLE 连接；而"连上以后主页
   * 那枚点是蓝的"又非证明不可。所以把"主页 + 起蓝牙 + 等链路"单独做成
   * 一个取证入口，两边互不影响。 */
  else if (strcmp(name, "bthome")    == 0) { if (!pw_bt_is_up()) pw_bt_init();
                                             pw_ui_root(); return 1; }
  /* 取证：主页 + 等"连上 → 再断开"之后再出图。
   * 为什么需要：用户报的现象就是"连过之后断开了，手表还显示已连接"。
   * 要证明它现在会翻回未连接，就得**真的经历一次断开**再拍 ——
   * 而 shot 是"开机后第一个 GUI"，没法先连一次再重启拍照。 */
  else if (strcmp(name, "btafter")   == 0) { if (!pw_bt_is_up()) pw_bt_init();
                                             pw_ui_root(); return 1; }
  /* 同上，但拍的是**蓝牙页**而不是主页：主页那枚点走 ui_bt_tick，
   * 蓝牙页的状态走 btp_tick —— 两条不同的刷新路径，都得证。 */
  else if (strcmp(name, "btlog")     == 0) { if (!pw_bt_is_up()) pw_bt_init();
                                             scr = pw_bt_screen(); }
  else if (strcmp(name, "voice")     == 0) scr = pw_voice_screen();
  else if (strncmp(name, "board", 5) == 0 && name[5] >= '0' && name[5] <= '5')
                                          { return pw_ui_open_board(name[5] - '0'); }
  else if (strcmp(name, "tools")     == 0) { return pw_ui_open_board(2); }
  else if (strcmp(name, "mech")      == 0) { return pw_ui_open_board(0); }
  else if (strcmp(name, "raw")       == 0) scr = pw_raw_screen();
  else if (strcmp(name, "pendulum")  == 0) scr = pw_pendulum_screen();
  else if (strcmp(name, "spring")    == 0) scr = pw_spring_screen();
  else if (strcmp(name, "centri")    == 0) scr = pw_centri_screen();
  else if (strcmp(name, "bounce")    == 0) scr = pw_bounce_screen();
  else if (strcmp(name, "incline")   == 0) scr = pw_incline_screen();
  else if (strcmp(name, "ruler")     == 0) scr = pw_ruler_screen();
  else if (strcmp(name, "spec_accel")== 0) scr = pw_spec_accel_screen();
  else if (strcmp(name, "spec_mic")  == 0) scr = pw_spec_mic_screen();
  else if (strcmp(name, "spec_mag")  == 0) scr = pw_spec_mag_screen();
  else if (strcmp(name, "stopwatch") == 0) scr = pw_motion_stopwatch_screen();
  else if (strcmp(name, "lightgate") == 0) scr = pw_light_gate_screen();
  else if (strcmp(name, "acousticgate") == 0) scr = pw_acoustic_gate_screen();
  else if (strcmp(name, "applause")  == 0) scr = pw_applause_screen();
  else if (strcmp(name, "settings")  == 0) scr = pw_settings_screen();
  else if (strcmp(name, "tone")      == 0) scr = pw_tone_screen();
  else if (strcmp(name, "mic")       == 0) { scr = pw_raw_screen(); pw_raw_goto(4); }
  else if (strcmp(name, "spk")       == 0) { scr = pw_raw_screen(); pw_raw_goto(5); }
  else if (strcmp(name, "gyro")      == 0) { scr = pw_raw_screen(); pw_raw_goto(1); }
  else if (strcmp(name, "mag")       == 0) { scr = pw_raw_screen(); pw_raw_goto(2); }
  else if (strcmp(name, "rawcurve")  == 0) { scr = pw_raw_screen();
                                             pw_raw_set_view(PW_RAW_VIEW_CURVE); }
  else if (strcmp(name, "imu")       == 0) scr = pw_imu_screen();
  else if (strcmp(name, "imubench")  == 0) { pw_imu_bench(1);
                                             scr = pw_imu_screen();
                                             pw_imu_goto(g_imu_bench_page); }
  else if (strcmp(name, "imubias")   == 0) { scr = pw_imu_screen(); pw_imu_goto(1); }
  else if (strcmp(name, "imu6")      == 0) { scr = pw_imu_screen(); pw_imu_goto(2); }
  else if (strcmp(name, "imumag")    == 0) { scr = pw_imu_screen(); pw_imu_goto(3); }
  else if (strcmp(name, "imutraj")   == 0) { scr = pw_imu_screen(); pw_imu_goto(4); }
  else if (strcmp(name, "imutrajbench") == 0) { pw_imu_bench(1);
                                             scr = pw_imu_screen();
                                             pw_imu_goto(4); }
  else if (strcmp(name, "btlink")    == 0) scr = pw_bt_screen();
  else if (strcmp(name, "about")     == 0) scr = pw_about_screen();
  else if (strcmp(name, "ai")        == 0) scr = pw_ai_coach_screen();
  else return 0;

  if (scr != NULL)
    {
      pw_scr_open(scr);
    }

  return 1;
}

/* 蓝牙页取证：等链路起来的上限（见主循环里的说明）。
 * 宿主 pw_ble_central.py 实测 ~7 s 连上；45 s 是“够用但不至于卡死巡检”的值。 */
#define PW_SHOT_BT_WAIT_MS  45000

/* 链路起来之后还要再静默这么久才出图（见主循环里的说明）。
 * 为什么要静默：连接/订阅/写特征会让 BT 侧往控制台打一行行日志
 * （[bt] B3 connected / ccc changed / [bt] msg in …），
 * 而截图是把上千行像素以文本形式从**同一个控制台**流出去的 ——
 * 两者一交叉就会撕掉像素行，pwshot 逐行校验直接拒收整帧
 * （实测：连接期间截 btlink 稳定报 1/1829 line(s) missing）。
 * 宿主侧的动作几秒内就做完了，等它说完再出图即可。 */
#define PW_SHOT_BT_QUIET_MS 4000

/* 截图巡检顺序：覆盖全部主屏 / 实验页 / 工具页 / 生活页 / 设置 / 关于
 * 注意：每页打开后不再返回上级，最后一次统一回根屏即可。 */

static FAR const char *const g_cap_seq[] =
{
  "root",
  "raw",
  "pendulum",
  "spring",
  "centri",
  "incline",
  "ruler",
  "spec_accel",
  "spec_mic",
  "spec_mag",
  "stopwatch",
  "lightgate",
  "acousticgate",
  "applause",
  "settings",
  "btlink",
  "about",
};

#define PW_SHOT_MAIN_COUNT \
  ((int)(sizeof(g_cap_seq) / sizeof(g_cap_seq[0])))

/* 说明 / 数据页巡检（真机截图第二阶段）。
 *
 * 这些页不是主屏，而是各实验的第二页（说明页或数据曲线页），需要 bench
 * 注入合成信号才有内容；因此每页都要先开 bench、再建屏、最后滚到第 2 页。
 * 打开顺序与 phywear.c 里各 bench 子命令保持一致。
 *
 * 铁律 4：这些页里的数值是 bench 注入的合成数据，不是真实测量结果。
 */

struct pw_shot_p2_s
{
  FAR const char *name;
  void (*bench)(int on, int page_interval_s, int start_page);
  lv_obj_t *(*screen)(void);
};

static const struct pw_shot_p2_s g_p2_seq[] =
{
  { "20_pend_p2",         pw_pend_bench,     pw_pendulum_screen         },
  { "21_spring_p2",       pw_spring_bench,   pw_spring_screen           },
  { "22_centri_p2",       pw_centri_bench,   pw_centri_screen           },
  { "23_incline_p2",      pw_incline_bench,  pw_incline_screen          },
  { "24_ruler_p2",        pw_ruler_bench,    pw_ruler_screen            },
  { "25_spec_p2",         pw_spec_bench,     pw_spec_accel_screen       },
  { "26_stopwatch_p2",    pw_time_bench,     pw_motion_stopwatch_screen },
  { "27_lightgate_p2",    pw_time_bench,     pw_light_gate_screen       },
  { "28_acousticgate_p2", pw_time_bench,     pw_acoustic_gate_screen    },
  { "29_applause_p2",     pw_applause_bench, pw_applause_screen         },
};

#define PW_SHOT_P2_COUNT \
  ((int)(sizeof(g_p2_seq) / sizeof(g_p2_seq[0])))

/* Open shot-plan entry `idx`: 0..MAIN_COUNT-1 are the main pages, the rest are
 * the bench-injected second pages. */

static void pw_shot_goto(int idx)
{
  if (idx < PW_SHOT_MAIN_COUNT)
    {
      pw_cap_open(g_cap_seq[idx]);
    }
  else
    {
      const struct pw_shot_p2_s *entry = &g_p2_seq[idx - PW_SHOT_MAIN_COUNT];

      entry->bench(1, 0, 0);          /* Enable injection before screen build */
      pw_scr_open(entry->screen());
      entry->bench(1, 0, 1);          /* Now scroll to the second page */
    }
}

static FAR const char *pw_shot_label(int idx)
{
  if (idx < PW_SHOT_MAIN_COUNT)
    {
      return g_cap_seq[idx];
    }

  return g_p2_seq[idx - PW_SHOT_MAIN_COUNT].name;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/


/****************************************************************************
 * Remote screenshot request (T-0004)
 *
 * Why: after autostart the GUI already owns the display.  A second
 * `phywear shot <page>` process used to run lv_nuttx_init() again, hit the
 * live display driver and die in drv_lcd_fb_init's error dump.  NuttX FLAT
 * builds share one .data segment between tasks, so the second process can
 * simply publish a request here and let the GUI thread build the page and
 * stream the frame -- no second LVGL instance, no display re-init.
 ****************************************************************************/

#define PW_SHOT_REQ_IDLE    0
#define PW_SHOT_REQ_PENDING 1
#define PW_SHOT_REQ_DONE    2
#define PW_SHOT_REQ_WORKING 3

static volatile int g_gui_up;                 /* GUI thread has LVGL up */
static volatile int g_cap_req_state = PW_SHOT_REQ_IDLE;

/* 主题切换请求（-1=无请求，0=方格，1=单排）。NSH 里改不了 LVGL 对象，
 * 所以命令只置这个标志，真正的重建在 GUI 主循环里做。 */

static volatile int g_theme_req = -1;

/* 弹跳测距的在线请求：0=无，1=开始，2=停止。
 * 必须交给 GUI 进程执行 —— NSH 里 `phywear bounce on` 是**另一个进程**，
 * 它起的 pthread 会在命令返回、进程退出时被一起干掉（实测：打印 started
 * 但一条 impact 都没有）。 */

static volatile int g_bounce_req = 0;
static volatile int g_bounce_auto = 0;   /* 自动连测：一次结果出完 2 s 后自动重开 */
static char         g_cap_req_page[32];
static struct timespec g_cap_req_t0;

/* Called in the *requesting* (NSH) task only. */

static int phywear_remote_shot(const char *page)
{
  long waited = 0;

  if (g_cap_req_state == PW_SHOT_REQ_PENDING ||
      g_cap_req_state == PW_SHOT_REQ_WORKING)
    {
      printf("ERROR(cap): 上一张还在出图，等它完\n");
      return EXIT_FAILURE;
    }

  strncpy(g_cap_req_page, page, sizeof(g_cap_req_page) - 1);
  g_cap_req_page[sizeof(g_cap_req_page) - 1] = '\0';
  g_cap_req_state = PW_SHOT_REQ_PENDING;

  printf("[cap] 请求已投给运行中的 GUI：%s\n", page);
  fflush(stdout);

  while (g_cap_req_state != PW_SHOT_REQ_DONE && waited < 60000)
    {
      usleep(100 * 1000);
      waited += 100;
    }

  if (g_cap_req_state != PW_SHOT_REQ_DONE)
    {
      printf("ERROR(cap): 等 GUI 出图超时（%ld ms）\n", waited);
      g_cap_req_state = PW_SHOT_REQ_IDLE;
      return EXIT_FAILURE;
    }

  g_cap_req_state = PW_SHOT_REQ_IDLE;
  return EXIT_SUCCESS;
}

int main(int argc, FAR char *argv[])
{
  pw_llm_apply_builtin();
  pw_relay_sub_start();

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
  const char *cap_screen = NULL;
  bool lvbench_mode = false;     /* phywear lvbench：跑 LVGL 官方 benchmark */  /* 截图专用：phywear cap <name> 打开指定屏并驻留 */
  bool shot_once = false;         /* --shot[=ms]: dump one console screenshot */
  bool shot_sweep = false;        /* --sweep[=ms]: dump the 16 main pages */
  bool shot_p2 = false;           /* --p2[=ms]: dump the 10 bench pages */
  int  shot_p2_only = -1;         /* --p2only=<idx>: dump one bench page */
  int  shot_settle_ms = PW_SHOT_SETTLE_MS;       /* Main page settle time */
  int  shot_p2_settle_ms = PW_SHOT_P2_SETTLE_MS; /* Bench page settle time */
  int  shot_first = 0;            /* First shot-plan index to visit */
  int  shot_last = 0;             /* One past the last shot-plan index */
  int  shot_idx = 0;              /* Current shot-plan index */
  struct timespec shot_t0;        /* When the current page was opened */
  static long shot_bt_conn_ms;    /* 首次看到蓝牙已连接的时刻（相对 shot_t0） */
  static int  shot_bt_phase;      /* btafter 用：0=等连上 1=等断开 */
#ifdef CONFIG_EXAMPLES_PHYWEAR_SIM_ZH_DEMO
  bool cap_sweep = false;         /* 截图巡检：phywear capsweep [停留秒] */
  int  cap_sweep_dwell = 4;       /* 每页停留秒数（宿主机按此间隔抓帧） */
  int  cap_sweep_idx = 0;
  time_t cap_sweep_last = 0;
#endif

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

  /* Console screenshot flags, accepted anywhere on the command line:
   *
   *   --shot[=<ms>]   dump one frame of whatever page this run opens
   *   --sweep[=<ms>]  dump the 16 main pages
   *   --p2[=<ms>]     dump the 10 bench-injected second pages
   *   --all[=<ms>]    both of the above (26 frames, one run)
   *
   * They are peeled out of argv here so that the positional parsing below is
   * unchanged, and they may be combined with any mode, e.g.
   *   phywear lang zh --all=3000
   *   phywear lang zh specbench 0 1 --shot=6000
   *
   * One process can only run the GUI once per board boot: a second phywear
   * run after the first exits hangs in the LCD driver, so a full capture
   * session is always a single run.
   */

  {
    int rd;
    int wr = 1;

    for (rd = 1; rd < argc; rd++)
      {
        if (strncmp(argv[rd], "--shot", 6) == 0 &&
            (argv[rd][6] == '\0' || argv[rd][6] == '='))
          {
            shot_once = true;
            if (argv[rd][6] == '=')
              {
                shot_settle_ms = atoi(&argv[rd][7]);
              }

            continue;
          }

        if (strncmp(argv[rd], "--sweep", 7) == 0 &&
            (argv[rd][7] == '\0' || argv[rd][7] == '='))
          {
            shot_sweep = true;
            if (argv[rd][7] == '=')
              {
                shot_settle_ms = atoi(&argv[rd][8]);
              }

            continue;
          }

        if (strncmp(argv[rd], "--all", 5) == 0 &&
            (argv[rd][5] == '\0' || argv[rd][5] == '='))
          {
            shot_sweep = true;
            shot_p2 = true;
            if (argv[rd][5] == '=')
              {
                shot_settle_ms = atoi(&argv[rd][6]);
                shot_p2_settle_ms = shot_settle_ms;
              }

            continue;
          }

        if (strncmp(argv[rd], "--p2only=", 9) == 0)
          {
            shot_p2 = true;
            shot_p2_only = atoi(&argv[rd][9]);
            if (shot_p2_only < 0 || shot_p2_only >= PW_SHOT_P2_COUNT)
              {
                shot_p2_only = 0;
              }

            continue;
          }

        if (strncmp(argv[rd], "--p2", 4) == 0 &&
            (argv[rd][4] == '\0' || argv[rd][4] == '='))
          {
            shot_p2 = true;
            if (argv[rd][4] == '=')
              {
                shot_p2_settle_ms = atoi(&argv[rd][5]);
              }

            continue;
          }

        argv[wr++] = argv[rd];
      }

    argc = wr;
  }

  if (shot_settle_ms < 200)
    {
      shot_settle_ms = 200;
    }
  else if (shot_settle_ms > 60000)
    {
      shot_settle_ms = 60000;
    }

  if (shot_p2_settle_ms < 200)
    {
      shot_p2_settle_ms = 200;
    }
  else if (shot_p2_settle_ms > 60000)
    {
      shot_p2_settle_ms = 60000;
    }

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

  /* 动效计时：查表 vs 解析式（P1-2 的"计算节省量"必须实测） */

  /* LVGL 官方 benchmark 跑分：`phywear lvbench`（跑完自动在屏幕与串口给汇总表） */

  if (argc > 1 && strcmp(argv[1], "lvbench") == 0)
    {
#if defined(CONFIG_LV_USE_DEMO_BENCHMARK)
      lvbench_mode = true;
      argc = 1;
#else
      printf("lvbench: 本固件未编入 LVGL benchmark（CONFIG_LV_USE_DEMO_BENCHMARK=n）\n");
      return 0;
#endif
    }

  if (argc > 1 && strcmp(argv[1], "motionbench") == 0)
    {
      pw_motion_bench((argc > 2) ? atoi(argv[2]) : 20000);
      return 0;
    }

  if (argc > 1 && strcmp(argv[1], "btio") == 0)
    {
      /* T-0009 对照实验（DSH 建议）：
       *   phywear btio jw        纯 Just Works（不显示、不输入）
       *   phywear btio display   屏上显示 6 位 passkey
       * 只影响之后新发起的配对，不打断已有连接。 */

      return pw_bt_set_io(argc > 2 && strcmp(argv[2], "jw") == 0);
    }

  if (argc > 1 && strcmp(argv[1], "pan") == 0)
    {
      /* T-0009：PANU over BNEP。
       *   pan on <mac>  发起（要求手机已与本机建立 BR/EDR 连接）
       *   pan off       断开
       *   pan status    当前状态 */

      if (argc > 2 && strcmp(argv[2], "on") == 0)
        {
          if (argc < 4)
            {
              printf("用法: phywear pan on AA:BB:CC:DD:EE:FF\n");
              return -EINVAL;
            }

          return pw_pan_start(argv[3]);
        }

      if (argc > 2 && strcmp(argv[2], "off") == 0)
        {
          return pw_pan_stop();
        }

      {
        char buf[256];

        pw_pan_status(buf, sizeof(buf));
        printf("%s\n", buf);
        return 0;
      }
    }

  if (argc > 1 && strcmp(argv[1], "bredr") == 0)
    {
      /* P1–P6：直发 BR/EDR 配置命令，看控制器真实态度（BOARD §18.3） */

      if (argc > 2 && strcmp(argv[2], "state") == 0)
        {
          return pw_bt_bredr_state();
        }

      return pw_bt_bredr_probe();
    }

  /* 掉电保留设置：查看当前 flash 记录 */

  if (argc > 1 && strcmp(argv[1], "store") == 0)
    {
      return pw_store_report() < 0 ? 1 : 0;
    }

  /* 读设置：给 GUI 用的"无 flash 访问"版本 —— GUI 进程碰 /dev/config0 会挂死，
   * 所以设置一律由独立进程读写，GUI 只负责 spawn + 读 /tmp 结果文件。 */

  if (argc > 1 && strcmp(argv[1], "sget") == 0)
    {
      struct pw_store_state_s st;

      if (pw_store_load(&st) < 0)
        {
          printf("none\n");
        }
      else
        {
          printf("%u %u %u\n", st.theme, st.zh, st.fps_show);
        }

      fflush(stdout);
      return 0;
    }

  /* 写设置：phywear ssave <theme> <zh> <fps> */

  if (argc > 1 && strcmp(argv[1], "ssave") == 0)
    {
      struct pw_store_state_s st;

      memset(&st, 0, sizeof(st));
      st.theme    = (uint8_t)(argc > 2 ? atoi(argv[2]) : 0);
      st.zh       = (uint8_t)(argc > 3 ? atoi(argv[3]) : 1);
      st.fps_show = (uint8_t)(argc > 4 ? atoi(argv[4]) : 0);

      printf("[store] save theme=%u zh=%u fps=%u -> %d\n",
             st.theme, st.zh, st.fps_show, pw_store_save(&st));
      fflush(stdout);
      return 0;
    }

  /* 电源动作自测（无触摸时验证 HAL 路径是否可用） */

  if (argc > 1 && strcmp(argv[1], "sysreboot") == 0)
    {
      printf("[power] sysreboot from shell\n");
      fflush(stdout);
      pw_power_reboot();
      return 0;
    }

  if (argc > 1 && strcmp(argv[1], "syspoweroff") == 0)
    {
      printf("[power] syspoweroff from shell\n");
      fflush(stdout);
      pw_power_shutdown();
      return 0;
    }

  /* 电源菜单（没有物理长按也能验证 UI） */

  if (argc > 1 && strcmp(argv[1], "powermenu") == 0)
    {
      g_power_req = 1;
      return 0;
    }

  if (argc > 1 && strcmp(argv[1], "bthci") == 0)
    {
      pw_bt_probe();
      return 0;
    }

  /* 电源快照（T-0002）：随时可读，串口核对电量/充电/引脚原始 mV。
   * 与开机那一行同一个函数，便于"插拔前后各打一次"做对比。 */

  if (argc > 1 && strcmp(argv[1], "bounce") == 0)
    {
      const char *sub = argc > 2 ? argv[2] : "status";

      if (strcmp(sub, "on") == 0)
        {
          g_bounce_req = 1;

          for (int i = 0; i < 50 && g_bounce_req != 0; i++)
            {
              usleep(20 * 1000);
            }

          printf("[bounce] start requested (running=%d)\n", pw_bounce_active());
        }
      else if (strcmp(sub, "auto") == 0)
        {
          g_bounce_auto = 1;
          printf("[bounce] auto=on（每 2 s 自动重开一次测量）\n");
        }
      else if (strcmp(sub, "off") == 0)
        {
          g_bounce_auto = 0;
          g_bounce_req = 2;

          for (int i = 0; i < 100 && g_bounce_req != 0; i++)
            {
              usleep(20 * 1000);
            }

          printf("[bounce] stop requested\n");
        }

      pw_bounce_report();
      return OK;
    }

  if (argc > 1 && strcmp(argv[1], "theme") == 0)
    {
      int want = -1;

      if (argc > 2)
        {
          if (strcmp(argv[2], "list") == 0 || strcmp(argv[2], "1") == 0)
            {
              want = 1;
            }
          else if (strcmp(argv[2], "grid") == 0 || strcmp(argv[2], "0") == 0)
            {
              want = 0;
            }
        }

      if (want < 0)
        {
          printf("[theme] 当前=%s（用法：phywear theme grid|list）\n",
                 pw_ui_theme() ? "单排列表" : "方格宫格");
          return OK;
        }

      /* 交给 GUI 线程重建；这里只等它生效，便于脚本下一步就截图 */

      g_theme_req = want;

      for (int i = 0; i < 50 && g_theme_req >= 0; i++)
        {
          usleep(20 * 1000);
        }

      printf("[theme] -> %s\n", want ? "单排列表" : "方格宫格");
      return OK;
    }

  if (argc > 1 && strcmp(argv[1], "power") == 0)
    {
      pw_power_report();
      return 0;
    }

  /* 息屏/亮屏取证入口（T-0002）：与长按走同一条路径，串口可复现 */

  if (argc > 1 && strcmp(argv[1], "sleep") == 0)
    {
      pw_ui_sleep_now();
      printf("[power] screen off (mask=%d)\n", pw_ui_is_asleep());
      return 0;
    }

  /* 屏上帧率表（T-0002 追加）：`phywear fps` 打印，`fps on/off` 开关 */

  /* [PW_AI_AUTORUN-CLI] AI 主动实验开关：phywear auto [on|off] */

  if (argc > 2 && strcmp(argv[1], "ask") == 0)
    {
      char *text = malloc(512);
      size_t off = 0;
      int i;

      if (!text)
        {
          return -ENOMEM;
        }

      text[0] = '\0';
      for (i = 2; i < argc && off < 500; i++)
        {
          int n = snprintf(text + off, 512 - off, "%s%s", (i > 2) ? " " : "",
                           argv[i]);
          if (n > 0)
            {
              off += (size_t)n;
            }
        }

      printf("[ask] relay rc=%d\n", pw_relay_ask(text));
      free(text);
      return 0;
    }

  if (argc > 2 && strcmp(argv[1], "llm") == 0)
    {
      char *av[6];
      int ac = argc - 1;
      int i;

      if (ac > 5)
        {
          ac = 5;
        }

      av[0] = (char *)"set_llm";
      for (i = 1; i < ac; i++)
        {
          av[i] = argv[i + 1];
        }
      av[ac] = NULL;

      cmd_set_llm(ac, av);
      return 0;
    }

  if (argc > 1 && strcmp(argv[1], "llmapply") == 0)
    {
#if defined(PW_LLM_URL) && defined(PW_LLM_KEY)
      char *av[5];

      av[0] = (char *)"set_llm";
      av[1] = (char *)PW_LLM_URL;
      av[2] = (char *)PW_LLM_MODEL;
      av[3] = (char *)PW_LLM_KEY;
      av[4] = NULL;
      cmd_set_llm(4, av);
#else
      printf("no built-in LLM config (pw_llm_default.h missing)\n");
#endif
      return 0;
    }

  if (argc > 1 && strcmp(argv[1], "auto") == 0)
    {
      if (argc > 2 && strcmp(argv[2], "on") == 0)
        {
          pw_ai_auto_set(true);
        }
      else if (argc > 2 && strcmp(argv[2], "off") == 0)
        {
          pw_ai_auto_set(false);
        }
      else if (argc > 2)
        {
          printf("用法：phywear auto [on|off]\n");
          return EXIT_FAILURE;
        }

      printf("[auto] AI 主动实验=%s (检测到摆动%s自动跑单摆)\n",
             pw_ai_auto_get() ? "开" : "关",
             pw_ai_auto_get() ? "" : "不会");
      return 0;
    }

  if (argc > 1 && strcmp(argv[1], "fps") == 0)
    {
      int on = 1;

      if (argc > 2 && strcmp(argv[2], "off") == 0)
        {
          on = 0;
        }
      else if (argc > 2 && strcmp(argv[2], "on") == 0)
        {
          on = 1;
        }
      else if (argc > 2)
        {
          printf("用法：phywear fps [on|off]\n");
          return EXIT_FAILURE;
        }

      if (argc > 2)
        {
          pw_fps_show(on);
        }

      /* 命令里带上正在跑的 GUI 的实时数字；没在跑就报 0 并提示 */

      printf("[fps] 屏上仪表=%s fps=%u.%u rnd=%ums(max %ums) loop=%u/s touch=%ums\n",
             pw_fps_is_shown() ? "开" : "关",
             (unsigned)(g_stat_fps_x10 / 10), (unsigned)(g_stat_fps_x10 % 10),
             (unsigned)(g_stat_rnd_us / 1000),
             (unsigned)(g_stat_rnd_max_us / 1000),
             (unsigned)g_stat_loop_rate,
             (unsigned)g_lat_ms);
      return 0;
    }

  if (argc > 1 && strcmp(argv[1], "px") == 0)
    {
      /* [AGENT-VM] 累计像素/帧数快照：用来判定"小区域更新是否被放大成整屏"。
       * 用法：phywear px 基线 -> 等 10 s -> phywear px 再看增量。
       */

      printf("[px] flush=%lu px=%lu frames=%lu avg=%lu px/帧\n",
             (unsigned long)pw_lcd_flush_cnt,
             (unsigned long)pw_lcd_flush_px,
             (unsigned long)g_fps_cnt,
             (unsigned long)(g_fps_cnt ? pw_lcd_flush_px / g_fps_cnt : 0));
      return 0;
    }

  if (argc > 1 && strcmp(argv[1], "lcdbench") == 0)
    {
      int n = 20;

      if (argc > 2)
        {
          n = atoi(argv[2]);
          if (n <= 0 || n > 200) n = 20;
        }
      pw_lcdbench(n);
      return 0;
    }

  if (argc > 1 && strcmp(argv[1], "fpsbench") == 0)
    {
      /* 只是置个标志：真正标脏屏幕的是 GUI 线程，本进程连 LVGL 都不碰
       * （碰了就要重复初始化显示驱动，见 T-0004）。 */

      g_bench_req = 1;
      printf("[bench] 已请求：3 s 全屏 + 2 s 小区域，结果约 6 s 后打到串口\n");
      return 0;
    }

  if (argc > 1 && strcmp(argv[1], "plog") == 0)
    {
      pw_power_log_dump();
      return 0;
    }

  /* 现场标定：`phywear batcal <万用表读数mV>` → 反推分压常数并打印 */

  if (argc > 2 && strcmp(argv[1], "batcal") == 0)
    {
      int ref = atoi(argv[2]);

      if (ref < 2500 || ref > 5000)
        {
          printf("用法：phywear batcal <电池实测 mV，2500~5000>\n");
          return EXIT_FAILURE;
        }

      return pw_power_set_ref(ref) < 0 ? EXIT_FAILURE : EXIT_SUCCESS;
    }

  if (argc > 1 && strcmp(argv[1], "wake") == 0)
    {
      pw_ui_wake_now();
      printf("[power] screen on (mask=%d)\n", pw_ui_is_asleep());
      return 0;
    }

  /* 任务 3 的替代通路：USB CDC-ACM + SLIP（本板无无线网卡，见 pw_net.c 注释） */
  if (argc > 1 && strcmp(argv[1], "net") == 0)
    {
      pw_net_up();
      return 0;
    }

  /* 任务 3 的第二条替代通路：**PPP over BLE**（见 pw_btppp.c）。
   * 与 `net` 一样是"做完就退"，不进 GUI —— 这条链路要长期挂着跑 pppd，
   * 再拉一个 GUI 只会白占内存（本板 SRAM 已 96%）。 */
  if (argc > 1 && strcmp(argv[1], "btppp") == 0)
    {
      pw_bt_init();
      pw_btppp_start();
      return 0;
    }

  /* Real-device screenshot: `phywear shot <name>` opens the page, dumps one
   * frame to the console and exits.  Same as `phywear cap <name> --shot`;
   * the host side decoder is tools/pwshot.py. */

  if (argc > 2 && strcmp(argv[1], "shot") == 0)
    {
      /* 自启之后 GUI 已在跑：把请求投给它，本进程到此为止，绝不碰 LVGL。 */

      if (g_gui_up)
        {
          return phywear_remote_shot(argv[2]);
        }

      cap_screen = argv[2];
      shot_once = true;
      argc = 1;
    }

  /* 模拟器截图巡检：phywear capsweep [停留秒] → 依次打开全部页面，
   * 每页停留 N 秒并把页名打印到控制台（宿主机据此逐帧抓取 /dev/fb0）。
   * 仅模拟器截图构建（SIM_ZH_DEMO）可用：页表与状态变量都在该配置下定义。 */

#ifdef CONFIG_EXAMPLES_PHYWEAR_SIM_ZH_DEMO
  if (argc > 1 && strcmp(argv[1], "capsweep") == 0)
    {
      cap_sweep = true;
      if (argc > 2)
        {
          cap_sweep_dwell = atoi(argv[2]);
          if (cap_sweep_dwell < 1)  cap_sweep_dwell = 1;
          if (cap_sweep_dwell > 60) cap_sweep_dwell = 60;
        }
      argc = 1;
    }
#endif

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

  /* 子命令：phywear spkpa 0|1 → 直接开关功放（听感 A/B 用） */

  if (argc > 2 && strcmp(argv[1], "spkpa") == 0)
    {
      int on = atoi(argv[2]) != 0;
      int rc = pw_tone_set_pa(on);

      printf("spkpa: %s -> %d\n", on ? "on" : "off", on);
      return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  /* 子命令：phywear coach <自然语言请求…> → 与 AI 教练页按钮完全相同的路径
   * （向端侧 Agent 发请求；回复异步落在 pw_ai 消息日志里，页面/日志可见）。
   * 无头环境（串口 / 模拟器脚本）用它验证整条链路；发完等待回复打印出来。 */

  if (argc > 2 && strcmp(argv[1], "coach") == 0)
    {
      char text[192];
      char seen[4][192];
      int n = 0;
      int i;
      int rc;
      int waited;

      text[0] = '\0';
      for (i = 2; i < argc && n < (int)sizeof(text) - 2; i++)
        {
          n += snprintf(text + n, sizeof(text) - n, "%s%s",
                        i > 2 ? " " : "", argv[i]);
        }

      memset(seen, 0, sizeof(seen));
      for (i = 0; i < pw_ai_log_count() && i < 4; i++)
        {
          const char *old = pw_ai_log_line(i);

          if (old != NULL)
            {
              strncpy(seen[i], old, sizeof(seen[i]) - 1);
            }
        }

      rc = pw_ai_ask(text);
      printf("coach: \"%s\" -> rc=%d (%s)\n", text, rc,
             rc == 0 ? "sent to agent" : "agent unavailable");

      if (rc == 0)
        {
          for (waited = 0; waited < 200; waited++)   /* 最多等 20 s */
            {
              const char *line = pw_ai_log_line(0);

              if (line != NULL && strncmp(line, seen[0], sizeof(seen[0]) - 1) != 0)
                {
                  printf("coach reply: %s\n", line);
                  break;
                }

              usleep(100000);
            }

          if (waited >= 200)
            {
              printf("coach: no reply within 20 s\n");
            }
        }

      return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  /* 子命令：phywear micread [n] → 连续读麦克风并打印峰值/有效值（自检） */

  /* 子命令：phywear imubench [页] —— 标定 UI 的**注入**演示/自测：
   *   注入已知零偏/刻度/硬铁中心，并自动走完标定流程（不需要人手、不需要触摸）。
   *   画面标题带 [BENCH]，避免把注入数据当测量。页：0 实时 1 零偏 2 六面 3 磁。 */

  if (argc > 1 && strcmp(argv[1], "imubench") == 0)
    {
      g_imu_bench_page = (argc > 2) ? atoi(argv[2]) : 2;
      cap_screen = "imubench";        /* 具体开屏与注入在 pw_cap_open 分支里做 */
    }

  /* 子命令：phywear ahrs [秒] —— 姿态解算。
   *   不带参数：只跑合成数据自检（主机/模拟器/真机都能跑，判断实现是否退化）。
   *   带秒数  ：自检之后再跑**实时姿态读数**：真机用 oneshot 读 IMU/磁，
   *             用 CLOCK_MONOTONIC 算**真实 dt**（不是固定标称值），
   *             每 0.5s 打印 roll/pitch/yaw 与零偏估计。
   *             磁力计拿不到时自动退化为 6 轴（模拟器就没有 /dev/mag0）。
   *   这条子命令是 ③ 惯性标尺的"真机可验证"入口，不依赖 LVGL。 */

  if (argc > 1 && strcmp(argv[1], "ahrs") == 0)
    {
      struct pw_ahrs_s ah;
      float err = 0.0f;
      int   rc = pw_ahrs_selftest(&err);
      int   secs = (argc > 2) ? atoi(argv[2]) : 0;

      printf("ahrs selftest: rc=%d max_attitude_err=%.3f deg\n",
             rc, (double)err);

      if (secs <= 0)
        {
          return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
        }

      pw_ahrs_init(&ah);

      /* 磁力计走的是 pw_sensors_open() 建好的缓存 fd（没有 oneshot 版本），
       * 所以这条无头子命令必须自己把传感器打开一次，否则 mag 恒为 off、
       * 偏航只能靠陀螺积分漂（实测 -2.2 dps 零偏 → ~2°/s 漂移）。 */

      (void)pw_sensors_open();

      {
        struct timespec t0;
        struct timespec t1;
        float last_print = 0.0f;
        int   nmag = 0;
        int   n = 0;

        clock_gettime(CLOCK_MONOTONIC, &t0);

        for (;;)
          {
            struct pw_imu_s imu;
            struct pw_mag_s mag;
            float a[3];
            float g[3];
            float m[3];
            float dt;
            int   have_mag = 0;

            clock_gettime(CLOCK_MONOTONIC, &t1);
            dt = (float)(t1.tv_sec - t0.tv_sec) +
                 (float)(t1.tv_nsec - t0.tv_nsec) * 1e-9f;
            t0 = t1;

            if (dt <= 0.0f || dt > 0.5f)
              {
                dt = 0.02f;
              }

            if (pw_sensors_read_imu_oneshot(&imu) != 0)
              {
                usleep(20 * 1000);
                continue;
              }

            a[0] = imu.ax / 1000.0f;
            a[1] = imu.ay / 1000.0f;
            a[2] = imu.az / 1000.0f;
            g[0] = imu.gx * 1.745329e-5f;
            g[1] = imu.gy * 1.745329e-5f;
            g[2] = imu.gz * 1.745329e-5f;

            if (pw_sensors_read_mag(&mag) == 0)
              {
                m[0] = (float)mag.x;
                m[1] = (float)mag.y;
                m[2] = (float)mag.z;
                have_mag = 1;
                nmag++;
              }

            pw_ahrs_update(&ah, a, g, have_mag ? m : NULL, dt);
            n++;

            last_print += dt;
            if (last_print >= 0.5f)
              {
                float r;
                float p;
                float y;
                float b[3];

                float ar;
                float ap;

                last_print = 0.0f;
                pw_ahrs_euler(&ah, &r, &p, &y);
                pw_ahrs_bias(&ah, b);

                /* 交叉校验：直接用加速度计解算静止倾角（不依赖 AHRS）。
                 * 静止时两者应一致；不一致就说明滤波器或数据有问题。 */

                ar = atan2f(a[1], a[2]) * 57.29578f;
                ap = atan2f(-a[0], sqrtf(a[1] * a[1] + a[2] * a[2])) *
                     57.29578f;

                printf("ahrs t=%5.1fs n=%d mag=%s "
                       "roll=%+7.2f pitch=%+7.2f yaw=%+7.2f "
                       "| accel-tilt r=%+7.2f p=%+7.2f "
                       "| bias(dps)=%+.3f %+.3f %+.3f\n",
                       (double)((float)n * 0.02f), n,
                       have_mag ? "on" : "off", (double)r, (double)p,
                       (double)y, (double)ar, (double)ap,
                       (double)(b[0] * 57.29578f),
                       (double)(b[1] * 57.29578f),
                       (double)(b[2] * 57.29578f));
              }

            if (n >= secs * 50)
              {
                break;
              }

            usleep(20 * 1000);
          }

        printf("ahrs done: %d samples, %d with mag\n", n, nmag);
      }

      pw_sensors_close();
      return EXIT_SUCCESS;
    }

  /* 子命令：phywear calib —— 标定数学自检（一维/二维拟合、六面法、磁椭球、陀螺零偏） */

  if (argc > 1 && strcmp(argv[1], "calib") == 0)
    {
      float err = 0.0f;
      int   rc = pw_calib_selftest(&err);

      printf("calib selftest: rc=%d worst_rel_err=%.4f\n", rc, (double)err);
      return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
    }

  /* 模拟器专用触摸注入：phywear tap <x> <y>
   *
   * 模拟器没有真实触摸（goldfish 的 utouch 是 UInput 注入设备），无头验证"点击
   * 切换视图"这类交互时，主机侧没法点屏幕。这里往 /dev/utouch 写一个
   * touch_sample_s：先 DOWN、隔 ~150ms 再 UP，保证 LVGL 的 indev 轮询能分别看到
   * 按下与抬起，从而产生一次真正的 CLICKED（一次同时带 DOWN|UP 的样本不可靠）。
   * 真机不开 UINPUT_TOUCH，所以这段只在模拟器编进去；真机点击靠手指验证。 */

#ifdef CONFIG_UINPUT_TOUCH
  if (argc > 3 && strcmp(argv[1], "tap") == 0)
    {
      struct touch_sample_s smp;
      int fd;
      int x = atoi(argv[2]);
      int y = atoi(argv[3]);
      int pass;

      fd = open("/dev/utouch", O_WRONLY);
      if (fd < 0)
        {
          printf("tap: open /dev/utouch failed: %d\n", errno);
          return EXIT_FAILURE;
        }

      for (pass = 0; pass < 2; pass++)
        {
          memset(&smp, 0, sizeof(smp));
          smp.npoints = 1;
          smp.point[0].id = 0;
          smp.point[0].x = (int16_t)x;
          smp.point[0].y = (int16_t)y;
          smp.point[0].h = 1;
          smp.point[0].w = 1;
          smp.point[0].flags = TOUCH_ID_VALID | TOUCH_POS_VALID |
                               (pass == 0 ? TOUCH_DOWN : TOUCH_UP);
          (void)write(fd, &smp, SIZEOF_TOUCH_SAMPLE_S(1));
          usleep(pass == 0 ? 300000 : 60000);   /* 按下保持 300ms：LVGL indev 轮询周期内必然能看到按下与抬起各一次 */
        }

      close(fd);
      printf("tap: (%d,%d) injected\n", x, y);
      return EXIT_SUCCESS;
    }
#endif

  if (argc > 1 && strcmp(argv[1], "micread") == 0)
    {
      int n = (argc > 2) ? atoi(argv[2]) : 20;
      int fd = open("/dev/mic0", O_RDONLY);
      int i;

      if (fd < 0)
        {
          printf("micread: open /dev/mic0 failed: %d\n", errno);
          return EXIT_FAILURE;
        }

      for (i = 0; i < n; i++)
        {
          int16_t buf[1024];
          ssize_t r = read(fd, buf, sizeof(buf));

          if (r == (ssize_t)sizeof(buf))
            {
              long sum = 0;
              int peak = 0;
              int j;

              for (j = 0; j < 1024; j++)
                {
                  int v = buf[j] < 0 ? -buf[j] : buf[j];

                  sum += (long)buf[j] * buf[j];
                  if (v > peak)
                    {
                      peak = v;
                    }
                }

              /* 零交叉估主频：用于核对"麦克风采样率/频谱轴"是否准
               * （样本率 16 kHz、每次 1024 样本 → 分辨率 15.6 Hz） */

              {
                int zc = 0;
                int prev = 0;
                int jj;

                for (jj = 0; jj < 1024; jj++)
                  {
                    if (buf[jj] > 300)
                      {
                        if (prev < 0)
                          {
                            zc++;
                          }

                        prev = 1;
                      }
                    else if (buf[jj] < -300)
                      {
                        prev = -1;
                      }
                  }

                printf("mic %2d: peak=%5d (%6.1f dBFS)  rms=%6.1f  ~%5.1f Hz\n",
                       i, peak,
                       (peak > 0)
                           ? 20.0 * log10((double)peak / 32768.0)
                           : -99.0,
                       sqrt((double)sum / 1024.0),
                       zc * 16000.0 / 1024.0);
              }
            }
          else
            {
              printf("mic %2d: read failed (%d)\n", i, (int)r);
            }
        }

      close(fd);
      return EXIT_SUCCESS;
    }

  /* 子命令：phywear tone <Hz> [ms] [dB] → 扬声器自检（发一段正弦） */

  if (argc > 2 && strcmp(argv[1], "tone") == 0)
    {
      uint32_t freq = (uint32_t)atoi(argv[2]);
      int ms = (argc > 3) ? atoi(argv[3]) : 2000;
      int db = (argc > 4) ? atoi(argv[4]) : INT32_MIN;
      int amp = (argc > 5) ? atoi(argv[5]) : 0;
      int rc;

      if (ms <= 0)
        {
          ms = 2000;
        }

      rc = pw_tone_beep(freq, ms, db);
      printf("tone: %s (freq=%u ms=%d db=%d rc=%d errno=%d)\n",
             rc == 0 ? "ok" : "failed", (unsigned)freq, ms, db, rc, errno);
      return rc == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
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

  /* 之后 `phywear shot` 从别的任务进来时，只投请求、不再初始化显示 */

  g_gui_up = 1;
  pw_key_monitor_start();

  /* 主菜单根屏（板块宫格）；各板块按需构建子屏 */

#if defined(CONFIG_LV_USE_DEMO_BENCHMARK)
  if (lvbench_mode)
    {
      /* benchmark 自己建屏、自己排场景；主循环负责驱动 LVGL */

      printf("[LVBENCH] start (LVGL %d.%d.%d)\n", LVGL_VERSION_MAJOR,
             LVGL_VERSION_MINOR, LVGL_VERSION_PATCH);
      fflush(stdout);
      lv_demo_benchmark();
    }
  else
#endif
  if (cap_screen)
    {
      /* 截图专用：打开指定屏（无注入、不自动翻页） */
      if (!pw_cap_open(cap_screen))
        {
          printf("WARN(cap): 未知屏 '%s'\n", cap_screen);
        }
    }
  else if (open_raw)
    {
      pw_scr_open(pw_raw_screen());
    }
  else
    {
      /* [AGENT-WIN11] 开机动画：先给 1.8 s 品牌页，再进主界面。
       * 只走"正常启动"这一支，截图/bench/raw 等取证模式不插广告。
       */

      /* 掉电保留：在第一次建根屏之前套用上次的设置（主题/语言/帧率表） */


      pw_splash_show(2800);

      pw_ui_root();
      pw_splash_release();

      /* 掉电保留：放到 UI 起来之后再读 flash。
       * 早期读 /dev/config0 会把整机挂住（T-0017 排查记录），
       * 此时驱动与缓存都已就绪，实测可正常返回。 */

      pw_settings_load_async();

      /* [AGENT-WIN11] 电源快照：电量 / 充电状态，串口可核对 */
      pw_power_report();

      /* 纯 GUI：后台起蓝牙（理由见 PW_BT_AUTOSTART 的注释）。
       * 只放在这一支 —— `phywear shot/cap` 是取证入口，串口要被像素文本流独占，
       * BT 日志会撕行导致截图被拒（docs/07 坑 ②），所以那些模式绝不自动起栈。 */
#if PW_BT_AUTOSTART
      pw_bt_init_async();
#endif
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

  /* Shot plan: which pages this run visits, and where it stops. */

  if (!shot_once && (shot_sweep || shot_p2))
    {
      shot_first = shot_sweep ? 0 : PW_SHOT_MAIN_COUNT;
      shot_last = shot_p2 ? PW_SHOT_MAIN_COUNT + PW_SHOT_P2_COUNT
                          : PW_SHOT_MAIN_COUNT;

      if (shot_p2_only >= 0)
        {
          shot_first = PW_SHOT_MAIN_COUNT + shot_p2_only;
          shot_last = shot_first + 1;
        }
      shot_idx = shot_first;

      /* The default startup path already opened the root screen (= shot index
       * 0), so only a plan that does not start there needs an explicit open. */

      if (shot_first != 0)
        {
          pw_shot_goto(shot_first);
        }
    }

  /* 把 PhyWear 的 Markdown Skill 发布到 AI Agent 的技能目录。
   * 真机 /data 是 tmpfs，掉电即空，所以每次启动都要装一遍。 */

  pw_skill_install();

  /* 主动场景：摆动检测（每 100 ms 采一次加速度，持续摆动就通知 Agent） */

  pw_watch_init();

  /* 主循环：驱动 LVGL。pendbench 模式附带每 2s 帧节奏/空闲堆统计。 */

  {
    struct timespec tmark;
    struct timespec dmark;
    unsigned long loop = 0;

    clock_gettime(CLOCK_MONOTONIC, &tmark);
    clock_gettime(CLOCK_MONOTONIC, &dmark);

    /* GUI 心跳（每 30 s 一行）：真机排查卡死时用来判断 GUI 线程是否还活着 */

    time_t alive_mark = 0;
    unsigned long alive_frames = 0;
    uint32_t alive_flush_mark = 0;

    /* 告诉 AI Agent 桥：GUI 主循环开始跑，可以受理"打开某页"请求了 */

    pw_ai_set_gui_running(true);
    pw_ai_note_screen("root");

    /* Start the settle timer for --shot / --sweep / --p2 from the moment the
     * first page is on screen. */

    clock_gettime(CLOCK_MONOTONIC, &shot_t0);
    shot_bt_conn_ms = 0;
    shot_bt_phase = 0;

#if PW_PERF_PROBE
    struct timespec perf_mark = shot_t0;
    uint32_t perf_loop_mark = 0;
#endif

    while (1)
      {
        struct timespec tn;
        struct timespec dn;

        /* P0：用 LVGL 自己给出的"下次定时器还有多久"来休眠，而不是固定 10 ms。
         * 动机：固定 10 ms 会给每一帧叠加最多 10 ms 的处理延迟（在 35 ms/帧的
         * 实时页上占 ~30%）。LVGL 的 lv_timer_handler() 返回距下一个定时器的毫秒数，
         * 拿它当休眠上限即可"该睡就睡、该画就画"。
         * 回退：把 PW_LOOP_SLEEP_MAX_MS 设成 10 即恢复原来的固定 10 ms 行为。 */

        {
          uint32_t nxt = lv_timer_handler();

          loop++;
          alive_frames++;
          g_loop_next_ms = nxt;
          pw_fps_stat_tick();
          pw_pin_sync();
          pw_bench_tick();
        }

        {
          struct timespec an;

          clock_gettime(CLOCK_MONOTONIC, &an);
          if (an.tv_sec - alive_mark >= 30)
            {
              /* 注意：这里统计的是 GUI 主循环次数（≈渲染帧率的 2 倍），
               * 不是屏幕刷新率，故记作 loops/s；真实帧率见 benchmark。 */

              uint32_t fc = g_fps_cnt;

              /* 两个口径都打，别混用：
               *   loops/s = GUI 主循环次数（不是屏幕刷新率）
               *   fps     = 本窗口实际推屏帧数/秒 —— 帧率是否退化看这个 */

              syslog(LOG_INFO,
                     "[phywear] alive t=%lds loops/s=%lu fps=%u\n",
                     (long)an.tv_sec,
                     (unsigned long)(alive_frames / 30),
                     (unsigned)((fc - alive_flush_mark) / 30u));
              alive_mark = an.tv_sec;
              alive_frames = 0;
              alive_flush_mark = fc;

              /* [T-0013] 诊断：把 HCI 计数并到心跳里，判断"手机到底有没有连上过"
               * （验完删除） */

              extern void sf32lb52_bt_stat(void);

              sf32lb52_bt_stat();
            }
        }

        /* 弹跳测距自动连测：结果出完（线程退出）2 s 后自动开始下一次 */

        if (g_bounce_auto && g_bounce_req == 0 && !pw_bounce_active())
          {
            static int64_t auto_last;

            struct timespec an;

            clock_gettime(CLOCK_MONOTONIC, &an);

            int64_t nowms = (int64_t)an.tv_sec * 1000 + an.tv_nsec / 1000000;

            if (nowms - auto_last > 2000)
              {
                auto_last = nowms;
                pw_bounce_start();
              }
          }

        /* 弹跳测距：由 GUI 进程真正起停采样线程 */

        if (g_bounce_req != 0)
          {
            if (g_bounce_req == 1)
              {
                pw_bounce_start();
              }
            else
              {
                pw_bounce_stop();
              }

            g_bounce_req = 0;
          }

        /* 后台读到的掉电保留设置：在这里套用（GUI 线程） */

        if (g_set_ready)
          {
            int theme = g_set_theme;

            g_set_ready = 0;
            if (theme >= 0)
              {
                pw_i18n_set_zh(g_set_zh != 0);
                g_fps_show = g_set_fps ? 1 : 0;
                if (pw_ui_theme() != theme)
                  {
                    pw_ui_set_theme(theme);
                    pw_ui_root();
                  }
                printf("[store] loaded theme=%d zh=%d fps=%d\n",
                       theme, g_set_zh, g_set_fps);
                fflush(stdout);
              }
          }

        /* 主题切换请求：立刻重建根屏（RAM 里生效，重启回方格） */

        if (g_theme_req >= 0)
          {
            pw_ui_set_theme(g_theme_req);
            printf("[theme] %s\n", g_theme_req ? "单排列表" : "方格宫格");
            fflush(stdout);
            g_theme_req = -1;
            pw_ui_root();
          }

        /* 远程截图（T-0004）：建屏 → 等 400 ms 让这一屏画稳 → 出图 */

        if (g_cap_req_state == PW_SHOT_REQ_PENDING)
          {
            g_cap_req_state = PW_SHOT_REQ_WORKING;
            clock_gettime(CLOCK_MONOTONIC, &g_cap_req_t0);

            if (!pw_cap_open(g_cap_req_page))
              {
                printf("WARN(cap): 未知屏 '%s'\n", g_cap_req_page);
                fflush(stdout);
                g_cap_req_state = PW_SHOT_REQ_DONE;
              }
          }
        else if (g_cap_req_state == PW_SHOT_REQ_WORKING)
          {
            struct timespec cr;
            long waited;

            clock_gettime(CLOCK_MONOTONIC, &cr);
            waited = (cr.tv_sec - g_cap_req_t0.tv_sec) * 1000 +
                     (cr.tv_nsec - g_cap_req_t0.tv_nsec) / 1000000;

            if (waited >= 400)
              {
                pw_shot_dump(g_cap_req_page, PW_SHOT_SRC_AUTO);
                printf("SHOTMODE DONE %s\n", g_cap_req_page);
                fflush(stdout);
                g_cap_req_state = PW_SHOT_REQ_DONE;
              }
          }

        /* AI Agent 桥：执行挂起的"打开某页"请求（必须在 GUI 线程内） */

        pw_ai_poll();

        /* 长按电源键：菜单只能在 GUI 线程里建 */

        if (g_power_req != 0)
          {
            g_power_req = 0;
            pw_ui_power_menu();
          }

        /* 主动场景：自己发现"手表在持续摆动"，并把事件推给 AI Agent。
         * 内部按 100 ms 限频，不参与截图/实验页的采样节奏。 */

        pw_watch_poll();

        /* Console screenshot: let the page settle, stream one frame to the
         * host, then either finish (--shot) or advance (--sweep / --p2). */

        if (shot_once || shot_sweep || shot_p2)
          {
            struct timespec sh;
            int settle = (shot_idx >= PW_SHOT_MAIN_COUNT) ? shot_p2_settle_ms
                                                          : shot_settle_ms;
            long waited;

            clock_gettime(CLOCK_MONOTONIC, &sh);
            waited = ((sh.tv_sec - shot_t0.tv_sec) * 1000 +
                      (sh.tv_nsec - shot_t0.tv_nsec) / 1000000);

            /* 蓝牙页的取证等待（**仅 shot 模式**）。
             *
             * 为什么需要：这几页要证明的恰恰是"连上/断开之后长什么样" ——
             * 未连接时点是灰的、日志是空的，截出来什么都证明不了；
             * 而"连上/断开"都是**外部行为**（宿主的 BLE 中心设备发起），
             * 板子只能等。等的时候 LVGL 照常跑，指示点/状态字本来就会
             * 随 pw_bt_link() 自己醒/变蓝。
             *
             * 三个入口，语义不同：
             *   btlink / bthome ：等**连上** → 静默 PW_SHOT_BT_QUIET_MS → 出图
             *   btafter         ：等连上 → 等**断开** → 再静默 → 出图
             *                     （用户报的就是"断开后还显示已连接"，
             *                       所以要拍的是断开之后那一刻）
             *
             * 为什么要静默：BT 侧在连接/订阅/读写的每一步都会往控制台打日志，
             * 而截图是把上千行像素以**文本**形式从同一个控制台流出去的 ——
             * 两者一交叉就撕掉像素行，pwshot 逐行校验会直接拒收整帧。
             *
             * 超时都照出图并打印原因，免得把"没连上"伪装成"连上了"。 */
            if (shot_once && cap_screen != NULL &&
                (strcmp(cap_screen, "btlink") == 0 ||
                 strcmp(cap_screen, "bthome") == 0 ||
                 strcmp(cap_screen, "btafter") == 0 ||
                 strcmp(cap_screen, "btlog") == 0))
              {
                int after = (strcmp(cap_screen, "btafter") == 0 ||
                             strcmp(cap_screen, "btlog") == 0);
                int conn = pw_bt_link()->connected ? 1 : 0;

                if (after && shot_bt_phase == 0)
                  {
                    /* 阶段 0：等连上（证明链路真的建立过） */
                    if (!conn)
                      {
                        if (waited < settle + PW_SHOT_BT_WAIT_MS)
                          {
                            continue;
                          }

                        printf("[phywear] BT shot(btafter): 一直没连上，"
                               "按未连接出图\n");
                        fflush(stdout);
                      }
                    else
                      {
                        shot_bt_phase = 1;
                        shot_bt_conn_ms = 0;
                        printf("[phywear] BT shot(btafter): 已连上，"
                               "等它断开 ……\n");
                        fflush(stdout);
                        continue;               /* 立刻接着等断开 */
                      }
                  }
                else if (after && shot_bt_phase == 1)
                  {
                    /* 阶段 1：等断开，断开后再静默一会儿才出图 */
                    if (conn)
                      {
                        if (waited < settle + PW_SHOT_BT_WAIT_MS)
                          {
                            continue;
                          }

                        printf("[phywear] BT shot(btafter): 等断开超时，"
                               "仍连着（这次拍不到断开后的样子）\n");
                        fflush(stdout);
                      }
                    else
                      {
                        if (shot_bt_conn_ms == 0)
                          {
                            shot_bt_conn_ms = waited;
                            printf("[phywear] BT shot(btafter): 已断开，"
                                   "静默 %d ms 后出图\n", PW_SHOT_BT_QUIET_MS);
                            fflush(stdout);
                          }

                        if (waited < shot_bt_conn_ms + PW_SHOT_BT_QUIET_MS)
                          {
                            continue;
                          }
                      }
                  }
                else
                  {
                    /* btlink / bthome：等连上 → 静默 → 出图 */
                    if (!conn)
                      {
                        if (waited < settle + PW_SHOT_BT_WAIT_MS)
                          {
                            if (waited >= settle && waited - settle < 1000)
                              {
                                printf("[phywear] BT shot: 等链路起来 ……\n");
                                fflush(stdout);
                              }

                            continue;
                          }

                        printf("[phywear] BT shot: WAIT-TIMEOUT 未连上，"
                               "按未连接状态出图\n");
                        fflush(stdout);
                      }
                    else
                      {
                        if (shot_bt_conn_ms == 0)
                          {
                            shot_bt_conn_ms = waited;
                          }

                        if (waited < shot_bt_conn_ms + PW_SHOT_BT_QUIET_MS)
                          {
                            continue;
                          }
                      }
                  }
              }

            if (waited >= settle)
              {
                const char *sname;

                if (shot_once)
                  {
                    sname = (cap_screen != NULL) ? cap_screen : "page";
                  }
                else
                  {
                    sname = pw_shot_label(shot_idx);
                  }

                pw_shot_dump(sname, PW_SHOT_SRC_AUTO);

                if (shot_once)
                  {
                    printf("SHOTMODE DONE %s\n", sname);
                    fflush(stdout);
                    break;
                  }

                shot_idx++;
                if (shot_idx >= shot_last)
                  {
                    printf("SHOTSWEEP DONE %d pages\n", shot_idx - shot_first);
                    fflush(stdout);
                    break;
                  }

                printf("SHOTNEXT %d %s\n", shot_idx, pw_shot_label(shot_idx));
                fflush(stdout);
                pw_shot_goto(shot_idx);
                clock_gettime(CLOCK_MONOTONIC, &shot_t0);
              }
          }

#ifdef CONFIG_EXAMPLES_PHYWEAR_SIM_ZH_DEMO
        /* capsweep：每 cap_sweep_dwell 秒切到下一页，并把页名打印到控制台，
         * 宿主机据此按固定节奏抓 /dev/fb0，一轮跑完全部页面。 */
        if (cap_sweep)
          {
            struct timespec cs;

            clock_gettime(CLOCK_MONOTONIC, &cs);
            if (cap_sweep_last == 0 ||
                (cs.tv_sec - cap_sweep_last) >= cap_sweep_dwell)
              {
                if (cap_sweep_idx >=
                    (int)(sizeof(g_cap_seq) / sizeof(g_cap_seq[0])))
                  {
                    printf("CAPSWEEP DONE (%d pages)\n", cap_sweep_idx);
                    fflush(stdout);
                    break;
                  }

                printf("CAPSWEEP %d %s\n", cap_sweep_idx,
                       g_cap_seq[cap_sweep_idx]);
                fflush(stdout);
                pw_cap_open(g_cap_seq[cap_sweep_idx]);
                cap_sweep_idx++;
                cap_sweep_last = cs.tv_sec;
              }
          }
#endif

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

#if PW_PERF_PROBE
        /* 每秒一行：瞬时 fps + 渲染耗时。fps 用**1 s 窗口**，避免 30 s 平均把
         * "图线 10 Hz 才重绘"的空闲时间摊进来（那会掩盖瞬时能力）。 */

        {
          struct timespec pn;

          clock_gettime(CLOCK_MONOTONIC, &pn);
          if ((pn.tv_sec - perf_mark.tv_sec) >= 1)
            {
              uint32_t n = g_perf_n;
              uint32_t lps = loop - perf_loop_mark;

              printf("[PERF] fps=%u render_avg=%uus render_max=%uus loops=%u\n",
                     (unsigned)g_fps_cnt,
                     (unsigned)(n > 0 ? g_perf_us_sum / n : 0),
                     (unsigned)g_perf_us_max, (unsigned)lps);
              fflush(stdout);

              g_fps_cnt = 0;
              g_perf_n = 0;
              g_perf_us_sum = 0;
              g_perf_us_max = 0;
              perf_mark = pn;
              perf_loop_mark = loop;
            }
        }
#endif

        /* 休眠：不超过 PW_LOOP_SLEEP_MAX_MS，也不超过 LVGL 说的"下次还有多久"。
         * 下限 1 ms：避免空转把 CPU 吃满（本板还有 50 Hz 采样线程要跑）。 */

        {
          uint32_t slp = g_loop_next_ms;

          if (slp > PW_LOOP_SLEEP_MAX_MS)
            {
              slp = PW_LOOP_SLEEP_MAX_MS;
            }

          if (slp < 1)
            {
              slp = 1;
            }

          usleep(slp * 1000);
        }
      }

    pw_ai_set_gui_running(false);
  }
  return EXIT_SUCCESS;
}

/****************************************************************************
 * [AGENT-WIN11] 上电自启支持
 *
 * 由板级 board_late_initialize() 调用：设备上电后直接进手表界面，
 * 不再依赖 USB 串口与手敲 `phywear`。
 *
 * 本 app 每个 boot 周期只能跑一次 GUI（见 main() 内说明），自启只创建
 * 一次任务，因此不会踩到「第二次运行卡在 LCD 驱动」的坑。
 ****************************************************************************/

#ifdef CONFIG_EXAMPLES_PHYWEAR_AUTOSTART

static FAR char *g_pw_autostart_argv[] =
{
  (FAR char *)"phywear",
  NULL
};

/* [AGENT-WIN11] 供板级代码调用的自启入口。
 *
 * 注意：openvela 的 nxtask_create 是 7 参数版本
 *   (name, priority, stack_addr, stack_size, entry, argv, envp)
 * 直接以 main 为入口、argv 显式给出，等价于在 NSH 里敲 `phywear`。
 */

int phywear_autostart(void)
{
  int pid;

  pid = nxtask_create("phywear",
                      CONFIG_EXAMPLES_PHYWEAR_PRIORITY,
                      NULL,
                      CONFIG_EXAMPLES_PHYWEAR_STACKSIZE,
                      main,
                      g_pw_autostart_argv,
                      NULL);
  if (pid < 0)
    {
      syslog(LOG_ERR, "[autostart] phywear task failed: %d\n", pid);
    }

  return pid;
}

#endif /* CONFIG_EXAMPLES_PHYWEAR_AUTOSTART */
