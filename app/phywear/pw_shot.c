/****************************************************************************
 * apps/examples/phywear/pw_shot.c
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Real-device screen capture for PhyWear.  See pw_shot.h for the framing
 * format and the reason this exists.
 *
 * Two pixel sources are supported:
 *
 *   1) The board LCD driver's full-screen PSRAM framebuffer.  With
 *      CONFIG_LCD_FB_USING_TWO_UNCOMPRESSED the SiFli board code keeps two
 *      390x450 RGB565 buffers and alternates them per display refresh, so the
 *      panel always scans out of the complete one.  get_disp_buf() (defined in
 *      vendor/sifli/boards/sf32lb52/drivers/lcd/lv_lcd.c) hands back the
 *      buffer LVGL is currently drawing into; after two forced full-screen
 *      refreshes both buffers hold the same complete frame, so it does not
 *      matter which one we read.  This is the ground truth: exactly what the
 *      AMOLED panel is being fed, EPIC GPU rendering included.
 *
 *   2) lv_snapshot_take() on the active screen.  This re-renders the widget
 *      tree with LVGL's own software renderer and therefore does not depend on
 *      any board specific framebuffer plumbing.  It is slower and may differ
 *      from the GPU-composited result, but it is a useful fallback and it also
 *      works in the emulator (which has /dev/fb0 instead).
 *
 * The transfer is base64 over the NSH console.  Each line carries a 4-digit
 * hexadecimal sequence number and the whole frame carries an FNV-1a hash, so
 * the host can detect console interleaving instead of silently producing a
 * corrupt PNG.
 ****************************************************************************/

/****************************************************************************
 * Included Files
 ****************************************************************************/

#include <nuttx/config.h>

#include <stdint.h>
#include <stdbool.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>
#include <malloc.h>
#include <time.h>
#include <unistd.h>

#include <nuttx/cache.h>

#include <lvgl/lvgl.h>
#include <lvgl/src/core/lv_refr.h>
#include <lvgl/src/others/snapshot/lv_snapshot.h>

#include "pw_shot.h"

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Raw bytes encoded per output line.  192 bytes -> 256 base64 characters. */

#define PW_SHOT_CHUNK      192

/* Independent passes over the whole frame.  The CH340N USB bridge behind the
 * console drops bytes under sustained load (measured 2026-09-13: 5 to 11 lost
 * lines out of 1829 with and without pacing, so it is not a FIFO overflow but
 * plain link loss).  A single pass therefore cannot be trusted.  Each line
 * carries its own 16-bit checksum, and the frame is sent twice so the host can
 * pick a clean copy of every line; the two passes are seconds apart, which
 * decorrelates burst losses.  The whole frame is additionally protected by the
 * FNV-1a hash in the SHOT-BEGIN header.
 */

#define PW_SHOT_PASSES     2

/* Worst case output line: 4 seq digits + ':' + 256 base64 + ':' + 4 crc. */

#define PW_SHOT_LINE_MAX   288

/****************************************************************************
 * Private Data
 ****************************************************************************/

static const char g_pw_shot_b64[] =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

/* Sequence number of the next line inside the current pass. */

static unsigned int g_pw_shot_seq;

/****************************************************************************
 * Public Functions
 ****************************************************************************/

/* Full-screen framebuffer accessor exported by the SiFli board LCD driver.
 * Declared here because the vendor tree ships no header for it.
 */

extern void *get_disp_buf(uint32_t size);

/****************************************************************************
 * Private Functions
 ****************************************************************************/

/****************************************************************************
 * Name: pw_shot_hash
 *
 * Description:
 *   32-bit FNV-1a hash of the raw frame, used as an end-to-end integrity
 *   check between the board and the host decoder.
 *
 ****************************************************************************/

static uint32_t pw_shot_hash(const uint8_t *data, uint32_t stride,
                             uint32_t rowbytes, uint32_t rows)
{
  uint32_t hash = 2166136261u;
  uint32_t row;
  uint32_t i;

  for (row = 0; row < rows; row++)
    {
      const uint8_t *p = data + (size_t)row * stride;

      for (i = 0; i < rowbytes; i++)
        {
          hash ^= p[i];
          hash *= 16777619u;
        }
    }

  return hash;
}

/****************************************************************************
 * Name: pw_shot_refresh
 *
 * Description:
 *   Redraw the whole active screen twice.  One pass fills one half of the
 *   board's double-buffered PSRAM framebuffer, the second pass fills the other
 *   half, which leaves both holding the identical complete frame.
 *
 ****************************************************************************/

static void pw_shot_refresh(void)
{
  int pass;

  for (pass = 0; pass < 2; pass++)
    {
      lv_obj_invalidate(lv_screen_active());
      lv_refr_now(NULL);
    }
}

/****************************************************************************
 * Name: pw_shot_hide_overlays
 *
 * Description:
 *   Hide whatever LVGL puts on top of the widget tree for debugging.  With
 *   CONFIG_LV_USE_PERF_MONITOR the built-in performance monitor draws an
 *   "FPS / CPU / ms" label on the system layer, which must not end up in an
 *   evidence screenshot.
 *
 ****************************************************************************/

static void pw_shot_hide_overlays(void)
{
  lv_obj_t *syslayer = lv_layer_sys();
  uint32_t count;
  uint32_t i;

  if (syslayer == NULL)
    {
      return;
    }

  count = lv_obj_get_child_count(syslayer);
  for (i = 0; i < count; i++)
    {
      lv_obj_add_flag(lv_obj_get_child(syslayer, (int32_t)i),
                      LV_OBJ_FLAG_HIDDEN);
    }
}

/****************************************************************************
 * Name: pw_shot_emit_chunk
 *
 * Description:
 *   Emit one base64 line: "<seq>:<base64>:<crc16>".  The checksum covers the
 *   raw bytes of the chunk, so the host can reject a line that lost or gained a
 *   character on the wire instead of feeding corrupt pixels to the decoder.
 *
 ****************************************************************************/

static void pw_shot_emit_chunk(const uint8_t *src, size_t len)
{
  char line[PW_SHOT_LINE_MAX];
  char *out = line;
  uint32_t hash = 2166136261u;
  size_t i;

  out += sprintf(out, "%04x:", g_pw_shot_seq++);

  for (i = 0; i < len; i += 3)
    {
      uint32_t v = (uint32_t)src[i] << 16;

      if (i + 1 < len)
        {
          v |= (uint32_t)src[i + 1] << 8;
        }

      if (i + 2 < len)
        {
          v |= (uint32_t)src[i + 2];
        }

      *out++ = g_pw_shot_b64[(v >> 18) & 0x3f];
      *out++ = g_pw_shot_b64[(v >> 12) & 0x3f];
      *out++ = (i + 1 < len) ? g_pw_shot_b64[(v >> 6) & 0x3f] : '=';
      *out++ = (i + 2 < len) ? g_pw_shot_b64[v & 0x3f] : '=';
    }

  for (i = 0; i < len; i++)
    {
      hash ^= src[i];
      hash *= 16777619u;
    }

  sprintf(out, ":%04x", (unsigned)(hash & 0xffffu));

  printf("%s\n", line);
}

/****************************************************************************
 * Name: pw_shot_emit_pass
 *
 * Description:
 *   Emit every visible row through a 192 byte carry buffer so that chunk
 *   boundaries do not depend on the row stride, then let the host validate and
 *   merge the passes.
 *
 ****************************************************************************/

static unsigned int pw_shot_emit_pass(const uint8_t *pixels, uint32_t stride,
                                      uint32_t rowbytes, uint32_t rows)
{
  uint8_t carry[PW_SHOT_CHUNK];
  size_t ncarry = 0;
  uint32_t row;

  g_pw_shot_seq = 0;

  for (row = 0; row < rows; row++)
    {
      const uint8_t *p = pixels + (size_t)row * stride;
      uint32_t left = rowbytes;

      while (left > 0)
        {
          uint32_t take = (uint32_t)(PW_SHOT_CHUNK - ncarry);

          if (take > left)
            {
              take = left;
            }

          memcpy(carry + ncarry, p, take);
          ncarry += take;
          p += take;
          left -= take;

          if (ncarry == PW_SHOT_CHUNK)
            {
              pw_shot_emit_chunk(carry, PW_SHOT_CHUNK);
              ncarry = 0;
            }
        }
    }

  if (ncarry > 0)
    {
      pw_shot_emit_chunk(carry, ncarry);
    }

  return g_pw_shot_seq;
}

/****************************************************************************
 * Public Functions
 ****************************************************************************/

int pw_shot_dump(const char *name, int src)
{
  lv_display_t *disp;
  const uint8_t *pixels = NULL;
  lv_draw_buf_t *snap = NULL;
  uint32_t width;
  uint32_t height;
  uint32_t stride;
  uint32_t total;
  uint32_t hash;
  const char *srcname;
  unsigned int seq = 0;
  int pass;
  struct timespec t0;
  struct timespec t1;

  if (name == NULL)
    {
      name = "screen";
    }

  disp = lv_display_get_default();
  if (disp == NULL)
    {
      printf("SHOT-FAIL %s no-display\n", name);
      fflush(stdout);
      return -ENODEV;
    }

  width  = (uint32_t)lv_display_get_horizontal_resolution(disp);
  height = (uint32_t)lv_display_get_vertical_resolution(disp);

  /* Drop debug overlays before the frame is rendered and captured. */

  pw_shot_hide_overlays();

  /* Snapshot has to be taken before the forced refresh: lv_snapshot_take()
   * renders the widget tree on its own and is not affected by it, but doing it
   * first keeps the framebuffer pass as short as possible.
   */

  if (src == PW_SHOT_SRC_SNAP)
    {
      snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
      if (snap == NULL)
        {
          printf("SHOT-FAIL %s snapshot-failed\n", name);
          fflush(stdout);
          return -ENOMEM;
        }

      pixels = (const uint8_t *)snap->data;
      width  = snap->header.w;
      height = snap->header.h;
      stride = snap->header.stride;
      srcname = "snap";
    }
  else
    {
      pw_shot_refresh();

      total = width * height * 2u;
      pixels = (const uint8_t *)get_disp_buf(total);
      if (pixels == NULL)
        {
          if (src == PW_SHOT_SRC_FB)
            {
              printf("SHOT-FAIL %s no-framebuffer\n", name);
              fflush(stdout);
              return -ENODEV;
            }

          /* AUTO: fall back to a software snapshot. */

          snap = lv_snapshot_take(lv_screen_active(), LV_COLOR_FORMAT_RGB565);
          if (snap == NULL)
            {
              printf("SHOT-FAIL %s no-framebuffer-and-snapshot-failed\n", name);
              fflush(stdout);
              return -ENOMEM;
            }

          pixels = (const uint8_t *)snap->data;
          width  = snap->header.w;
          height = snap->header.h;
          stride = snap->header.stride;
          srcname = "snap";
        }
      else
        {
          stride = width * 2u;
          srcname = "fb";
        }
    }

  /* The framebuffer lives in PSRAM and is written by the LCD/EPIC DMA engine;
   * make sure the CPU does not read a stale cached line.
   */

  up_invalidate_dcache((uintptr_t)pixels,
                       (uintptr_t)pixels + (size_t)stride * height);

  hash = pw_shot_hash(pixels, stride, width * 2u, height);

  clock_gettime(CLOCK_MONOTONIC, &t0);

  /* The framing lines carry no checksum of their own and the link drops bytes,
   * so emit them twice: if the first copy is truncated the host still sees a
   * complete one.  A header that is corrupt yet still parses is caught by the
   * frame hash below. */

  printf("SHOT-BEGIN %s %u %u rgb565 %u %08x %s\n", name, (unsigned)width,
         (unsigned)height, (unsigned)(width * height * 2u), (unsigned)hash,
         srcname);
  printf("SHOT-BEGIN %s %u %u rgb565 %u %08x %s\n", name, (unsigned)width,
         (unsigned)height, (unsigned)(width * height * 2u), (unsigned)hash,
         srcname);
  fflush(stdout);

  /* Send the whole frame PW_SHOT_PASSES times.  The host keeps, for every
   * sequence number, the first line copy that passes its checksum. */

  for (pass = 0; pass < PW_SHOT_PASSES; pass++)
    {
      seq = pw_shot_emit_pass(pixels, stride, width * 2u, height);

      if (pass + 1 < PW_SHOT_PASSES)
        {
          printf("SHOT-PASS %d %u\n", pass + 1, seq);
          fflush(stdout);
        }
    }

  clock_gettime(CLOCK_MONOTONIC, &t1);
  printf("\nSHOT-END %s %u %d %u\n", name, seq, PW_SHOT_PASSES,
         (unsigned)((t1.tv_sec - t0.tv_sec) * 1000 +
                    (t1.tv_nsec - t0.tv_nsec) / 1000000));
  printf("SHOT-END %s %u %d %u\n", name, seq, PW_SHOT_PASSES,
         (unsigned)((t1.tv_sec - t0.tv_sec) * 1000 +
                    (t1.tv_nsec - t0.tv_nsec) / 1000000));
  fflush(stdout);

  if (snap != NULL)
    {
      lv_draw_buf_destroy(snap);
    }

  return 0;
}
