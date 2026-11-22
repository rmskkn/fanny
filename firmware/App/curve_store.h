/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Persists autocurve.c's per-channel curve tables across reset, in the
 * last flash page (reserved for this and nothing else, see
 * Core/stm32f103x8.ld). That page has no sub-page erase, so a write always
 * replaces the whole blob.
 */
#ifndef CURVE_STORE_H
#define CURVE_STORE_H

#include <stddef.h>

void curve_store_read(void *data, size_t size);
void curve_store_write(const void *data, size_t size);

#endif
