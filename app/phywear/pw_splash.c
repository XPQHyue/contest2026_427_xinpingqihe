/****************************************************************************
 * apps/examples/phywear/pw_splash.c
 *
 * [AGENT-VM] 开机动画 BG1（LOGO 版），2026-11-04 按 T-0006 交付单实现。
 *
 * 版式唯一真源：`PROPOSALS/work/2026-11-03-2356-splash-bg1/tools/showcase_bg1.py`
 * 顶部常量。本文件是把那份版式翻译成 LVGL 调用，**坐标不要在这里手改**。
 *
 * 分层：
 *   底层    `pw_bg`（195x225 RGB565，`lv_image_set_scale(512)` 即 2 倍）铺满 390x450
 *   上层    标题 / 副标 / 进度轨与填充 / powered by / 两个标识 / 落款
 *
 * 时间轴（总 1400 ms，由调用方传入 ms）：
 *   0    – 35%   背景亮度 0.25→1.0，整层 opacity 0→1
 *   35%  – 82%   进度填充 0→210 px
 *   82%  – 100%  整层淡出
 *
 * `pw_splash_show()` 仍然手推 `lv_timer_handler()`：不依赖 lv_anim，
 * 避免与主循环的定时器生命周期纠缠（沿用 WORK 端原实现的取舍）。
 *
 * 位图是 `const` 全局符号（落 flash、XIP 直读），**不要 free**；
 * 本固件没有 PNG 解码器，所以这里只有 `lv_image_dsc_t`，没有任何文件路径。
 *
 * Licensed to the Apache Software Foundation (ASF) under one or more
 * contributor license agreements.  See the NOTICE file distributed with
 * this work for additional information regarding copyright ownership.
 * The ASF licenses this file to you under the Apache License, Version 2.0.
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdio.h>
#include <unistd.h>

#include <lvgl/lvgl.h>

#include "pw_splash.h"

#ifdef CONFIG_EXAMPLES_PHYWEAR_SPLASH

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* 版式常量（与交付单 §5 一一对应，改这里等于改版式） */

#define PW_SPLASH_W          390
#define PW_SPLASH_H          450

#define PW_SPLASH_Y_TITLE    74
#define PW_SPLASH_Y_SUBTITLE 150   /* 必须 >=150：`y` 的下伸部垂到 137 */
#define PW_SPLASH_Y_TRACK    186
#define PW_SPLASH_Y_POWERED  204
#define PW_SPLASH_Y_LOGOS    218
#define PW_SPLASH_Y_MARK     302

#define PW_SPLASH_TRACK_W    210
#define PW_SPLASH_TRACK_H    2

#define PW_SPLASH_LOGO_SZ    62
#define PW_SPLASH_LOGO_GAP   22
#define PW_SPLASH_LOGO_X0    122

/* 时间轴切分（百分比） */

#define PW_SPLASH_FADE_END   35
#define PW_SPLASH_PROG_END   82

#define PW_SPLASH_BG         0x000000
#define PW_SPLASH_FG         0xFFFFFF
#define PW_SPLASH_SUB        0x7A8AA0
#define PW_SPLASH_TRACK      0x333333
#define PW_SPLASH_ACCENT     0x35E6E8
#define PW_SPLASH_DIM        0x8A8A8A

/* 背景起始亮度 0.25 */

#define PW_SPLASH_BG_OPA_MIN 64

/****************************************************************************
 * Private Data
 ****************************************************************************/

extern const lv_image_dsc_t pw_bg;
extern const lv_image_dsc_t pw_logo_mi;
extern const lv_image_dsc_t pw_logo_ov;

extern const lv_font_t pw_font_20;

static lv_obj_t *g_splash_scr;

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/* 建一个无样式容器：让淡出只改一个对象的 opa，而不是逐个控件改 */

static lv_obj_t *pw_splash_layer(lv_obj_t *parent)
{
  lv_obj_t *layer = lv_obj_create(parent);

  lv_obj_remove_style_all(layer);
  lv_obj_remove_flag(layer, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(layer, PW_SPLASH_W, PW_SPLASH_H);
  lv_obj_set_pos(layer, 0, 0);

  return layer;
}

static lv_obj_t *pw_splash_label(lv_obj_t *parent, const char *text,
                                 const lv_font_t *font, uint32_t color,
                                 int y)
{
  lv_obj_t *lab = lv_label_create(parent);

  lv_label_set_text(lab, text);
  lv_obj_set_style_text_font(lab, font, 0);
  lv_obj_set_style_text_color(lab, lv_color_hex(color), 0);
  lv_obj_set_style_bg_opa(lab, LV_OPA_TRANSP, 0);
  lv_obj_align(lab, LV_ALIGN_TOP_MID, 0, y);

  return lab;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

void pw_splash_show(int ms)
{
  lv_obj_t *layer;
  lv_obj_t *bg;
  lv_obj_t *fill;
  lv_obj_t *logo_mi;
  lv_obj_t *logo_ov;
  lv_obj_t *track;
  uint32_t  t0;
  int       elapsed;

  if (ms <= 0)
    {
      return;
    }

  g_splash_scr = lv_obj_create(NULL);
  lv_obj_remove_flag(g_splash_scr, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_style_bg_color(g_splash_scr, lv_color_hex(PW_SPLASH_BG), 0);
  lv_obj_set_style_bg_opa(g_splash_scr, LV_OPA_COVER, 0);
  lv_obj_set_style_pad_all(g_splash_scr, 0, 0);
  lv_obj_set_style_border_width(g_splash_scr, 0, 0);

  layer = pw_splash_layer(g_splash_scr);

  /* ── 背景层：195x225 放大 2 倍 = 390x450，铺满 ────────────────────── */

  bg = lv_image_create(layer);
  lv_image_set_src(bg, &pw_bg);

  /* 对象尺寸要按**放大后**的尺寸给（390x450），并把变换原点钉在左上角：
   * LVGL 9 的 image 默认绕中心缩放，且绘制会被裁在对象自身尺寸内，只设
   * `set_scale(512)` 会得到"195x225 的窗口里画了一块 2 倍图"的效果。 */

  /* 位图本身就是 390x450（生成期已放大），这里 1:1 画即可。
   * 不要在设备端 set_scale：LVGL 9 的 image 变换是绕 pivot 的，
   * 全屏铺满时容易出现偏移/只画一角，2026-11-04 已踩过两次。 */

  lv_obj_set_pos(bg, 0, 0);
  lv_obj_set_style_opa(bg, PW_SPLASH_BG_OPA_MIN, 0);

  /* ── 标题与副标 ────────────────────────────────────────────────────── */

  pw_splash_label(layer, "PhyWear", &lv_font_montserrat_48,
                  PW_SPLASH_FG, PW_SPLASH_Y_TITLE);
  pw_splash_label(layer, "腕上物理工坊", &pw_font_20,
                  PW_SPLASH_SUB, PW_SPLASH_Y_SUBTITLE);

  /* ── 进度轨道 + 填充 ──────────────────────────────────────────────── */

  track = lv_obj_create(layer);
  lv_obj_remove_flag(track, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(track, PW_SPLASH_TRACK_W, PW_SPLASH_TRACK_H);
  lv_obj_set_style_bg_color(track, lv_color_hex(PW_SPLASH_TRACK), 0);
  lv_obj_set_style_bg_opa(track, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(track, 0, 0);
  lv_obj_set_style_radius(track, PW_SPLASH_TRACK_H / 2, 0);
  lv_obj_set_style_pad_all(track, 0, 0);
  lv_obj_align(track, LV_ALIGN_TOP_MID, 0, PW_SPLASH_Y_TRACK);

  fill = lv_obj_create(layer);
  lv_obj_remove_flag(fill, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_size(fill, 1, PW_SPLASH_TRACK_H);
  lv_obj_set_style_bg_color(fill, lv_color_hex(PW_SPLASH_ACCENT), 0);
  lv_obj_set_style_bg_opa(fill, LV_OPA_COVER, 0);
  lv_obj_set_style_border_width(fill, 0, 0);
  lv_obj_set_style_radius(fill, PW_SPLASH_TRACK_H / 2, 0);
  lv_obj_set_style_pad_all(fill, 0, 0);
  lv_obj_align_to(fill, track, LV_ALIGN_TOP_LEFT, 0, 0);

  /* ── powered by + 两个标识 + 落款 ─────────────────────────────────── */

  pw_splash_label(layer, "powered by", &lv_font_montserrat_14,
                  PW_SPLASH_SUB, PW_SPLASH_Y_POWERED);

  logo_mi = lv_image_create(layer);
  lv_image_set_src(logo_mi, &pw_logo_mi);   /* 62x62，勿 set_scale */
  lv_obj_set_pos(logo_mi, PW_SPLASH_LOGO_X0, PW_SPLASH_Y_LOGOS);

  logo_ov = lv_image_create(layer);
  lv_image_set_src(logo_ov, &pw_logo_ov);
  lv_obj_set_pos(logo_ov,
                 PW_SPLASH_LOGO_X0 + PW_SPLASH_LOGO_SZ + PW_SPLASH_LOGO_GAP,
                 PW_SPLASH_Y_LOGOS);

  pw_splash_label(layer, "XINPINGQIHE 427", &lv_font_montserrat_14,
                  PW_SPLASH_DIM, PW_SPLASH_Y_MARK);

  lv_screen_load(g_splash_scr);

  /* ── 手推动画 ─────────────────────────────────────────────────────── */

  t0 = lv_tick_get();
  for (;;)
    {
      int fade_ms = ms * PW_SPLASH_FADE_END / 100;
      int prog_ms = ms * PW_SPLASH_PROG_END / 100;
      int w;

      elapsed = (int)(lv_tick_get() - t0);
      if (elapsed >= ms)
        {
          break;
        }

      if (elapsed < fade_ms)
        {
          int opa = PW_SPLASH_BG_OPA_MIN +
                    (255 - PW_SPLASH_BG_OPA_MIN) * elapsed / fade_ms;

          lv_obj_set_style_opa(bg, opa, 0);
          lv_obj_set_style_opa(layer, 255 * elapsed / fade_ms, 0);
        }
      else if (elapsed < prog_ms)
        {
          lv_obj_set_style_opa(bg, 255, 0);
          lv_obj_set_style_opa(layer, 255, 0);

          w = (elapsed - fade_ms) * PW_SPLASH_TRACK_W / (prog_ms - fade_ms);
          lv_obj_set_width(fill, w > 0 ? w : 1);
        }
      else
        {
          /* 末段淡出：主界面 FADE_ON 与这里重叠，不留黑帧 */

          lv_obj_set_style_opa(layer, 255 * (ms - elapsed) / (ms - prog_ms), 0);
        }

      lv_timer_handler();

      usleep(16 * 1000);
    }

  printf("[splash] done in %d ms\n", ms);
}

void pw_splash_release(void)
{
  if (g_splash_scr != NULL)
    {
      lv_obj_delete(g_splash_scr);
      g_splash_scr = NULL;
    }
}

#endif /* CONFIG_EXAMPLES_PHYWEAR_SPLASH */
