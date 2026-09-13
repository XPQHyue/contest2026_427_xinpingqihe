/****************************************************************************
 * apps/examples/phywear/pw_watch.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Sustained-swing detector + proactive agent event.  See pw_watch.h.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <math.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <syslog.h>
#include <time.h>

#include "pw_watch.h"
#include "phywear_sensors.h"

#ifdef CONFIG_EXAMPLES_AI_AGENT_VELA
#  include <velaclaw/client.h>
#endif

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* 主动场景开关：检测到"持续摆动"后是否把事件推给 AI Agent 并让它自动跑
 * 单摆实验。
 *
 * ⚠️ 默认关闭（2026-09-13 真机实测）：Agent 侧的 phywear_run_experiment 会在
 * **agent 任务**里直接读传感器、并请求 GUI 打开实验页，两条线程同时用同一颗
 * IMU + 同时切页，实测启动后十几秒就整机卡住。检测本身保留（只打日志），
 * 主动场景改用不抢传感器的方式重做（见 docs/主动场景说明）。
 *
 * 需要复现原行为时把它置 1。 */

#ifndef PW_WATCH_PROACTIVE
#  define PW_WATCH_PROACTIVE 0
#endif

/* One IMU sample every 100 ms: enough to see a 0.5-2 Hz swing without
 * competing with the experiment pages, which sample at 50 Hz. */

#define PW_WATCH_PERIOD_MS   100

/* Sliding window: 40 samples = 4 s. */

#define PW_WATCH_WINDOW      40

/* Peak-to-peak magnitude that counts as "moving".  The watch resting on a desk
 * stays within a few mg; a hand swing is hundreds of mg. */

#define PW_WATCH_RANGE_MG    250.0f

/* How long the motion must last before it is worth an agent turn. */

#define PW_WATCH_HOLD        25    /* consecutive samples = 2.5 s */

/* Do not fire again while the user keeps playing with the watch. */

#define PW_WATCH_COOLDOWN_S  60

/****************************************************************************
 * Private Data
 ****************************************************************************/

static float g_pw_watch_win[PW_WATCH_WINDOW];
static int g_pw_watch_count;
static int g_pw_watch_head;
static int g_pw_watch_hold;
static time_t g_pw_watch_last_fire;
static struct timespec g_pw_watch_last_sample;

#if defined(CONFIG_EXAMPLES_AI_AGENT_VELA) && PW_WATCH_PROACTIVE
static velaclaw_client_t *g_pw_watch_client;
#endif

/****************************************************************************
 * Private Functions
 ****************************************************************************/

#if defined(CONFIG_EXAMPLES_AI_AGENT_VELA) && PW_WATCH_PROACTIVE
static void pw_watch_reply(int status, const char *text, void *cookie)
{
  (void)cookie;

  syslog(LOG_INFO, "[phywear] proactive agent reply (%d): %.120s\n",
         status, text != NULL ? text : "(null)");
}

#endif /* CONFIG_EXAMPLES_AI_AGENT_VELA && PW_WATCH_PROACTIVE */

/* Push the event text to the agent.  With CONFIG_EXAMPLES_AI_AGENT_VELA the
 * agent is linked in and answers asynchronously; otherwise the event is only
 * logged, which keeps the app usable without the agent. */

static void pw_watch_notify(float range, float seconds, long uptime_s)
{
  char text[256];

  /* "检测到持续摆动" also matches the agent's offline intent table, so the
   * scenario still works when no LLM backend is configured. */

  snprintf(text, sizeof(text),
           "PhyWear 巡检：检测到持续摆动（窗口振幅 %.2f g，持续 %.1f s）。"
           "请跑单摆实验测重力加速度 g 并解释结果。",
           (double)(range / 1000.0f), (double)seconds);

  syslog(LOG_WARNING, "[phywear] t=%lds %s\n", uptime_s, text);

#if defined(CONFIG_EXAMPLES_AI_AGENT_VELA) && PW_WATCH_PROACTIVE
  /* The agent may still be starting when PhyWear comes up (or the user may
   * start it later), so retry the link here instead of giving up for good. */

  if (g_pw_watch_client == NULL)
    {
      g_pw_watch_client = velaclaw_client_open("phywear");

      if (g_pw_watch_client == NULL)
        {
          syslog(LOG_WARNING, "[phywear] agent client unavailable; "
                 "proactive events will only be logged\n");
        }
    }

  if (g_pw_watch_client != NULL)
    {
      velaclaw_ask_req_t req;

      req.text = text;
      req.timeout_ms = 0;

      if (velaclaw_ask(g_pw_watch_client, &req, pw_watch_reply, NULL) != 0)
        {
          syslog(LOG_ERR, "[phywear] cannot reach the agent\n");
        }
    }
#else
  syslog(LOG_INFO, "[phywear] proactive push disabled: event only logged\n");
#endif
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void pw_watch_init(void)
{
  memset(g_pw_watch_win, 0, sizeof(g_pw_watch_win));
  g_pw_watch_count = 0;
  g_pw_watch_head = 0;
  g_pw_watch_hold = 0;
  g_pw_watch_last_fire = 0;
  clock_gettime(CLOCK_MONOTONIC, &g_pw_watch_last_sample);

#if defined(CONFIG_EXAMPLES_AI_AGENT_VELA) && PW_WATCH_PROACTIVE
  g_pw_watch_client = velaclaw_client_open("phywear");

  if (g_pw_watch_client == NULL)
    {
      syslog(LOG_WARNING, "[phywear] agent client unavailable; "
             "proactive events will only be logged\n");
    }
#endif
}

bool pw_watch_poll(void)
{
  struct pw_imu_s imu;
  struct timespec now;
  float mag;
  float lo;
  float hi;
  float range;
  int i;

  clock_gettime(CLOCK_MONOTONIC, &now);

  if ((now.tv_sec - g_pw_watch_last_sample.tv_sec) * 1000 +
      (now.tv_nsec - g_pw_watch_last_sample.tv_nsec) / 1000000
      < PW_WATCH_PERIOD_MS)
    {
      return false;
    }

  g_pw_watch_last_sample = now;

  memset(&imu, 0, sizeof(imu));
  if (pw_sensors_read_imu(&imu) != 0)
    {
      return false;
    }

  mag = sqrtf((float)imu.ax * (float)imu.ax +
              (float)imu.ay * (float)imu.ay +
              (float)imu.az * (float)imu.az);

  g_pw_watch_win[g_pw_watch_head] = mag;
  g_pw_watch_head = (g_pw_watch_head + 1) % PW_WATCH_WINDOW;

  if (g_pw_watch_count < PW_WATCH_WINDOW)
    {
      g_pw_watch_count++;
      return false;
    }

  lo = hi = g_pw_watch_win[0];
  for (i = 1; i < PW_WATCH_WINDOW; i++)
    {
      if (g_pw_watch_win[i] < lo)
        {
          lo = g_pw_watch_win[i];
        }

      if (g_pw_watch_win[i] > hi)
        {
          hi = g_pw_watch_win[i];
        }
    }

  range = hi - lo;

  if (range < PW_WATCH_RANGE_MG)
    {
      g_pw_watch_hold = 0;
      return false;
    }

  g_pw_watch_hold++;

  if (g_pw_watch_hold < PW_WATCH_HOLD)
    {
      return false;
    }

  g_pw_watch_hold = 0;

  if (g_pw_watch_last_fire != 0 &&
      now.tv_sec - g_pw_watch_last_fire < PW_WATCH_COOLDOWN_S)
    {
      return false;
    }

  g_pw_watch_last_fire = now.tv_sec;

  pw_watch_notify(range, (float)PW_WATCH_HOLD * PW_WATCH_PERIOD_MS / 1000.0f,
                  (long)now.tv_sec);
  return true;
}
