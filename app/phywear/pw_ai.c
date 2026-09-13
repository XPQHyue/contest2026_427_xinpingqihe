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
#include <stdio.h>
#include <time.h>
#include <stdbool.h>
#include <syslog.h>

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
  { "raw",          "Raw sensors: accel / gyro / mag / light / microphone / speaker" },
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
  { "tone",         "Tone generator: sine 40 Hz - 4 kHz from the speaker" },
  { "mic",          "Raw microphone level (dBFS) and 32-band bars" },
  { "spk",          "Speaker page: play/stop a 440 Hz test tone" },
  { "settings",     "Settings (UI language)" },
  { "about",        "About PhyWear" },
  { "ai",           "AI coach: on-device agent panel (status + quick requests + latest reply)" },
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

/****************************************************************************
 * AI 教练页：消息日志 + 端侧 Agent 请求
 ****************************************************************************/

#define PW_AI_LOG_N     4                     /* 最近 4 条 */
#define PW_AI_LOG_LEN   180                   /* 单条上限（截断保护） */

static char g_pw_ai_log[PW_AI_LOG_N][PW_AI_LOG_LEN];
static long g_pw_ai_log_t[PW_AI_LOG_N];       /* 写入时的 uptime（秒） */
static int  g_pw_ai_log_next;                 /* 环形写指针 */
static int  g_pw_ai_log_cnt;
static bool g_pw_ai_agent_ready;

static long pw_ai_uptime_s(void)
{
  struct timespec ts;

  if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
    {
      return 0;
    }

  return (long)ts.tv_sec;
}

void pw_ai_note(const char *text)
{
  int slot;

  if (text == NULL || text[0] == '\0')
    {
      return;
    }

  nxmutex_lock(&g_pw_ai_lock);
  slot = g_pw_ai_log_next;
  strncpy(g_pw_ai_log[slot], text, PW_AI_LOG_LEN - 1);
  g_pw_ai_log[slot][PW_AI_LOG_LEN - 1] = '\0';
  g_pw_ai_log_t[slot] = pw_ai_uptime_s();
  g_pw_ai_log_next = (slot + 1) % PW_AI_LOG_N;
  if (g_pw_ai_log_cnt < PW_AI_LOG_N)
    {
      g_pw_ai_log_cnt++;
    }

  nxmutex_unlock(&g_pw_ai_lock);

  syslog(LOG_INFO, "[phywear] ai-coach: %.160s\n", text);
}

/* 环形日志里第 idx 条（0 = 最新）对应的物理槽位 */

static int pw_ai_log_slot(int idx)
{
  return ((g_pw_ai_log_next - 1 - idx) % PW_AI_LOG_N + PW_AI_LOG_N) % PW_AI_LOG_N;
}

int pw_ai_log_count(void)
{
  int n;

  nxmutex_lock(&g_pw_ai_lock);
  n = g_pw_ai_log_cnt;
  nxmutex_unlock(&g_pw_ai_lock);
  return n;
}

const char *pw_ai_log_line(int idx)
{
  if (idx < 0 || idx >= pw_ai_log_count())
    {
      return NULL;
    }

  return g_pw_ai_log[pw_ai_log_slot(idx)];
}

long pw_ai_log_age_s(int idx)
{
  if (idx < 0 || idx >= pw_ai_log_count())
    {
      return -1;
    }

  return pw_ai_uptime_s() - g_pw_ai_log_t[pw_ai_log_slot(idx)];
}

bool pw_ai_agent_ready(void)
{
  return g_pw_ai_agent_ready;
}

#if defined(CONFIG_EXAMPLES_AI_AGENT_VELA)

#include <velaclaw/client.h>

static velaclaw_client_t *g_pw_ai_client;

/* Agent 的工具回执是给模型看的 JSON，直接显示在手表上看不懂。
 * 这里压成一行「人话」：优先取关键字段，取不到就剥掉 JSON 语法后截断。
 * 只用符号与数字，不引 i18n（避免为一句调试信息再加 10 条双语字符串）。 */

static void pw_ai_humanize(const char *raw, char *out, size_t out_size)
{
  const char *p;
  char screen[32];
  size_t n = 0;
  int items = 0;

  out[0] = '\0';
  if (raw == NULL || raw[0] == '\0')
    {
      return;
    }

  /* 切页/跑实验：{"accepted":true,"screen":"pendulum",...} → “OK - pendulum” */

  p = strstr(raw, "\"screen\":\"");
  if (p != NULL && strstr(raw, "\"accepted\":true") != NULL)
    {
      p += 10;   /* strlen("\"screen\":\"") */
      while (*p != '\0' && *p != '"' && n < sizeof(screen) - 1)
        {
          screen[n++] = *p++;
        }

      screen[n] = '\0';
      snprintf(out, out_size, "OK - %s", screen);
      return;
    }

  /* 实验列表：数 "name" 出现次数 → “list: 14 items” */

  for (p = raw; (p = strstr(p, "\"name\"")) != NULL; p += 6)
    {
      items++;
    }

  if (items > 0)
    {
      snprintf(out, out_size, "list: %d screens", items);
      return;
    }

  /* 兜底：剥掉 JSON 语法（花括号/方括号/引号），逗号变空格，截断显示 */

  {
    size_t o = 0;

    for (p = raw; *p != '\0' && o < out_size - 1; p++)
      {
        char c = *p;

        if (c == '{' || c == '}' || c == '[' || c == ']' || c == '"')
          {
            continue;
          }

        if (c == ',')
          {
            c = ' ';
          }
        else if (c == ':')
          {
            c = '=';
          }

        if (c == ' ' && o > 0 && out[o - 1] == ' ')
          {
            continue;
          }

        out[o++] = c;
      }

    out[o] = '\0';
  }
}

static void pw_ai_ask_cb(int status, const char *text, void *cookie)
{
  char human[140];
  char line[PW_AI_LOG_LEN];

  if (status == 0 && text != NULL && text[0] != '\0')
    {
      pw_ai_humanize(text, human, sizeof(human));
      snprintf(line, sizeof(line), "AI: %s", human);
    }
  else
    {
      snprintf(line, sizeof(line), "AI: (no reply, status=%d)", status);
    }

  pw_ai_note(line);
}

int pw_ai_ask(const char *text)
{
  velaclaw_ask_req_t req;

  if (text == NULL || text[0] == '\0')
    {
      return -EINVAL;
    }

  if (g_pw_ai_client == NULL)
    {
      g_pw_ai_client = velaclaw_client_open("phywear-coach");
    }

  if (g_pw_ai_client == NULL)
    {
      g_pw_ai_agent_ready = false;
      pw_ai_note("AI: not connected (start ai_agent first)");
      return -ENODEV;
    }

  req.text = text;
  req.timeout_ms = 0;               /* 异步：回复走 pw_ai_ask_cb */

  if (velaclaw_ask(g_pw_ai_client, &req, pw_ai_ask_cb, NULL) != 0)
    {
      g_pw_ai_agent_ready = false;
      pw_ai_note("AI: send failed");
      return -EIO;
    }

  g_pw_ai_agent_ready = true;
  return OK;
}

#else

int pw_ai_ask(const char *text)
{
  if (text == NULL || text[0] == '\0')
    {
      return -EINVAL;
    }

  g_pw_ai_agent_ready = false;
  pw_ai_note("AI: agent not built in this firmware");
  return -ENODEV;
}

#endif /* CONFIG_EXAMPLES_AI_AGENT_VELA */
