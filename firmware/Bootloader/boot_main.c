/* SPDX-License-Identifier: GPL-3.0-only */
#include <stddef.h>
#include <stdint.h>

#include <libopencm3/cm3/scb.h>
#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/rcc.h>

#include "flash_layout.h"
#include "fw_image.h"
#include "update_recv.h"

/* USB_DEVICE/usb_cdc.c calls millis() (Core/board.h) for its D+ reset
 * pulldown delay and TX timeout; same SysTick setup as Core/main.c's,
 * since this binary has no main.c of its own. */
static volatile uint32_t ms_ticks;

void sys_tick_handler(void)
{
	ms_ticks = (ms_ticks + 1) & INT32_MAX;
}

uint32_t millis(void)
{
	return ms_ticks;
}

/* App/fw_update.c's "update" command sets this (its own .noinit, at the
 * same address: both links place ram's one and only .noinit variable at
 * ORIGIN(ram), see cortex-m-generic.ld) and resets, asking this boot to
 * run the update receiver instead of verifying and jumping. */
#define BOOT_MAGIC 0x5A5AA5A5u
__attribute__((section(".noinit"))) static volatile uint32_t g_boot_magic;

/* Sets VTOR to the app region and jumps to its reset handler, exactly as
 * the hardware would on a cold reset from that address. Never returns. */
static void jump_to_app(void)
{
	uint32_t sp = ((const uint32_t *)APP_BASE)[0];
	uint32_t reset_handler = ((const uint32_t *)APP_BASE)[1];

	SCB_VTOR = APP_BASE;
	__asm__ volatile (
		"msr msp, %0\n"
		"bx %1\n"
		: : "r" (sp), "r" (reset_handler)
	);
	for (;;)
		; /* unreachable */
}

int main(void)
{
	rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);
	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
	systick_set_reload(rcc_ahb_frequency / 1000 - 1);
	systick_interrupt_enable();
	systick_counter_enable();

	if (g_boot_magic == BOOT_MAGIC) {
		g_boot_magic = 0;
		update_recv_run(false); /* never returns -- host-requested, app was fine */
	}

	if (fw_image_verify(NULL))
		jump_to_app();

	/* No bootable app: nothing else to fall back to but an update. PC13
	 * blinks fast so a corrupted board is visibly different from one
	 * just sitting in a normal update. */
	update_recv_run(true);
	return 0;
}
