/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Host-side tests for Bootloader/update_recv.c's own frame protocol and
 * its BEGIN/DATA/ACK-or-NAK receive loop, run against the real
 * update_recv.c + fw_image.c + chunky's format2.c/sha256.c/util.c.
 * Everything that touches real hardware is faked below: USB CDC (an RX
 * byte queue a test feeds, a TX buffer it inspects), the app region
 * (a plain buffer instead of flash registers), and PC13/clock/reset
 * (libopencm3 calls the real chip would make, recorded instead of run).
 *
 * update_recv_run() never returns on real hardware -- a successful
 * transfer resets into the new app, a failed one loops back to wait for
 * another FU_BEGIN. setjmp/longjmp stands in for that reset: the fake
 * scb_reset_system() jumps out on success, and the fake usb_cdc_poll()
 * jumps out after the RX queue sits empty for a while, which is the
 * NAK case's only way back to a test since nothing else in the real
 * loop ever gives up (see update_recv.h's own comment on that).
 */
#include <setjmp.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <libopencm3/cm3/scb.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>

#include "board.h"
#include "flash_layout.h"
#include "update_recv.h"
#include "usb_cdc.h"
#include "util.h" /* chunky_bootloader/lib/util.h: crc32_buf(), same CRC the device checks frames with */

#include "build_test_image.h"

/* Mirrors Bootloader/update_recv.c's own private frame format (not
 * exposed by update_recv.h, so duplicated here the same way
 * host/fwtool.py duplicates it on the real host side). */
#define FU_MAGIC0 0xA5
#define FU_MAGIC1 0x5A
#define FU_MAX_PAYLOAD 256
enum { FU_BEGIN = 1, FU_DATA = 2, FU_ACK = 3, FU_NAK = 4 };

/* ---- fake app region (Core/flash_port_stm32.c) ---- */

static uint8_t fake_flash[APP_SIZE];

void flash_image_read(uint32_t addr, void *buf, size_t len)
{
	memcpy(buf, fake_flash + (addr - APP_BASE), len);
}

void flash_image_erase(uint32_t addr, size_t len)
{
	memset(fake_flash + (addr - APP_BASE), 0xFF, len);
}

void flash_image_write(uint32_t addr, const void *buf, size_t len)
{
	memcpy(fake_flash + (addr - APP_BASE), buf, len);
}

/* ---- fake millis() (Core/main.c's SysTick, Bootloader has no main.c of
 * its own): advances on every poll so FAULT_BLINK_MS actually elapses. ---- */

static uint32_t fake_now;

uint32_t millis(void)
{
	return fake_now;
}

/* ---- fake USB CDC: an RX queue a test fills, a TX buffer it reads back ---- */

static const uint8_t *rx_buf;
static size_t rx_len, rx_pos;
static uint8_t tx_buf[2 * APP_SIZE];
static size_t tx_len;
static int starved_polls;
static int gpio_toggle_calls;

static jmp_buf escape;
enum { ESCAPE_NONE = 0, ESCAPE_RESET, ESCAPE_STARVED };
static int escape_reason;

void usb_cdc_init(void) {}

void usb_cdc_poll(void)
{
	fake_now += 20; /* ms per poll: a few hundred polls cover FAULT_BLINK_MS */
	if (rx_pos >= rx_len) {
		if (++starved_polls > 200) {
			escape_reason = ESCAPE_STARVED;
			longjmp(escape, 1);
		}
	} else {
		starved_polls = 0;
	}
}

bool usb_cdc_getc(uint8_t *c)
{
	if (rx_pos >= rx_len)
		return false;
	*c = rx_buf[rx_pos++];
	return true;
}

void usb_cdc_write_bytes(const void *data, size_t len)
{
	memcpy(tx_buf + tx_len, data, len);
	tx_len += len;
}

/* ---- fake libopencm3 calls (no real register access on a host build) ---- */

void rcc_periph_clock_enable(enum rcc_periph_clken clken) { (void)clken; }
void gpio_set(uint32_t gpioport, uint16_t gpios) { (void)gpioport; (void)gpios; }
void gpio_set_mode(uint32_t gpioport, uint8_t mode, uint8_t cnf, uint16_t gpios)
{
	(void)gpioport; (void)mode; (void)cnf; (void)gpios;
}
void gpio_toggle(uint32_t gpioport, uint16_t gpios)
{
	(void)gpioport; (void)gpios;
	gpio_toggle_calls++;
}

void scb_reset_system(void)
{
	escape_reason = ESCAPE_RESET;
	longjmp(escape, 1);
}

/* ---- harness ---- */

static uint8_t stream[3 * APP_SIZE];
static size_t stream_len;

static void stream_reset(void)
{
	stream_len = 0;
}

static void stream_frame(uint8_t type, const uint8_t *payload, uint16_t len)
{
	uint8_t hdr[3] = { type, (uint8_t)(len & 0xFFu), (uint8_t)(len >> 8) };
	uint8_t chk[3 + FU_MAX_PAYLOAD];
	uint32_t crc;

	memcpy(chk, hdr, 3);
	if (len)
		memcpy(chk + 3, payload, len);
	crc = crc32_buf(chk, 3u + len);

	stream[stream_len++] = FU_MAGIC0;
	stream[stream_len++] = FU_MAGIC1;
	memcpy(stream + stream_len, hdr, 3);
	stream_len += 3;
	if (len) {
		memcpy(stream + stream_len, payload, len);
		stream_len += len;
	}
	memcpy(stream + stream_len, &crc, 4);
	stream_len += 4;
}

/* Appends a whole image as FU_BEGIN followed by FU_MAX_PAYLOAD-sized
 * FU_DATA frames, exactly as host/fwtool.py's flash() streams it. */
static void stream_image(const uint8_t *image, size_t image_len)
{
	stream_frame(FU_BEGIN, NULL, 0);
	for (size_t off = 0; off < image_len; off += FU_MAX_PAYLOAD) {
		size_t n = image_len - off < FU_MAX_PAYLOAD ? image_len - off : FU_MAX_PAYLOAD;
		stream_frame(FU_DATA, image + off, (uint16_t)n);
	}
}

static int failures;

#define CHECK(cond) do { \
	if (!(cond)) { \
		printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond); \
		failures++; \
	} \
} while (0)

/* True if tx_buf's last frame (its last 9+ bytes) is an empty-payload
 * frame of the given type: magic, type, len=0, then its CRC. */
static bool last_frame_is(uint8_t type)
{
	uint8_t want[3] = { type, 0, 0 };
	uint32_t crc = crc32_buf(want, 3);
	uint8_t tail[9];

	if (tx_len < sizeof tail)
		return false;
	memcpy(tail, tx_buf + tx_len - sizeof tail, sizeof tail);
	return tail[0] == FU_MAGIC0 && tail[1] == FU_MAGIC1 && tail[2] == type &&
	       tail[3] == 0 && tail[4] == 0 && memcmp(tail + 5, &crc, 4) == 0;
}

static void run_update_recv(bool fault)
{
	rx_buf = stream;
	rx_len = stream_len;
	rx_pos = 0;
	tx_len = 0;
	starved_polls = 0;
	gpio_toggle_calls = 0;
	fake_now = 0;
	escape_reason = ESCAPE_NONE;

	if (!setjmp(escape))
		update_recv_run(fault);
}

int main(void)
{
	static const uint8_t payload[] = "pretend app binary bytes, not a real .bin";
	uint8_t valid_image[APP_SIZE];
	uint8_t bad_image[APP_SIZE];

	build_test_image(valid_image, payload, sizeof payload, 3u, 3u, FANNY_PRODUCT_ID, FANNY_GEOMETRY_ID);
	build_test_image(bad_image, payload, sizeof payload, 3u, 3u, FANNY_PRODUCT_ID, FANNY_GEOMETRY_ID);
	bad_image[0] ^= 0x01u; /* same seal, wrong image: fails fw_image_verify() */

	/* A good transfer: erases, writes exactly the image, verifies, ACKs,
	 * and resets into it -- never reaches another BEGIN. */
	stream_reset();
	stream_image(valid_image, sizeof valid_image);
	run_update_recv(false);
	CHECK(escape_reason == ESCAPE_RESET);
	CHECK(last_frame_is(FU_ACK));
	CHECK(memcmp(fake_flash, valid_image, sizeof valid_image) == 0);
	CHECK(gpio_toggle_calls == 0); /* fault=false: no blink */

	/* A transfer that writes a corrupted image: NAKed, then the loop
	 * waits for another BEGIN that never comes, so the test's own
	 * starved-poll bailout is what ends it, not the device. */
	stream_reset();
	stream_image(bad_image, sizeof bad_image);
	run_update_recv(false);
	CHECK(escape_reason == ESCAPE_STARVED);
	CHECK(last_frame_is(FU_NAK));

	/* fault=true (boot_main.c's "no bootable app" path): blinks while it
	 * waits, same as a corrupted board sitting with no host connected. */
	stream_reset();
	run_update_recv(true);
	CHECK(escape_reason == ESCAPE_STARVED);
	CHECK(gpio_toggle_calls > 0);

	/* A frame with a flipped CRC bit breaks the DATA loop (recv_frame()
	 * returns -1, type != FU_DATA): erase already happened, so the app
	 * region is left erased, same as the real failure mode. */
	stream_reset();
	stream_frame(FU_BEGIN, NULL, 0);
	stream_frame(FU_DATA, valid_image, FU_MAX_PAYLOAD);
	stream[stream_len - 1] ^= 0x01u; /* last CRC byte of that DATA frame */
	run_update_recv(false);
	CHECK(escape_reason == ESCAPE_STARVED);
	CHECK(last_frame_is(FU_NAK));
	CHECK(fake_flash[0] == 0xFFu); /* erased, never got a good write */

	if (failures) {
		printf("%d check(s) failed\n", failures);
		return 1;
	}
	printf("all update_recv tests passed\n");
	return 0;
}
