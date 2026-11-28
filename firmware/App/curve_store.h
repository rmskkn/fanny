/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Persists autocurve.c's per-channel curve tables and its auto on/off flag
 * across reset, in the last flash page (reserved for this and nothing else;
 * see Core/flash_layout.h's comment on why BOOTLOADER_SIZE + APP_SIZE stop
 * one page short of the physical part). That page has no sub-page erase,
 * so a write always replaces the whole blob.
 */
#ifndef CURVE_STORE_H
#define CURVE_STORE_H

#include <stddef.h>

void curve_store_read(void *data, size_t size);
void curve_store_write(const void *data, size_t size);

#endif
