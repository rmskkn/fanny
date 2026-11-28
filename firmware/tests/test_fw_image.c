/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Host-side tests for Bootloader/fw_image.c's fw_image_verify(): the
 * format2 descriptor checks that stand between a streamed update and
 * jump_to_app(). Core/flash_port_stm32.c's flash_image_read() (real
 * flash registers) is replaced by the fake below, backed by a plain
 * buffer standing in for the app region.
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "chunky_format.h"
#include "flash_layout.h"
#include "fw_image.h"

#include "build_test_image.h"

/* ---- fakes ---- */

static uint8_t fake_flash[APP_SIZE];

void flash_image_read(uint32_t addr, void *buf, size_t len)
{
	memcpy(buf, fake_flash + (addr - APP_BASE), len);
}

/* ---- harness ---- */

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

int main(void)
{
	static const uint8_t payload[] = "pretend app binary bytes, not a real .bin";
	uint32_t version;

	build_test_image(fake_flash, payload, sizeof payload, 7u, 7u, FANNY_PRODUCT_ID, FANNY_GEOMETRY_ID);
	CHECK(fw_image_verify(&version));
	CHECK(version == 7u);

	/* flipped payload bit: seal and length still check out, only the
	 * whole-image hash (fw_image.c's own stage after
	 * format2_descriptor_verify()) disagrees. */
	build_test_image(fake_flash, payload, sizeof payload, 1u, 1u, FANNY_PRODUCT_ID, FANNY_GEOMETRY_ID);
	fake_flash[0] ^= 0x01u;
	CHECK(!fw_image_verify(NULL));

	build_test_image(fake_flash, payload, sizeof payload, 1u, 1u, FANNY_PRODUCT_ID, FANNY_GEOMETRY_ID);
	fake_flash[(size_t)APP_IMAGE_LEN + CHUNKY_DESCRIPTOR_BODY_MAGIC_OFFSET] = 'X';
	CHECK(!fw_image_verify(NULL));

	build_test_image(fake_flash, payload, sizeof payload, 1u, 1u, 0xDEADBEEFu, FANNY_GEOMETRY_ID);
	CHECK(!fw_image_verify(NULL));

	build_test_image(fake_flash, payload, sizeof payload, 1u, 1u, FANNY_PRODUCT_ID, 0xFFu);
	CHECK(!fw_image_verify(NULL));

	/* seal tampered after a valid build: the body's own fields (including
	 * image_len and geometry/product id) all still pass, only the seal
	 * hash no longer matches. */
	build_test_image(fake_flash, payload, sizeof payload, 1u, 1u, FANNY_PRODUCT_ID, FANNY_GEOMETRY_ID);
	fake_flash[(size_t)APP_IMAGE_LEN + CHUNKY_DESCRIPTOR_BODY_LEN] ^= 0x01u;
	CHECK(!fw_image_verify(NULL));

	if (failures) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all fw_image tests passed\n");
	return 0;
}
