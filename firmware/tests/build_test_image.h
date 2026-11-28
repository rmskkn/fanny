/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef BUILD_TEST_IMAGE_H
#define BUILD_TEST_IMAGE_H
#include <string.h>

#include "chunky_format.h" /* firmware/Bootloader/chunky/rust/chunky_format/c */
#include "flash_layout.h"  /* APP_IMAGE_LEN */
#include "sha256.h"        /* firmware/Bootloader/chunky/lib */

/* Shared by tests/test_fw_image.c and tests/test_update_recv.c: builds the
 * APP_SIZE-byte blob Bootloader/update_recv.c expects, the same byte layout
 * Bootloader/fw_image.c checks (lib/format2.c's format2_descriptor_verify(),
 * scheme 2 unsigned) and host/fwtool.py produces for a real update. Writing
 * it by hand here, rather than linking chunky's own format2_image_build(),
 * matches fw_image.c's own reasoning (its header comment): that function is
 * coupled to chunky's device.toml-generated flash.h geometry, which this
 * single-partition layout doesn't use. product_id/geometry_id are
 * parameters so a test can build a descriptor that fails those checks on
 * purpose. */

static inline void put_test_u32(uint8_t *p, uint32_t v)
{
	p[0] = (uint8_t)(v & 0xFFu);
	p[1] = (uint8_t)((v >> 8u) & 0xFFu);
	p[2] = (uint8_t)((v >> 16u) & 0xFFu);
	p[3] = (uint8_t)((v >> 24u) & 0xFFu);
}

/* out must hold APP_SIZE bytes. */
static inline void build_test_image(uint8_t *out, const uint8_t *payload, size_t payload_len,
                                    uint32_t version, uint32_t security_counter,
                                    uint32_t product_id, uint32_t geometry_id)
{
	uint8_t *body = out + (size_t)APP_IMAGE_LEN;
	uint8_t *seal = body + CHUNKY_DESCRIPTOR_BODY_LEN;
	uint8_t sha[32];
	uint8_t msg[26 + CHUNKY_DESCRIPTOR_BODY_LEN]; /* SEAL_DOMAIN_LEN (lib/format2.c) + body */
	uint8_t sealhash[32];

	memset(out, 0xFF, (size_t)APP_IMAGE_LEN);
	memcpy(out, payload, payload_len);

	memset(body, 0, CHUNKY_DESCRIPTOR_BODY_LEN);
	memcpy(body + CHUNKY_DESCRIPTOR_BODY_MAGIC_OFFSET, "CKY2", CHUNKY_DESCRIPTOR_BODY_MAGIC_COUNT);
	body[CHUNKY_DESCRIPTOR_BODY_FORMAT_OFFSET] = 2u;
	body[CHUNKY_DESCRIPTOR_BODY_SCHEME_OFFSET] = (uint8_t)CHUNKY_SCHEME_UNSIGNED;
	put_test_u32(body + CHUNKY_DESCRIPTOR_BODY_PRODUCT_ID_OFFSET, product_id);
	put_test_u32(body + CHUNKY_DESCRIPTOR_BODY_VERSION_OFFSET, version);
	put_test_u32(body + CHUNKY_DESCRIPTOR_BODY_SECURITY_COUNTER_OFFSET, security_counter);
	put_test_u32(body + CHUNKY_DESCRIPTOR_BODY_IMAGE_LEN_OFFSET, (uint32_t)APP_IMAGE_LEN);
	put_test_u32(body + CHUNKY_DESCRIPTOR_BODY_GEOMETRY_ID_OFFSET, geometry_id);

	sha256(out, (size_t)APP_IMAGE_LEN, sha);
	memcpy(body + CHUNKY_DESCRIPTOR_BODY_IMAGE_SHA256_OFFSET, sha, 32u);

	memcpy(msg, "chunky image descriptor v2", 26u);
	memcpy(msg + 26u, body, CHUNKY_DESCRIPTOR_BODY_LEN);
	sha256(msg, sizeof msg, sealhash);
	memcpy(seal, sealhash, 32u);
	memset(seal + 32u, 0, 32u);
}

#endif
