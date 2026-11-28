/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FLASH_PORT_STM32_H
#define FLASH_PORT_STM32_H
#include <stddef.h>
#include <stdint.h>

/* Direct STM32F103 flash access, absolute addresses: no chunky
 * flash_port.h plat_flash_t indirection, since that contract exists for
 * lib/flash.c's A/B region bookkeeping, which this single-partition
 * bootloader doesn't use. */

/* Memory-mapped: a plain copy. */
void flash_image_read(uint32_t addr, void *buf, size_t len);

/* Erases every FLASH_PAGE_SIZE page covering [addr, addr+len). len need
 * not be page-aligned; the last partial page is erased whole. */
void flash_image_erase(uint32_t addr, size_t len);

/* Half-word granular (PM0075); len may be odd, the last byte is padded
 * with 0xFF (an erased bit, so it never blocks a later program there). */
void flash_image_write(uint32_t addr, const void *buf, size_t len);

#endif
