/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FW_UPDATE_H
#define FW_UPDATE_H

/* Reboots into the bootloader's own update receiver
 * (Bootloader/update_recv.c): sets a RAM flag at a fixed address shared
 * with the bootloader's link (both place their one .noinit variable at
 * ORIGIN(ram)) and resets. The app plays no further part in an update --
 * the bootloader owns USB CDC for it, erases and rewrites the app region,
 * verifies it, and resets into it. Call from the "update" line command. */
void fw_update_start(void);

#endif
