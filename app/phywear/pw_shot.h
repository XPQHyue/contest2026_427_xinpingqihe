/****************************************************************************
 * apps/examples/phywear/pw_shot.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Screen capture for the PhyWear GUI (real device screenshot path).
 *
 * The Huangshan Pi board (SF32LB52) has no /dev/fb0 node: LVGL renders into
 * the board LCD driver's PSRAM double buffer (drv_lcd_fb).  There is also no
 * dd/cat/fb utility in NSH, so the only way to get a screenshot off the board
 * is to print the pixels to the console.  pw_shot_dump() does exactly that:
 * it grabs one complete RGB565 frame and writes it to stdout as base64 with
 * SHOT-BEGIN / SHOT-END framing, so a host script can decode it back to PNG.
 *
 * Every pixel leaves the board through the serial console, so a capture takes
 * roughly (width * height * 2 * 4 / 3) bytes / baud-rate seconds (about 5 s at
 * 1 Mbaud for the 390x450 panel).  This is a debug/evidence path, not a data
 * path.
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_PHYWEAR_PW_SHOT_H
#define __APPS_EXAMPLES_PHYWEAR_PW_SHOT_H

#include <stdint.h>

/****************************************************************************
 * Pre-processor Definitions
 ****************************************************************************/

/* Pixel source selection for pw_shot_dump(). */

#define PW_SHOT_SRC_AUTO   0  /* Board framebuffer, snapshot as fallback */
#define PW_SHOT_SRC_FB     1  /* Board LCD PSRAM framebuffer only */
#define PW_SHOT_SRC_SNAP   2  /* LVGL lv_snapshot_take() only */

/* Default settle time (ms) between opening a page and dumping it.  The host
 * can override it with the --shot=<ms> / --sweep=<ms> command line flags.
 */

#define PW_SHOT_SETTLE_MS  1500

/* Default settle time (ms) before dumping a bench-injected second page: the
 * synthetic signal needs a few seconds to fill the charts.
 */

#define PW_SHOT_P2_SETTLE_MS 8000

/****************************************************************************
 * Public Function Prototypes
 ****************************************************************************/

/****************************************************************************
 * Name: pw_shot_dump
 *
 * Description:
 *   Force a complete redraw of the active screen, then print one full-screen
 *   RGB565 frame to stdout as base64, framed by:
 *
 *     SHOT-BEGIN <name> <w> <h> rgb565 <bytes> <hash> <src>
 *     <seq>:<base64 line>
 *     ...
 *     SHOT-END <name> <lines> <ms>
 *
 *   <hash> is a 32-bit FNV-1a hash of the raw pixel data and <seq> is a
 *   4-digit hexadecimal line counter; both let the host detect a truncated or
 *   interleaved transfer.
 *
 * Input Parameters:
 *   name - Page name copied into the framing lines (never NULL).
 *   src  - PW_SHOT_SRC_* pixel source selector.
 *
 * Returned Value:
 *   0 on success, a negated errno value on failure.
 *
 ****************************************************************************/

int pw_shot_dump(const char *name, int src);

#endif /* __APPS_EXAMPLES_PHYWEAR_PW_SHOT_H */
