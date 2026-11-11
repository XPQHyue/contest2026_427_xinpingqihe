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

/* [SIZE-PROBE] 只用来测镜像大小是否影响启动，勿留 */
const char pw_pad_probe[600] = { 1 };
const char *pw_pad_probe_use(void) { return pw_pad_probe; }


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
  { "imu",          "IMU attitude: level + gyro-bias / six-face / magnetometer calibration" },
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

/****************************************************************************
 * 实验结果登记
 ****************************************************************************/

static struct pw_ai_result_s g_pw_ai_res[PW_AI_RESULT_N];

static struct pw_ai_result_s *pw_ai_result_slot(const char *kind)
{
  int i;
  int free_i = -1;

  for (i = 0; i < PW_AI_RESULT_N; i++)
    {
      if (g_pw_ai_res[i].kind[0] != '\0' &&
          strcmp(g_pw_ai_res[i].kind, kind) == 0)
        {
          return &g_pw_ai_res[i];
        }

      if (free_i < 0 && g_pw_ai_res[i].kind[0] == '\0')
        {
          free_i = i;
        }
    }

  if (free_i < 0)
    {
      /* 满了：轮转覆盖最旧的一个（seq 最小的） */

      free_i = 0;

      for (i = 1; i < PW_AI_RESULT_N; i++)
        {
          if (g_pw_ai_res[i].seq < g_pw_ai_res[free_i].seq)
            {
              free_i = i;
            }
        }
    }

  return &g_pw_ai_res[free_i];
}

void pw_ai_publish_result(const char *kind, float value, const char *unit,
                          float aux, const char *detail)
{
  struct pw_ai_result_s *r;

  if (kind == NULL || kind[0] == '\0')
    {
      return;
    }

  nxmutex_lock(&g_pw_ai_lock);

  r = pw_ai_result_slot(kind);
  strncpy(r->kind, kind, PW_AI_RESULT_KIND_MAX - 1);
  r->kind[PW_AI_RESULT_KIND_MAX - 1] = '\0';
  strncpy(r->unit, (unit != NULL) ? unit : "", PW_AI_RESULT_UNIT_MAX - 1);
  r->unit[PW_AI_RESULT_UNIT_MAX - 1] = '\0';
  strncpy(r->detail, (detail != NULL) ? detail : "",
          PW_AI_RESULT_TEXT_MAX - 1);
  r->detail[PW_AI_RESULT_TEXT_MAX - 1] = '\0';
  r->value = value;
  r->aux = aux;
  r->seq++;

  nxmutex_unlock(&g_pw_ai_lock);

  syslog(LOG_INFO, "[phywear] result %s: %.4f %s (%s)\n",
         kind, (double)value, unit != NULL ? unit : "", 
         detail != NULL ? detail : "");
}

int pw_ai_result_seq(const char *kind)
{
  int seq = 0;
  int i;

  if (kind == NULL)
    {
      return 0;
    }

  nxmutex_lock(&g_pw_ai_lock);
  for (i = 0; i < PW_AI_RESULT_N; i++)
    {
      if (g_pw_ai_res[i].kind[0] != '\0' &&
          strcmp(g_pw_ai_res[i].kind, kind) == 0)
        {
          seq = g_pw_ai_res[i].seq;
          break;
        }
    }

  nxmutex_unlock(&g_pw_ai_lock);
  return seq;
}

bool pw_ai_result_get(const char *kind, FAR struct pw_ai_result_s *out)
{
  bool found = false;
  int i;

  if (kind == NULL || out == NULL)
    {
      return false;
    }

  nxmutex_lock(&g_pw_ai_lock);
  for (i = 0; i < PW_AI_RESULT_N; i++)
    {
      if (g_pw_ai_res[i].kind[0] != '\0' &&
          strcmp(g_pw_ai_res[i].kind, kind) == 0)
        {
          *out = g_pw_ai_res[i];
          found = true;
          break;
        }
    }

  nxmutex_unlock(&g_pw_ai_lock);
  return found;
}

static void (*g_pw_ai_text_sink)(const char *text);

void pw_ai_set_text_sink(void (*fn)(const char *text))
{
  g_pw_ai_text_sink = fn;
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

  if (g_pw_ai_text_sink != NULL && text != NULL)
    {
      g_pw_ai_text_sink(text);
    }
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

/* [PW_AI_AUTORUN] 默认关闭；状态落在 /data/pw_ai_auto（tmpfs），
 * 让 GUI 进程、串口 CLI、设置页开关看到同一份值。
 * 评委演示时晃一下表就跳页会很烦，所以默认 0，必须在设置页显式打开。 */

#define PW_AI_AUTORUN_FLAGS "/data/pw_ai_auto"

static bool g_pw_ai_auto;
static bool g_pw_ai_auto_loaded;

static void pw_ai_auto_load(void)
{
  FILE *f = fopen(PW_AI_AUTORUN_FLAGS, "r");

  if (f == NULL)
    {
      return;
    }

  g_pw_ai_auto = (fgetc(f) == '1');
  g_pw_ai_auto_loaded = true;
  fclose(f);
}

bool pw_ai_auto_get(void)
{
  if (!g_pw_ai_auto_loaded)
    {
      pw_ai_auto_load();
      g_pw_ai_auto_loaded = true;
    }

  return g_pw_ai_auto;
}

void pw_ai_auto_set(bool on)
{
  FILE *f;

  g_pw_ai_auto = on;
  g_pw_ai_auto_loaded = true;

  f = fopen(PW_AI_AUTORUN_FLAGS, "w");
  if (f != NULL)
    {
      fputc(on ? '1' : '0', f);
      fclose(f);
    }
}


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

  /* 工具自带 UI 文案时优先用它（例如实验结果的"已自动测重力加速度: g = ..."），
   * 这样数值不会被压掉。 */

  p = strstr(raw, "\"human\":\"");
  if (p != NULL)
    {
      p += 9;   /* strlen("\"human\":\"") */

      while (*p != '\0' && *p != '"' && n < out_size - 1)
        {
          out[n++] = *p++;
        }

      out[n] = '\0';

      if (n > 0)
        {
          return;
        }
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
