/****************************************************************************
 * apps/examples/phywear/pw_graph.c
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

#include "pw_graph.h"

#define GRAPH_MAX   256
#define SERIES_MAX  4    /* 多序列上限（roadmap §7.3 第 1 条） */

struct pw_series_s
{
  float     x[GRAPH_MAX];
  float     y[GRAPH_MAX];
  int       n;
  uint16_t  color;      /* RGB565 */
};

struct pw_graph_s
{
  lv_obj_t       *img;
  lv_image_dsc_t  dsc;
  uint16_t       *buf;
  int             w;
  int             h;
  int             dots;
  uint16_t        line;   /* 默认序列颜色（RGB565） */
  uint16_t        bg;
  uint16_t        axis;
  uint16_t        grid;   /* 淡网格线颜色（RGB565） */
  int             grid_on;   /* 1=画网格/刻度 */

  struct pw_series_s series[SERIES_MAX];
  int             nseries;

  float           xmin;
  float           xmax;
  float           ymin;
  float           ymax;
  int             auto_fit;

  lv_point_t      last;       /* 平移拖动上次点 */
  int             pan_down;
};

static inline uint16_t g_rgb565(lv_color_t c)
{
  return (uint16_t)(((c.red & 0xF8) << 8) |
                    ((c.green & 0xFC) << 3) |
                    (c.blue >> 3));
}

static inline void g_px(struct pw_graph_s *g, int x, int y, uint16_t c)
{
  if (x >= 0 && x < g->w && y >= 0 && y < g->h)
    {
      g->buf[y * g->w + x] = c;
    }
}

static void g_line(struct pw_graph_s *g, int x0, int y0,
                   int x1, int y1, uint16_t c)
{
  int dx = abs(x1 - x0);
  int sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0);
  int sy = y0 < y1 ? 1 : -1;
  int err = dx + dy;

  for (;;)
    {
      g_px(g, x0, y0, c);
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

static int g_xpix(struct pw_graph_s *g, float x)
{
  float f = (g->xmax - g->xmin);
  int r;

  if (f <= 0.0f)
    {
      f = 1.0f;
    }

  r = (int)((x - g->xmin) / f * (g->w - 1) + 0.5f);
  if (r < 0)
    {
      r = 0;
    }
  else if (r >= g->w)
    {
      r = g->w - 1;
    }

  return r;
}

static int g_ypix(struct pw_graph_s *g, float y)
{
  float f = (g->ymax - g->ymin);
  int r;

  if (f <= 0.0f)
    {
      f = 1.0f;
    }

  r = (int)((g->ymax - y) / f * (g->h - 1) + 0.5f);
  if (r < 0)
    {
      r = 0;
    }
  else if (r >= g->h)
    {
      r = g->h - 1;
    }

  return r;
}

static void g_axes(struct pw_graph_s *g)
{
  int i;

  /* 淡网格（对齐参考，非噪音）：均匀四分线 + 边缘小幅刻度。默认开、可关 */
  if (g->grid_on)
    {
      for (i = 1; i <= 3; i++)
        {
          int gy = (g->h * i) / 4;
          int gx = (g->w * i) / 4;
          int k;

          for (k = 0; k < g->w; k++)
            {
              g_px(g, k, gy, g->grid);
            }

          for (k = 0; k < g->h; k++)
            {
              g_px(g, gx, k, g->grid);
            }
        }

      /* 左下沿距中 4px 小刻度（示意轴刻度位置） */
      for (i = 1; i <= 3; i++)
        {
          int gy = (g->h * i) / 4;
          int gx = (g->w * i) / 4;
          int k;

          for (k = 0; k < 4; k++)
            {
              g_px(g, gx, g->h - 1 - k, g->axis);
              g_px(g, k, gy, g->axis);
            }
        }
    }

  for (i = 0; i < g->w; i++)
    {
      g_px(g, i, 0, g->axis);
      g_px(g, i, g->h - 1, g->axis);
    }

  for (i = 0; i < g->h; i++)
    {
      g_px(g, 0, i, g->axis);
      g_px(g, g->w - 1, i, g->axis);
    }

  if (g->ymin < 0.0f && g->ymax > 0.0f)
    {
      int y0 = g_ypix(g, 0.0f);
      for (i = 0; i < g->w; i++)
        {
          g_px(g, i, y0, g->axis);
        }
    }

  if (g->xmin < 0.0f && g->xmax > 0.0f)
    {
      int x0 = g_xpix(g, 0.0f);
      for (i = 0; i < g->h; i++)
        {
          g_px(g, x0, i, g->axis);
        }
    }
}

static void g_render(struct pw_graph_s *g)
{
  uint32_t px = (uint32_t)g->w * (uint32_t)g->h;
  int s;
  int i;

  for (i = 0; i < (int)px; i++)
    {
      g->buf[i] = g->bg;
    }

  g_axes(g);

  for (s = 0; s < g->nseries; s++)
    {
      struct pw_series_s *sr = &g->series[s];

      if (sr->n < 1)
        {
          continue;
        }

      if (g->dots)
        {
          for (i = 0; i < sr->n; i++)
            {
              int xi = g_xpix(g, sr->x[i]);
              int yi = g_ypix(g, sr->y[i]);
              int dx;
              int dy;

              for (dy = -1; dy <= 1; dy++)
                {
                  for (dx = -1; dx <= 1; dx++)
                    {
                      if (dx * dx + dy * dy <= 1)
                        {
                          g_px(g, xi + dx, yi + dy, sr->color);
                        }
                    }
                }
            }
        }
      else
        {
          int px0 = g_xpix(g, sr->x[0]);
          int py0 = g_ypix(g, sr->y[0]);

          for (i = 1; i < sr->n; i++)
            {
              int px1 = g_xpix(g, sr->x[i]);
              int py1 = g_ypix(g, sr->y[i]);

              g_line(g, px0, py0, px1, py1, sr->color);
              px0 = px1;
              py0 = py1;
            }
        }
    }

  lv_obj_invalidate(g->img);
}

static void g_ev(lv_event_t *e)
{
  struct pw_graph_s *g = lv_event_get_user_data(e);
  lv_indev_t *indev = lv_event_get_indev(e);
  lv_event_code_t code = lv_event_get_code(e);
  lv_point_t p;

  if (g == NULL || indev == NULL)
    {
      return;
    }

  lv_indev_get_point(indev, &p);

  if (code == LV_EVENT_PRESSED)
    {
      g->pan_down = 1;
      g->last = p;
    }
  else if (code == LV_EVENT_PRESSING && g->pan_down)
    {
      int dx = p.x - g->last.x;
      int dy = p.y - g->last.y;

      if (dx != 0 || dy != 0)
        {
          pw_graph_pan(g, -dx, -dy);
          g->last = p;
        }
    }
  else if (code == LV_EVENT_RELEASED || code == LV_EVENT_PRESS_LOST)
    {
      g->pan_down = 0;
    }
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

struct pw_graph_s *pw_graph_create(lv_obj_t *parent, int w, int h,
                                   lv_color_t line, lv_color_t bg, int dots)
{
  struct pw_graph_s *g;
  uint32_t px;

  g = lv_malloc_zeroed(sizeof(*g));
  if (g == NULL)
    {
      return NULL;
    }

  g->w = w;
  g->h = h;
  g->dots = dots;
  g->line = g_rgb565(line);
  g->bg = g_rgb565(bg);
  g->axis = g_rgb565(lv_color_hex(0x5b6875));
  g->grid = g_rgb565(lv_color_hex(0x3a4550));
  g->grid_on = 1;
  g->auto_fit = 1;
  g->xmin = 0.0f;
  g->xmax = 1.0f;
  g->ymin = -1.0f;
  g->ymax = 1.0f;

  px = (uint32_t)w * (uint32_t)h;
  g->buf = lv_malloc(px * 2);
  if (g->buf == NULL)
    {
      lv_free(g);
      return NULL;
    }

  for (uint32_t i = 0; i < px; i++)
    {
      g->buf[i] = g->bg;
    }

  g->dsc.header.magic = LV_IMAGE_HEADER_MAGIC;
  g->dsc.header.cf = LV_COLOR_FORMAT_RGB565;
  g->dsc.header.flags = 0;
  g->dsc.header.w = w;
  g->dsc.header.h = h;
  g->dsc.header.stride = w * 2;
  g->dsc.data_size = px * 2;
  g->dsc.data = (const uint8_t *)g->buf;

  g->img = lv_image_create(parent);
  lv_image_set_src(g->img, &g->dsc);
  lv_obj_set_size(g->img, w, h);
  lv_obj_set_style_bg_opa(g->img, LV_OPA_TRANSP, 0);
  lv_obj_set_style_border_width(g->img, 0, 0);
  lv_obj_set_style_pad_all(g->img, 0, 0);
  lv_obj_add_flag(g->img, LV_OBJ_FLAG_CLICKABLE);
  lv_obj_add_flag(g->img, LV_OBJ_FLAG_SCROLLABLE);
  lv_obj_set_scroll_dir(g->img, LV_DIR_ALL);
  lv_obj_remove_flag(g->img, LV_OBJ_FLAG_SCROLL_CHAIN_HOR);
  lv_obj_remove_flag(g->img, LV_OBJ_FLAG_SCROLL_CHAIN_VER);
  lv_obj_set_scrollbar_mode(g->img, LV_SCROLLBAR_MODE_OFF);
  lv_obj_add_event_cb(g->img, g_ev, LV_EVENT_PRESSED, g);
  lv_obj_add_event_cb(g->img, g_ev, LV_EVENT_PRESSING, g);
  lv_obj_add_event_cb(g->img, g_ev, LV_EVENT_RELEASED, g);
  lv_obj_add_event_cb(g->img, g_ev, LV_EVENT_PRESS_LOST, g);

  g_render(g);
  return g;
}

lv_obj_t *pw_graph_obj(struct pw_graph_s *g)
{
  return g ? g->img : NULL;
}

/* ---- 多序列 API（roadmap §7.3 第 1 条） ---- */

void pw_graph_begin(struct pw_graph_s *g)
{
  if (g == NULL)
    {
      return;
    }

  g->nseries = 0;
}

int pw_graph_add_series(struct pw_graph_s *g, FAR const float *x,
                        FAR const float *y, int n, lv_color_t color)
{
  struct pw_series_s *sr;
  int i;

  if (g == NULL || x == NULL || y == NULL || n < 1 ||
      g->nseries >= SERIES_MAX)
    {
      return -1;
    }

  if (n > GRAPH_MAX)
    {
      n = GRAPH_MAX;
    }

  sr = &g->series[g->nseries];
  for (i = 0; i < n; i++)
    {
      sr->x[i] = x[i];
      sr->y[i] = y[i];
    }

  sr->n = n;
  sr->color = g_rgb565(color);
  g->nseries++;
  return 0;
}

void pw_graph_end(struct pw_graph_s *g)
{
  if (g == NULL)
    {
      return;
    }

  if (g->auto_fit)
    {
      pw_graph_fit(g);
    }
  else
    {
      g_render(g);
    }
}

/* 单序列便捷 API = begin + 序列 0（默认颜色）+ end */

void pw_graph_set_data(struct pw_graph_s *g, FAR const float *x,
                       FAR const float *y, int n)
{
  lv_color_t c;

  if (g == NULL)
    {
      return;
    }

  c.red   = (uint8_t)((g->line >> 11) << 3);
  c.green = (uint8_t)(((g->line >> 5) & 0x3F) << 2);
  c.blue  = (uint8_t)((g->line & 0x1F) << 3);

  pw_graph_begin(g);
  pw_graph_add_series(g, x, y, n, c);
  pw_graph_end(g);
}

void pw_graph_set_auto(struct pw_graph_s *g, int on)
{
  if (g == NULL)
    {
      return;
    }

  g->auto_fit = on ? 1 : 0;
  if (on)
    {
      pw_graph_fit(g);
    }}

void pw_graph_set_grid(struct pw_graph_s *g, int on)
{
  if (g == NULL)
    {
      return;
    }

  g->grid_on = on ? 1 : 0;
  pw_graph_redraw(g);
}

void pw_graph_fit(struct pw_graph_s *g)
{
  int s;
  int i;
  float xmin;
  float xmax;
  float ymin;
  float ymax;
  float pad;
  int have = 0;

  if (g == NULL)
    {
      return;
    }

  /* 全序列范围联合 fit */

  for (s = 0; s < g->nseries; s++)
    {
      struct pw_series_s *sr = &g->series[s];

      for (i = 0; i < sr->n; i++)
        {
          if (!have)
            {
              xmin = xmax = sr->x[i];
              ymin = ymax = sr->y[i];
              have = 1;
              continue;
            }

          if (sr->x[i] < xmin)
            {
              xmin = sr->x[i];
            }

          if (sr->x[i] > xmax)
            {
              xmax = sr->x[i];
            }

          if (sr->y[i] < ymin)
            {
              ymin = sr->y[i];
            }

          if (sr->y[i] > ymax)
            {
              ymax = sr->y[i];
            }
        }
    }

  if (!have)
    {
      g_render(g);
      return;
    }

  if (xmax - xmin < 1e-6f)
    {
      xmax = xmin + 1.0f;
    }

  if (ymax - ymin < 1e-6f)
    {
      ymax = ymin + 1.0f;
    }

  pad = (xmax - xmin) * 0.05f;
  g->xmin = xmin - pad;
  g->xmax = xmax + pad;

  pad = (ymax - ymin) * 0.10f;
  g->ymin = ymin - pad;
  g->ymax = ymax + pad;

  g_render(g);
}

void pw_graph_zoom(struct pw_graph_s *g, int axis, float factor)
{
  float lo;
  float hi;
  float mid;
  float half;

  if (g == NULL || factor <= 0.0f)
    {
      return;
    }

  if (axis == PW_GRAPH_AXIS_X)
    {
      lo = g->xmin;
      hi = g->xmax;
    }
  else
    {
      lo = g->ymin;
      hi = g->ymax;
    }

  mid = (lo + hi) * 0.5f;
  half = (hi - lo) * 0.5f / factor;

  lo = mid - half;
  hi = mid + half;

  if (axis == PW_GRAPH_AXIS_X)
    {
      g->xmin = lo;
      g->xmax = hi;
    }
  else
    {
      g->ymin = lo;
      g->ymax = hi;
    }

  g->auto_fit = 0;
  g_render(g);
}

void pw_graph_pan(struct pw_graph_s *g, int dx, int dy)
{
  float dxr;
  float dyr;

  if (g == NULL)
    {
      return;
    }

  dxr = (g->xmax - g->xmin) * (float)dx / (float)(g->w > 0 ? g->w : 1);
  dyr = (g->ymax - g->ymin) * (float)dy / (float)(g->h > 0 ? g->h : 1);

  g->xmin += dxr;
  g->xmax += dxr;
  g->ymin -= dyr;   /* 屏幕 y 向下，数据 y 向上 */
  g->ymax -= dyr;

  g->auto_fit = 0;
  g_render(g);
}

void pw_graph_redraw(struct pw_graph_s *g)
{
  if (g != NULL)
    {
      g_render(g);
    }
}

void pw_graph_get_range(struct pw_graph_s *g, FAR float *xmin,
                        FAR float *xmax, FAR float *ymin, FAR float *ymax)
{
  if (g == NULL)
    {
      return;
    }

  if (xmin)
    {
      *xmin = g->xmin;
    }

  if (xmax)
    {
      *xmax = g->xmax;
    }

  if (ymin)
    {
      *ymin = g->ymin;
    }

  if (ymax)
    {
      *ymax = g->ymax;
    }
}
