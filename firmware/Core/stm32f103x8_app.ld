/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * STM32F103C8 app image: fan control + the ASCII line protocol, behind
 * the bootloader (Core/stm32f103x8_bootloader.ld). ORIGIN/LENGTH must
 * match Core/flash_layout.h's APP_BASE/APP_SIZE by hand -- GNU ld cannot
 * include that header. The last 128 bytes of this region are the
 * format2 image descriptor (Bootloader/fw_image.c); the 1 KiB page after
 * it (0x0800FC00) belongs to App/curve_store.c, outside this region
 * entirely (APP_SIZE already stops short of the physical 64K for that).
 */
MEMORY
{
	rom (rx)  : ORIGIN = 0x08005000, LENGTH = 43K
	ram (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

INCLUDE cortex-m-generic.ld
