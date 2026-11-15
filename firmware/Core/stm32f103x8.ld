/*
 * STM32F103C8: 64 KiB flash, 20 KiB SRAM. Sections come from libopencm3.
 * The linked image gets 64K - 1K: the last 1 KiB page (0x0800FC00) is
 * reserved for curve_store.c's persisted auto-mode curve tables and must
 * stay out of reach of code and data.
 */
MEMORY
{
	rom (rx)  : ORIGIN = 0x08000000, LENGTH = 64K - 1K
	ram (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

INCLUDE cortex-m-generic.ld
