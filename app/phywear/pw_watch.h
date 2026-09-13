/****************************************************************************
 * apps/examples/phywear/pw_watch.h
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Motion watcher: let PhyWear notice a swinging watch by itself.
 *
 * The AI Agent requirement asks for at least one "proactive + acting"
 * scenario, i.e. the device starts something without being asked.  PhyWear
 * watches the accelerometer while the GUI runs; when the watch is swung for
 * several seconds it pushes a message to the agent, which then opens the
 * pendulum page, measures g and explains the result.
 *
 * The check runs inside the GUI loop (no extra thread) and reads the IMU at
 * about 10 Hz, which is negligible next to the LVGL work.
 ****************************************************************************/

#ifndef __APPS_EXAMPLES_PHYWEAR_PW_WATCH_H
#define __APPS_EXAMPLES_PHYWEAR_PW_WATCH_H

#include <stdbool.h>

/****************************************************************************
 * Name: pw_watch_init
 *
 * Description:
 *   Reset the detector state and open the agent link.  Safe to call when no
 *   sensor or no agent is present: the detector then simply never fires.
 ****************************************************************************/

void pw_watch_init(void);

/****************************************************************************
 * Name: pw_watch_poll
 *
 * Description:
 *   Feed one detector step.  Call it from the GUI loop; it rate-limits itself
 *   to one IMU sample every PW_WATCH_PERIOD_MS.
 *
 * Returned Value:
 *   true when this call detected sustained swinging (and pushed an event).
 ****************************************************************************/

bool pw_watch_poll(void);

#endif /* __APPS_EXAMPLES_PHYWEAR_PW_WATCH_H */
