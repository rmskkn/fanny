/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>

#include <libopencm3/cm3/scb.h>

#include "fw_update.h"

/* Same address as Bootloader/boot_main.c's own g_boot_magic: both links
 * place ram's one and only .noinit variable at ORIGIN(ram) (see
 * cortex-m-generic.ld's ".noinit (NOLOAD) : { *(.noinit*) } >ram", the
 * first thing placed in ram), so this coincides with the bootloader's
 * copy without either side needing a shared symbol. */
#define BOOT_MAGIC 0x5A5AA5A5u
__attribute__((section(".noinit"))) static volatile uint32_t g_boot_magic;

void fw_update_start(void)
{
	g_boot_magic = BOOT_MAGIC;
	scb_reset_system();
}
