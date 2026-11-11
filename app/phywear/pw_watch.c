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
#include "pw_ai.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* 主动场景开关：检测到"持续摆动"后把事件推给 AI Agent，让它自动跑单摆实验
 * 并解释结果（"主动 + 执行"场景）。
 *
 * 历史：2026-09-13 真机实测启动后十几秒整机卡住，故默认关闭。2026-09-15 定位到
 * **真正的卡死原因**：不是切页，而是 `phywear_run_experiment` 在 **agent 任务**里
 * 又按 50 Hz 采了一遍 IMU，而屏幕上的实验页同时也在 50 Hz 采同一颗传感器
 * （oneshot 每样本还 open/ioctl/close 一次）→ 两路 50 Hz 抢同一个 I2C。
 *
 * 修法（本次）：Agent 侧**不再采样**，改为"开页 → 等实验页把结果登记出来 → 读结果"
 * （见 pw_ai.h 的 pw_ai_publish_result）。I2C 永远只有 GUI 一路，因此重新打开。
 * ⚠️ 打开后必须真机泡机复验（反复晃动 + ≥10 min），若再卡死立即置 0 并如实记录。 */

#ifndef PW_WATCH_PROACTIVE
#  define PW_WATCH_PROACTIVE 1
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

/****************************************************************************
 * Private Functions
 ****************************************************************************/


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
  pw_ai_note(text);

/* [PW_AI_AUTORUN] 主动推送默认关闭；设置页打开才真的叫 agent（也就不会自动跳页）。 */

  if (!pw_ai_auto_get())
    {
      syslog(LOG_INFO, "[phywear] proactive push off (AI auto-run disabled)\n");
      return;
    }

#if defined(CONFIG_EXAMPLES_AI_AGENT_VELA) && PW_WATCH_PROACTIVE
  /* 统一走 pw_ai_ask()（而不是自己再开一个 velaclaw 客户端）：
   *   - 只有一个客户端实例，避免重复注册 tap；
   *   - 回复由 pw_ai_ask_cb 自动 humanize 后写进手表的 AI 消息日志；
   *   - Agent 没起来时 velaclaw_client_open() 会干净返回 NULL（总线未初始化的
   *     探测在客户端里做），这里只记一条日志，**不会 panic**。
   *     （2026-09-15 实测：Agent 没起时直接 velaclaw_ask 会在
   *      msg_queue_push→pthread_mutex_take 撞 DEBUGASSERT 把 GUI 带崩。） */

  if (pw_ai_ask(text) != 0)
    {
      syslog(LOG_WARNING, "[phywear] proactive event not delivered "
             "(agent not running?); kept in the AI log anyway\n");
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
