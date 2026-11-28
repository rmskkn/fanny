/* SPDX-License-Identifier: GPL-3.0-only */
#ifndef FW_IMAGE_H
#define FW_IMAGE_H
#include <stdbool.h>
#include <stdint.h>

/* True if the app region holds a format2-descriptor-sealed image
 * (chunky_bootloader/lib/format2.c's format2_descriptor_verify(), SPEC.md
 * 4.2/4.3, scheme 2 unsigned) whose whole-image SHA-256 matches. No
 * anti-rollback floor (always 0): this single-partition bootloader keeps
 * no persisted security_floor, unlike chunky's own A/B meta_t. *version
 * (if non-NULL) receives the descriptor's version field on success. */
bool fw_image_verify(uint32_t *version);

#endif
