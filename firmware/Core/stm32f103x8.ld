/* STM32F103C8: 64 KiB flash, 20 KiB SRAM. Sections come from libopencm3. */
MEMORY
{
	rom (rx)  : ORIGIN = 0x08000000, LENGTH = 64K
	ram (rwx) : ORIGIN = 0x20000000, LENGTH = 20K
}

INCLUDE cortex-m-generic.ld
