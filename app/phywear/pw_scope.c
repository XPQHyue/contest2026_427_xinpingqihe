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
};

#define PW_SCOPE_MAX   6

static struct pw_scope_s g_scope[PW_SCOPE_MAX];
static int g_nscope;

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

  if (g_nscope >= PW_SCOPE_MAX)
    {
      return NULL;
    }

  if (w <= 0 || h <= 0 || w > 1024 || h > 1024)
    {
      return NULL;
    }

  s = &g_scope[g_nscope++];
  memset(s, 0, sizeof(*s));

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
