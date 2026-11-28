/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef UPDATE_RECV_H
#define UPDATE_RECV_H

#include <stdbool.h>

/* Runs the USB CDC update receiver until a transfer completes (flashed,
 * verified, and reset into the app) or the link is idle/broken long
 * enough to give up and fall back to re-running this loop. Never returns
 * normally -- either it resets into the newly-flashed app, or it keeps
 * waiting for a host to connect (DESIGN.md-style recovery loop: nothing
 * else is bootable, so there is nowhere else to go).
 *
 * fault: true when boot_main.c got here because the app region failed
 * fw_image_verify() rather than because the app asked for an update
 * (App/fw_update.c's "update" command, a healthy app, passes false).
 * While true, PC13 blinks fast (same cadence as App/safety.c's failsafe,
 * README.md's "Failsafe behaviour" table) so a corrupted board waiting
 * for a re-flash looks different from one just sitting in a normal
 * update. */
void update_recv_run(bool fault);

#endif
