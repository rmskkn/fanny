/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * STM32F103C8: 64 KiB flash, 20 KiB SRAM. Whole-flash build with no
 * bootloader and no OTA: the app alone, at the hardware reset vector
 * (0x08000000), same as every build before the chunky_bootloader
 * integration. For bench/dev use or a board that will never run an
 * update; `make monolithic` / `make flash-monolithic`. The top 1 KiB page
 * (0x0800FC00) is still reserved for curve_store.c's persisted curve
 * tables, same reservation the bootloader/app build also keeps outside
 * its map (Core/flash_layout.h's comment on BOOTLOADER_SIZE + APP_SIZE).
 */
MEMORY
{
	rom (rx)  : ORIGIN = 0x08000000, LENGTH = 64K - 1K
	ram (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

INCLUDE cortex-m-generic.ld
