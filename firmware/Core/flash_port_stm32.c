/* SPDX-License-Identifier: GPL-3.0-only */
#include <string.h>

#include <libopencm3/stm32/flash.h>

#include "flash_layout.h"
#include "flash_port_stm32.h"

void flash_image_read(uint32_t addr, void *buf, size_t len)
{
	memcpy(buf, (const void *)addr, len);
}

void flash_image_erase(uint32_t addr, size_t len)
{
	uint32_t page = addr - addr % FLASH_PAGE_SIZE;
	uint32_t end = addr + (uint32_t)len;

	flash_unlock();
	for (; page < end; page += FLASH_PAGE_SIZE)
		flash_erase_page(page);
	flash_lock();
}

void flash_image_write(uint32_t addr, const void *buf, size_t len)
{
	const uint8_t *src = buf;

	flash_unlock();
	for (size_t i = 0; i < len; i += 2) {
		uint16_t half = src[i];

		if (i + 1 < len)
			half |= (uint16_t)src[i + 1] << 8;
		else
			half |= 0xFF00u;
		flash_program_half_word(addr + i, half);
	}
	flash_lock();
}
