/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * STM32F103C8 bootloader image: USB CDC + format2 image verify/receive
 * (Bootloader/). ORIGIN/LENGTH must match Core/flash_layout.h's
 * BOOTLOADER_BASE/BOOTLOADER_SIZE by hand -- GNU ld cannot include that
 * header.
 */
MEMORY
{
	rom (rx)  : ORIGIN = 0x08000000, LENGTH = 20K
	ram (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

INCLUDE cortex-m-generic.ld
