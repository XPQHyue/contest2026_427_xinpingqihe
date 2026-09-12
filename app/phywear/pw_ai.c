/****************************************************************************
 * apps/examples/phywear/pw_ai.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * PhyWear ←→ AI Agent 桥的实现（见 pw_ai.h 说明）。
 *
 * 邮箱很小：只保存"下一个要打开的页名"。不需要队列，因为 AI Agent 一次只
 * 会请求打开一页，而 GUI 每 10 ms 就会取一次。
 ****************************************************************************/

#include <nuttx/config.h>

#include <errno.h>
#include <string.h>
#include <stdbool.h>

#include <nuttx/mutex.h>

#include "pw_ai.h"

/****************************************************************************
 * 页面目录
 ****************************************************************************/

struct pw_ai_screen_s
{
  FAR const char *name;
  FAR const char *desc;
};

static const struct pw_ai_screen_s g_pw_ai_screens[] =
{
  { "root",         "Home grid of all experiment groups" },
  { "raw",          "Raw sensors: accelerometer / gyro / magnetometer / light" },
  { "pendulum",     "Pendulum: measure local gravity g from swing period" },
  { "spring",       "Spring: oscillation period and relative amplitude" },
  { "centri",       "Centripetal: radius r from a vs w^2 slope" },
  { "incline",      "Incline: tilt angle from gravity components" },
  { "ruler",        "Magnetic ruler: count magnets passing by" },
  { "spec_accel",   "Acceleration spectrum (FFT)" },
  { "spec_mic",     "Microphone spectrum (FFT)" },
  { "spec_mag",     "Magnetic field spectrum (FFT)" },
  { "stopwatch",    "Motion stopwatch: knock-knock event timing" },
  { "lightgate",    "Light gate: threshold crossing timing" },
  { "acousticgate", "Acoustic gate: loudness threshold timing" },
  { "applause",     "Applause meter: loudness and clap counting" },
  { "settings",     "Settings (UI language)" },
  { "about",        "About PhyWear" },
};

#define PW_AI_SCREEN_N ((int)(sizeof(g_pw_ai_screens) / sizeof(g_pw_ai_screens[0])))

int pw_ai_screen_count(void)
{
  return PW_AI_SCREEN_N;
}

const char *pw_ai_screen_name(int idx)
{
  if (idx < 0 || idx >= PW_AI_SCREEN_N)
    {
      return NULL;
    }

  return g_pw_ai_screens[idx].name;
}

const char *pw_ai_screen_desc(int idx)
{
  if (idx < 0 || idx >= PW_AI_SCREEN_N)
    {
      return NULL;
    }

  return g_pw_ai_screens[idx].desc;
}

/****************************************************************************
 * 状态
 ****************************************************************************/

#define PW_AI_NAME_MAX 24

static mutex_t       g_pw_ai_lock = NXMUTEX_INITIALIZER;
static volatile bool g_pw_ai_gui_running;
static char          g_pw_ai_pending[PW_AI_NAME_MAX];
static char          g_pw_ai_current[PW_AI_NAME_MAX];

bool pw_ai_gui_running(void)
{
  return g_pw_ai_gui_running;
}

void pw_ai_set_gui_running(bool running)
{
  g_pw_ai_gui_running = running;
}

const char *pw_ai_current_screen(void)
{
  return g_pw_ai_current;
}

void pw_ai_note_screen(const char *name)
{
  if (name == NULL)
    {
      return;
    }

  nxmutex_lock(&g_pw_ai_lock);
  strncpy(g_pw_ai_current, name, PW_AI_NAME_MAX - 1);
  g_pw_ai_current[PW_AI_NAME_MAX - 1] = '\0';
  nxmutex_unlock(&g_pw_ai_lock);
}

int pw_ai_request_open(const char *screen)
{
  int i;
  bool known = false;

  if (screen == NULL)
    {
      return -EINVAL;
    }

  if (!g_pw_ai_gui_running)
    {
      /* GUI 没在跑：请求永远不会被执行，明确告诉调用方 */

      return -ENODEV;
    }

  for (i = 0; i < PW_AI_SCREEN_N; i++)
    {
      if (strcmp(screen, g_pw_ai_screens[i].name) == 0)
        {
          known = true;
          break;
        }
    }

  if (!known)
    {
      return -EINVAL;
    }

  nxmutex_lock(&g_pw_ai_lock);
  strncpy(g_pw_ai_pending, screen, PW_AI_NAME_MAX - 1);
  g_pw_ai_pending[PW_AI_NAME_MAX - 1] = '\0';
  nxmutex_unlock(&g_pw_ai_lock);

  return OK;
}

/****************************************************************************
 * GUI 线程侧
 ****************************************************************************/

void pw_ai_poll(void)
{
  char name[PW_AI_NAME_MAX];

  nxmutex_lock(&g_pw_ai_lock);
  if (g_pw_ai_pending[0] == '\0')
    {
      nxmutex_unlock(&g_pw_ai_lock);
      return;
    }

  strncpy(name, g_pw_ai_pending, PW_AI_NAME_MAX - 1);
  name[PW_AI_NAME_MAX - 1] = '\0';
  g_pw_ai_pending[0] = '\0';
  nxmutex_unlock(&g_pw_ai_lock);

  if (pw_cap_open(name))
    {
      pw_ai_note_screen(name);
    }
}
