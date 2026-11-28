/* SPDX-License-Identifier: GPL-3.0-only */
#include <string.h>

#include "flash_layout.h"
#include "flash_port_stm32.h"
#include "fw_image.h"

#include "chunky_format.h"
#include "format2.h"
#include "sha256.h"

/* Adapted from chunky_bootloader/lib/format2_image.c's
 * format2_image_verify_flash()/format2_image_hash_flash(): same checks
 * (descriptor seal via format2_descriptor_verify(), then the whole-image
 * SHA-256 against the descriptor's own copy), reading APP_BASE directly
 * instead of through lib/flash.h's slot_base(slot) -- this single
 * partition has no slot to parameterize on. */
bool fw_image_verify(uint32_t *version)
{
	uint8_t desc_raw[CHUNKY_SIGNED_DESCRIPTOR_LEN];
	chunky_signed_descriptor_view_t desc;
	chunky_descriptor_body_view_t body;
	uint8_t chunk[256];
	sha256_t s;
	uint8_t got[32];
	size_t o;

	flash_image_read(APP_BASE + APP_IMAGE_LEN, desc_raw, sizeof desc_raw);
	if (chunky_signed_descriptor_view(desc_raw, sizeof desc_raw, &desc) != 0)
		return false;

	if (format2_descriptor_verify(desc, FANNY_GEOMETRY_ID, FANNY_PRODUCT_ID, 0u,
	                               (uint32_t)APP_IMAGE_LEN, &body) != FORMAT2_OK)
		return false;

	sha256_init(&s);
	for (o = 0; o < (size_t)APP_IMAGE_LEN; o += sizeof chunk) {
		size_t n = (size_t)APP_IMAGE_LEN - o < sizeof chunk ? (size_t)APP_IMAGE_LEN - o : sizeof chunk;

		flash_image_read(APP_BASE + o, chunk, n);
		sha256_update(&s, chunk, n);
	}
	sha256_final(&s, got);
	if (memcmp(got, chunky_descriptor_body_image_sha256(body), 32u) != 0)
		return false;

	if (version)
		*version = chunky_descriptor_body_version(body);
	return true;
}
