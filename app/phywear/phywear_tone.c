/****************************************************************************
 * apps/examples/phywear/phywear_tone.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * 声学板块：音频发生器（Tone generator）。
 *
 * 用板载扬声器（/dev/spk0，见 sf32lb52_spk.c）发一段连续正弦：频率 40 Hz ～
 * 4 kHz、音量 -36 ～ +54 dB。配合麦克风频谱页可以做：房间/表壳共振、拍频、
 * 驻波、以及"用已知频率校准麦克风频谱"。
 *
 * 布局（390×450）：
 *   0..54     顶栏
 *   70..150   频率行（- / 数值 / +）
 *   170..250  音量行（- / 数值 / +）
 *   270..330  四个频率预设
 *   340..404  播放/停止大按钮
 *   404..450  提示文字
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdio.h>

#include <lvgl/lvgl.h>

#include "phywear_ui.h"
#include "phywear_i18n.h"
#include "pw_tone.h"

/****************************************************************************
 * Private Data
 ****************************************************************************/

static lv_obj_t *g_tone_freq_lab;
static lv_obj_t *g_tone_vol_lab;
static lv_obj_t *g_tone_btn_lab;
static lv_obj_t *g_tone_state;

static int g_tone_freq = 440;
static int g_tone_vol  = 12;

static const int g_tone_preset[] = { 220, 440, 880, 1760 };

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static void tone_refresh(void)
{
  char buf[32];

  if (g_tone_freq_lab != NULL)
    {
      snprintf(buf, sizeof(buf), "%d Hz", g_tone_freq);
      lv_label_set_text(g_tone_freq_lab, buf);
    }

  if (g_tone_vol_lab != NULL)
    {
      snprintf(buf, sizeof(buf), "%+d dB", g_tone_vol);
      lv_label_set_text(g_tone_vol_lab, buf);
    }

  if (g_tone_btn_lab != NULL)
    {
      lv_label_set_text(g_tone_btn_lab,
                        pw_tone_playing() ? PW_STR(SPK_STOP)
                                          : PW_STR(SPK_PLAY));
    }

  if (g_tone_state != NULL)
    {
      if (!pw_tone_available())
        {
          lv_label_set_text(g_tone_state, PW_STR(SPK_NO_HW));
        }
      else if (pw_tone_playing())
        {
          snprintf(buf, sizeof(buf), "%s  %d Hz", PW_STR(SPK_PLAYING),
                   g_tone_freq);
          lv_label_set_text(g_tone_state, buf);
        }
      else
        {
          lv_label_set_text(g_tone_state, PW_STR(SPK_IDLE));
        }
    }
}

/* 正在播放时改参数：立即用新参数重发（驱动内部只重填缓冲，不重启 DMA） */

static void tone_apply(void)
{
  if (pw_tone_playing())
    {
      pw_tone_play((uint32_t)g_tone_freq, g_tone_vol);
    }

  tone_refresh();
}

static void tone_step_cb(lv_event_t *e)
{
  int delta = (int)(intptr_t)lv_event_get_user_data(e);
  int next = g_tone_freq + delta * 20;

  if (next < 40)
    {
      next = 40;
    }
  else if (next > 4000)
    {
      next = 4000;
    }

  g_tone_freq = next;
  tone_apply();
}

static void tone_preset_cb(lv_event_t *e)
{
  g_tone_freq = (int)(intptr_t)lv_event_get_user_data(e);
  tone_apply();
}

static void tone_vol_cb(lv_event_t *e)
{
  int delta = (int)(intptr_t)lv_event_get_user_data(e);
  int next = g_tone_vol + delta * 6;

  if (next < -36)
    {
      next = -36;
    }
  else if (next > 30)
    {
      next = 30;
    }

  g_tone_vol = next;
  tone_apply();
}

static void tone_play_cb(lv_event_t *e)
{
  (void)e;

  if (pw_tone_playing())
    {
      pw_tone_stop();
    }
  else
    {
      pw_tone_play((uint32_t)g_tone_freq, g_tone_vol);
    }

  tone_refresh();
}

/* 离开页面必须停声，否则会一直响 */

static void tone_del_cb(lv_event_t *e)
{
  (void)e;

  pw_tone_stop();
}

static lv_obj_t *tone_make_btn(lv_obj_t *parent, int x, int y, int w, int h,
                               const char *text, lv_color_t color,
                               lv_event_cb_t cb, void *user)
{
  lv_obj_t *btn = pw_card_new(parent, w, h, PW_COL_CARD);
  lv_obj_t *lab;

  lv_obj_set_pos(btn, x, y);
  lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, user);
  lab = pw_label_new(btn, text, PW_FNT_MED, color);
  lv_obj_center(lab);
  return btn;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

lv_obj_t *pw_tone_screen(void)
{
  lv_obj_t *scr;
  lv_obj_t *cont;
  lv_obj_t *lab;
  int i;

  scr = pw_scr_new();
  cont = pw_topbar(scr, PW_STR(EXP_TONE_NAME));

  /* 频率行 */

  lab = pw_label_new(cont, PW_STR(TONE_FREQ_LABEL), PW_FNT_SMALL,
                     PW_COL_DIM);
  lv_obj_set_pos(lab, 24, 8);

  tone_make_btn(cont, 24, 32, 64, 64, "-", PW_COL_TEXT, tone_step_cb,
                (void *)(intptr_t)-1);
  tone_make_btn(cont, 302, 32, 64, 64, "+", PW_COL_TEXT, tone_step_cb,
                (void *)(intptr_t)1);

  g_tone_freq_lab = pw_label_new(cont, "440 Hz", PW_FNT_LARGE,
                                 PW_ACC_ACOU);
  lv_obj_set_pos(g_tone_freq_lab, 120, 44);

  /* 音量行 */

  lab = pw_label_new(cont, PW_STR(TONE_VOL_LABEL), PW_FNT_SMALL,
                     PW_COL_DIM);
  lv_obj_set_pos(lab, 24, 104);

  tone_make_btn(cont, 24, 128, 64, 64, "-", PW_COL_TEXT, tone_vol_cb,
                (void *)(intptr_t)-1);
  tone_make_btn(cont, 302, 128, 64, 64, "+", PW_COL_TEXT, tone_vol_cb,
                (void *)(intptr_t)1);

  g_tone_vol_lab = pw_label_new(cont, "+12 dB", PW_FNT_LARGE, PW_COL_TEXT);
  lv_obj_set_pos(g_tone_vol_lab, 120, 140);

  /* 频率预设 */

  for (i = 0; i < (int)(sizeof(g_tone_preset) / sizeof(g_tone_preset[0])); i++)
    {
      char buf[16];

      snprintf(buf, sizeof(buf), "%d", g_tone_preset[i]);
      tone_make_btn(cont, 24 + i * 88, 204, 80, 52, buf, PW_COL_TEXT,
                    tone_preset_cb, (void *)(intptr_t)g_tone_preset[i]);
    }

  /* 播放/停止 */

  {
    lv_obj_t *btn = pw_card_new(cont, 342, 60, PW_ACC_ACOU);

    lv_obj_set_pos(btn, 24, 266);
    lv_obj_add_event_cb(btn, tone_play_cb, LV_EVENT_CLICKED, NULL);
    g_tone_btn_lab = pw_label_new(btn, PW_STR(SPK_PLAY), PW_FNT_MED,
                                  lv_color_hex(0x101418));
    lv_obj_center(g_tone_btn_lab);
  }

  /* 状态 + 提示 */

  g_tone_state = pw_label_new(cont, "", PW_FNT_SMALL, PW_COL_DIM);
  lv_obj_set_pos(g_tone_state, 24, 336);

  lab = pw_label_new(cont, PW_STR(TONE_HINT), PW_FNT_BODY, PW_COL_FAINT);
  lv_obj_set_pos(lab, 24, 362);
  lv_obj_set_width(lab, 342);
  lv_label_set_long_mode(lab, LV_LABEL_LONG_WRAP);

  lv_obj_add_event_cb(scr, tone_del_cb, LV_EVENT_DELETE, NULL);

  tone_refresh();
  return scr;
}
