/****************************************************************************
 * apps/examples/phywear/pw_scope.c
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

#include <string.h>
#include <syslog.h>
#include <stdlib.h>

#include <lvgl/lvgl.h>

#include "pw_scope.h"

/****************************************************************************
 * Private Types
 ****************************************************************************/

struct pw_scope_s
{
  lv_obj_t       *img;
  lv_image_dsc_t  dsc;
  uint16_t       *buf;      /* RGB565 帧缓冲（w*h） */
  int             w;
  int             h;
  uint16_t        line;
  uint16_t        bg;
  uint16_t        axis;
  bool            has_axes;
  bool            used;     /* 槽位是否占用（LVGL 对象删除时自动释放） */
};

#define PW_SCOPE_MAX   6

static struct pw_scope_s g_scope[PW_SCOPE_MAX];
static int g_nscope;

/* 槽位分配：优先复用已释放的槽。
 *
 * 背景（真机隐患，2026-09-14 修复）：原来只有 g_nscope++ 没有释放路径，
 * 而每次进入带曲线的页面都会 pw_scope_create 一次 —— 同一个 boot 里进出
 * 6 次之后 create 返回 NULL，曲线静默消失。现在把 scope 绑到 LVGL 对象的
 * LV_EVENT_DELETE 上：页面被销毁时自动 lv_free 缓冲并归还槽位。 */

static struct pw_scope_s *scope_alloc(void)
{
  int i;

  for (i = 0; i < PW_SCOPE_MAX; i++)
    {
      if (!g_scope[i].used)
        {
          g_scope[i].used = true;
          if (i + 1 > g_nscope)
            {
              g_nscope = i + 1;
            }

          return &g_scope[i];
        }
    }

  return NULL;
}

static void scope_release(struct pw_scope_s *s)
{
  if (s == NULL || !s->used)
    {
      return;
    }

  syslog(LOG_INFO, "[phywear] pw_scope: release slot %d\n", (int)(s - g_scope));

  if (s->buf != NULL)
    {
      lv_free(s->buf);
      s->buf = NULL;
    }

  s->img = NULL;
  s->used = false;

  while (g_nscope > 0 && !g_scope[g_nscope - 1].used)
    {
      g_nscope--;
    }
}

static void pw_scope_del_cb(lv_event_t *e)
{
  scope_release((struct pw_scope_s *)lv_event_get_user_data(e));
}

/****************************************************************************
 * Private Functions
 ****************************************************************************/

static inline uint16_t rgb565(lv_color_t c)
{
  return (uint16_t)(((c.red & 0xF8) << 8) |
                    ((c.green & 0xFC) << 3) |
                    (c.blue >> 3));
}

static inline void put_px(struct pw_scope_s *s, int x, int y, uint16_t c)
{
  if (x >= 0 && x < s->w && y >= 0 && y < s->h)
    {
      s->buf[y * s->w + x] = c;
    }
}

/* 由 lv_image 对象反查内部槽位（未占用/未命中返回 NULL） */

static struct pw_scope_s *scope_find(lv_obj_t *img)
{
  int i;

  for (i = 0; i < PW_SCOPE_MAX; i++)
    {
      if (g_scope[i].used && g_scope[i].img == img)
        {
          return &g_scope[i];
        }
    }

  return NULL;
}

/* 背景提亮一档：未显式设置轴线颜色时用作泳道分隔线 */

static inline uint16_t lift565(uint16_t c)
{
  uint16_t r = (c >> 11) & 0x1f;
  uint16_t g = (c >> 5) & 0x3f;
  uint16_t b = c & 0x1f;

  r = (r + 7 > 0x1f) ? 0x1f : (uint16_t)(r + 7);
  g = (g + 14 > 0x3f) ? 0x3f : (uint16_t)(g + 14);
  b = (b + 7 > 0x1f) ? 0x1f : (uint16_t)(b + 7);

  return (uint16_t)((r << 11) | (g << 5) | b);
}

static void draw_axes(struct pw_scope_s *s)
{
  int x;
  int mid = s->h / 2;

  for (x = 0; x < s->w; x++)
    {
      put_px(s, x, mid, s->axis);
      put_px(s, x, 0, s->axis);
      put_px(s, x, s->h - 1, s->axis);
    }

  for (int y = 0; y < s->h; y++)
    {
      put_px(s, 0, y, s->axis);
      put_px(s, s->w - 1, y, s->axis);
    }
}

static void draw_line(struct pw_scope_s *s, int x0, int y0,
                      int x1, int y1, uint16_t c)
{
  int dx = abs(x1 - x0);
  int sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  for (;;)
    {
      put_px(s, x0, y0, c);
      if (x0 == x1 && y0 == y1)
        {
          break;
        }

      {
        int e2 = 2 * err;
        if (e2 >= dy)
          {
            err += dy;
            x0 += sx;
          }

        if (e2 <= dx)
          {
            err += dx;
            y0 += sy;
          }
      }
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

lv_obj_t *pw_scope_create(lv_obj_t *parent, int w, int h,
                          lv_color_t line, lv_color_t bg)
{
  struct pw_scope_s *s;
  uint32_t px;

  if (w <= 0 || h <= 0 || w > 1024 || h > 1024)
    {
      return NULL;
    }

  s = scope_alloc();
  if (s == NULL)
    {
      syslog(LOG_WARNING, "[phywear] pw_scope: out of slots (%d in use)\n",
             PW_SCOPE_MAX);
      return NULL;
    }

  syslog(LOG_INFO, "[phywear] pw_scope: slot %d, %d/%d in use\n",
         (int)(s - g_scope), g_nscope, PW_SCOPE_MAX);

  memset(s, 0, sizeof(*s));
  s->used = true;

  s->w = w;
  s->h = h;
  s->line = rgb565(line);
  s->bg = rgb565(bg);

  px = (uint32_t)w * (uint32_t)h;
  s->buf = lv_malloc(px * 2);
  if (s->buf == NULL)
    {
      return NULL;
    }

  for (uint32_t i = 0; i < px; i++)
    {
      s->buf[i] = s->bg;
    }

  s->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  s->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  s->dsc.header.flags = 0;
  s->dsc.header.w = w;
  s->dsc.header.h = h;
  s->dsc.header.stride = w * 2;
  s->dsc.data_size = px * 2;
  s->dsc.data = (const uint8_t *)s->buf;

  s->img = lv_image_create(parent);
  lv_image_set_src(s->img, &s->dsc);
  lv_obj_set_size(s->img, w, h);
  lv_obj_set_style_bg_opa(s->img, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(s->img, 0, 0);
  lv_obj_set_style_pad_all(s->img, 0, 0);

  /* 页面（父对象）被删除 → 自动释放缓冲与槽位 */

  lv_obj_add_event_cb(s->img, pw_scope_del_cb, LV_EVENT_DELETE, s);

  return s->img;
}

void pw_scope_set_data(lv_obj_t *scope, FAR const float *y, int n)
{
  struct pw_scope_s *s;
  uint32_t px;
  int i;
  int x0;
  int y0;

  if (scope == NULL || y == NULL || n < 2)
    {
      return;
    }

  /* 由返回的 img 对象反查内部结构：比较所有实例 */

  s = NULL;
  for (i = 0; i < g_nscope; i++)
    {
      if (g_scope[i].used && g_scope[i].img == scope)
        {
          s = &g_scope[i];
          break;
        }
    }

  if (s == NULL)
    {
      return;
    }

  px = (uint32_t)s->w * (uint32_t)s->h;

  for (i = 0; i < (int)px; i++)
    {
      s->buf[i] = s->bg;
    }

  if (s->has_axes)
    {
      draw_axes(s);
    }

  for (i = 0; i < n; i++)
    {
      float v = y[i];
      int xi = (n > 1) ? (i * (s->w - 1) / (n - 1)) : 0;
      int yi;

      if (v > 1.0f)
        {
          v = 1.0f;
        }
      else if (v < -1.0f)
        {
          v = -1.0f;
        }

      /* v∈[-1,1] → y 向下（屏幕 y 增为向下），留 4% 边距 */

      yi = (int)((0.5f - v * 0.48f) * (s->h - 1) + 0.5f);

      if (i == 0)
        {
          x0 = xi;
          y0 = yi;
        }
      else
        {
          draw_line(s, x0, y0, xi, yi, s->line);
          x0 = xi;
          y0 = yi;
        }
    }

  lv_obj_invalidate(s->img);
}

void pw_scope_set_points(lv_obj_t *scope, FAR const float *x,
                         FAR const float *y, int n)
{
  struct pw_scope_s *s;
  uint32_t px;
  int i;

  if (scope == NULL || x == NULL || y == NULL || n <= 0)
    {
      return;
    }

  s = NULL;
  for (i = 0; i < g_nscope; i++)
    {
      if (g_scope[i].img == scope)
        {
          s = &g_scope[i];
          break;
        }
    }

  if (s == NULL)
    {
      return;
    }

  px = (uint32_t)s->w * (uint32_t)s->h;
  for (i = 0; i < (int)px; i++)
    {
      s->buf[i] = s->bg;
    }

  if (s->has_axes)
    {
      draw_axes(s);
    }

  for (i = 0; i < n; i++)
    {
      float vx = x[i];
      float vy = y[i];
      int xi;
      int yi;
      int dx;
      int dy;

      if (vx > 1.0f)
        {
          vx = 1.0f;
        }
      else if (vx < -1.0f)
        {
          vx = -1.0f;
        }

      if (vy > 1.0f)
        {
          vy = 1.0f;
        }
      else if (vy < -1.0f)
        {
          vy = -1.0f;
        }

      xi = (int)((vx + 1.0f) * 0.5f * (s->w - 1) + 0.5f);
      yi = (int)((0.5f - vy * 0.5f) * (s->h - 1) + 0.5f);

      /* 画 3×3 圆点，增强可见性 */

      for (dy = -1; dy <= 1; dy++)
        {
          for (dx = -1; dx <= 1; dx++)
            {
              if (dx * dx + dy * dy <= 1)
                {
                  put_px(s, xi + dx, yi + dy, s->line);
                }
            }
        }
    }

  lv_obj_invalidate(s->img);
}

void pw_scope_axes(lv_obj_t *scope, lv_color_t color)
{
  struct pw_scope_s *s;
  int i;

  if (scope == NULL)
    {
      return;
    }

  s = NULL;
  for (i = 0; i < g_nscope; i++)
    {
      if (g_scope[i].img == scope)
        {
          s = &g_scope[i];
          break;
        }
    }

  if (s == NULL)
    {
      return;
    }

  s->axis = rgb565(color);
  s->has_axes = true;
}

void pw_scope_set_lanes_i16(lv_obj_t *scope, FAR const int16_t *hist,
                            int nser, int n, float inv,
                            FAR const lv_color_t *colors)
{
  struct pw_scope_s *s = scope_find(scope);
  uint16_t grid;
  uint32_t px;
  int lane_h;
  int i;
  int k;

  if (s == NULL || hist == NULL || colors == NULL ||
      nser <= 0 || nser > 8 || n < 2)
    {
      return;
    }

  if (inv <= 0.0f)
    {
      inv = 1.0f;
    }

  px = (uint32_t)s->w * (uint32_t)s->h;
  for (i = 0; i < (int)px; i++)
    {
      s->buf[i] = s->bg;
    }

  grid = s->has_axes ? s->axis : lift565(s->bg);
  lane_h = s->h / nser;

  for (i = 0; i < nser; i++)
    {
      FAR const int16_t *row = hist + (size_t)i * (size_t)n;
      uint16_t c = rgb565(colors[i]);
      int top = i * lane_h;
      int bot = (i == nser - 1) ? s->h : top + lane_h;
      int lh = bot - top;
      int x0 = 0;
      int y0 = 0;

      /* 泳道之间的分隔线 */

      if (i > 0)
        {
          for (k = 0; k < s->w; k++)
            {
              put_px(s, k, top, grid);
            }
        }

      for (k = 0; k < n; k++)
        {
          float v = (float)row[k] * inv;
          /* 横向量程留出外框两列（1..w-2），否则最新样本会被外框盖掉 */
          int xi = 1 + k * (s->w - 3) / (n - 1);
          int yi;

          if (v > 1.0f)
            {
              v = 1.0f;
            }
          else if (v < -1.0f)
            {
              v = -1.0f;
            }

          /* 泳道内留 5% 上下边距，曲线不贴分隔线 */

          yi = top + (int)((0.5f - v * 0.45f) * (lh - 1) + 0.5f);

          if (k == 0)
            {
              x0 = xi;
              y0 = yi;
            }
          else
            {
              draw_line(s, x0, y0, xi, yi, c);
              x0 = xi;
              y0 = yi;
            }
        }
    }

  /* 外框（1px），最后画以裁掉溢出像素 */

  for (k = 0; k < s->w; k++)
    {
      put_px(s, k, 0, grid);
      put_px(s, k, s->h - 1, grid);
    }

  for (k = 0; k < s->h; k++)
    {
      put_px(s, 0, k, grid);
      put_px(s, s->w - 1, k, grid);
    }

  lv_obj_invalidate(s->img);
}
