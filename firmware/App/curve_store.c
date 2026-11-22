/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdint.h>
#include <string.h>

#include <libopencm3/stm32/flash.h>

#include "curve_store.h"

/* Last 1 KiB page of the 64 KiB flash (STM32F103C8, medium-density: 1 KiB
 * pages). Core/stm32f103x8.ld keeps the linked image a page short so code
 * and data never reach it. */
#define CURVE_STORE_ADDR 0x0800FC00u

void curve_store_read(void *data, size_t size)
{
	memcpy(data, (const void *)CURVE_STORE_ADDR, size);
}

void curve_store_write(const void *data, size_t size)
{
	const uint8_t *src = data;

	flash_unlock();
	flash_erase_page(CURVE_STORE_ADDR);
	for (size_t i = 0; i < size; i += 2) {
		uint16_t half = src[i];

		if (i + 1 < size)
			half |= (uint16_t)src[i + 1] << 8;
		flash_program_half_word(CURVE_STORE_ADDR + i, half);
	}
	flash_lock();
}
