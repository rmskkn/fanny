/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * STM32F103C8 flash map, single-partition OTA: a bootloader that owns
 * USB CDC and receives the update itself (no A/B, no trial-boot -- a
 * failed/interrupted update needs a re-flash over the same CDC link, not
 * an automatic revert). The last 1 KiB page is still reserved for
 * App/curve_store.c's calibration data, same as before any OTA work.
 *
 *   0x08000000  BOOTLOADER_SIZE    bootloader (USB CDC + receiver)
 *   +           APP_SIZE           app (fan control + ASCII protocol)
 *   0x0800FC00  1 KiB              curve_store.c, untouched by either
 */
#ifndef FLASH_LAYOUT_H
#define FLASH_LAYOUT_H
#include <stdint.h>

#define FW_FLASH_BASE      0x08000000u
#define FLASH_PAGE_SIZE 1024u          /* STM32F103C8: medium-density, 1 KiB pages */

#define BOOTLOADER_BASE FW_FLASH_BASE
#define BOOTLOADER_SIZE (20u * 1024u)

#define APP_BASE        (BOOTLOADER_BASE + BOOTLOADER_SIZE)
#define APP_SIZE        (63u * 1024u - BOOTLOADER_SIZE)  /* up to curve_store's page */

/* format2's signed image descriptor (SPEC.md 4.1/4.2, scheme 2 unsigned)
 * sits in the app region's last 128 bytes; App/fw_image.c reuses
 * chunky_bootloader/lib/format2.c's format2_descriptor_verify() and
 * chunky_format.h's accessors to read and check it, without any of
 * format2_image.c's own flash.h-coupled (A/B slot) plumbing. */
#define APP_IMAGE_LEN   (APP_SIZE - 128u)

/* format2_descriptor_verify()'s product_id/geometry_id are a match key
 * between device and the host tool that builds an update; chunky's own
 * CB_PRODUCT_ID/format2_geometry_id() come from its A/B device catalog
 * (device.toml + tools/gen_device.py), which this single-partition
 * layout has no use for, so fanny picks its own fixed constants instead
 * -- both sides just need to agree on them. */
#define FANNY_PRODUCT_ID  0x46414E31u /* "FAN1" */
#define FANNY_GEOMETRY_ID 0x00000001u

/* GNU ld cannot include this header, so Core/stm32f103x8_bootloader.ld
 * and Core/stm32f103x8_app.ld duplicate BOOTLOADER_BASE/_SIZE and
 * APP_BASE/APP_SIZE by hand. These catch the two drifting apart. */
_Static_assert(BOOTLOADER_BASE == 0x08000000u, "stm32f103x8_bootloader.ld ORIGIN drifted from flash_layout.h");
_Static_assert(BOOTLOADER_SIZE == 20u * 1024u, "stm32f103x8_bootloader.ld LENGTH drifted from flash_layout.h");
_Static_assert(APP_BASE        == 0x08005000u, "stm32f103x8_app.ld ORIGIN drifted from flash_layout.h");
_Static_assert(APP_SIZE        == 43u * 1024u, "stm32f103x8_app.ld LENGTH drifted from flash_layout.h");

#endif
