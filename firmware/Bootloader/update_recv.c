/* SPDX-License-Identifier: GPL-3.0-only */
#include <stdbool.h>
#include <stdint.h>
#include <string.h>

#include <libopencm3/cm3/scb.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>

#include "board.h"
#include "flash_layout.h"
#include "flash_port_stm32.h"
#include "fw_image.h"
#include "update_recv.h"
#include "usb_cdc.h"
#include "util.h" /* chunky_bootloader/lib/util.h: crc32_buf(), geometry-free */

/* A small frame format of its own, not chunky's protocol.h: that one's
 * HELLO/manifest/plan message set exists for format2's A/B session,
 * which this single-partition bootloader doesn't run. The host tool
 * builds the actual update with chunky's format2_image_build()
 * (SPEC.md 4.1/4.2) -- what rides inside FU_DATA is that finished,
 * already-padded-and-sealed blob, streamed as plain bytes. */
#define FU_MAGIC0 0xA5
#define FU_MAGIC1 0x5A
#define FU_MAX_PAYLOAD 256
enum { FU_BEGIN = 1, FU_DATA = 2, FU_ACK = 3, FU_NAK = 4 };

/* PC13 (active low), same cadence as App/safety.c's host-timeout failsafe
 * (README.md's "Failsafe behaviour" table): a fast, even blink reads as a
 * fault from across a room, unlike the app's comms-activity flicker. Only
 * armed when update_recv_run(true) ran because fw_image_verify() failed --
 * not for a plain host-requested update, where the app was fine. */
#define FAULT_BLINK_MS 150
static bool g_fault_blink;

static uint8_t rx_byte(void)
{
	uint8_t c;

	for (;;) {
		usb_cdc_poll();
		if (usb_cdc_getc(&c))
			return c;
		if (g_fault_blink) {
			static uint32_t toggled_at;
			uint32_t now = millis();

			if (ms_since(toggled_at, now) >= FAULT_BLINK_MS) {
				gpio_toggle(GPIOC, GPIO13);
				toggled_at = now;
			}
		}
	}
}

static int recv_frame(uint8_t *type, uint8_t *payload, uint16_t *len)
{
	uint8_t hdr[3]; /* type, len lo, len hi -- magic is read separately */
	uint8_t crc_buf[4];
	uint8_t chk[3 + FU_MAX_PAYLOAD];
	uint32_t want, got;

	if (rx_byte() != FU_MAGIC0)
		return -1;
	if (rx_byte() != FU_MAGIC1)
		return -1;
	hdr[0] = rx_byte();
	hdr[1] = rx_byte();
	hdr[2] = rx_byte();
	*type = hdr[0];
	*len = (uint16_t)(hdr[1] | ((uint16_t)hdr[2] << 8));
	if (*len > FU_MAX_PAYLOAD)
		return -1;
	for (uint16_t i = 0; i < *len; i++)
		payload[i] = rx_byte();
	for (int i = 0; i < 4; i++)
		crc_buf[i] = rx_byte();

	memcpy(chk, hdr, 3);
	if (*len)
		memcpy(chk + 3, payload, *len);
	got = crc32_buf(chk, 3u + *len);
	memcpy(&want, crc_buf, 4);
	return got == want ? 0 : -1;
}

static void send_frame(uint8_t type, const void *payload, uint16_t len)
{
	uint8_t hdr[5];
	uint8_t chk[3 + FU_MAX_PAYLOAD];
	uint32_t crc;
	uint8_t crc_buf[4];

	hdr[0] = FU_MAGIC0;
	hdr[1] = FU_MAGIC1;
	hdr[2] = type;
	hdr[3] = (uint8_t)(len & 0xFFu);
	hdr[4] = (uint8_t)(len >> 8);
	memcpy(chk, hdr + 2, 3);
	if (len)
		memcpy(chk + 3, payload, len);
	crc = crc32_buf(chk, 3u + len);
	memcpy(crc_buf, &crc, 4);

	usb_cdc_write_bytes(hdr, sizeof hdr);
	if (len)
		usb_cdc_write_bytes(payload, len);
	usb_cdc_write_bytes(crc_buf, sizeof crc_buf);
}

void update_recv_run(bool fault)
{
	g_fault_blink = fault;
	rcc_periph_clock_enable(RCC_GPIOC);
	gpio_set(GPIOC, GPIO13);
	gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO13);

	usb_cdc_init();

	for (;;) {
		uint8_t type;
		uint8_t payload[FU_MAX_PAYLOAD];
		uint16_t len;
		uint32_t off;
		bool ok;

		if (recv_frame(&type, payload, &len) < 0 || type != FU_BEGIN)
			continue;

		flash_image_erase(APP_BASE, APP_SIZE);

		off = 0;
		ok = true;
		while (off < APP_SIZE) {
			if (recv_frame(&type, payload, &len) < 0 || type != FU_DATA) {
				ok = false;
				break;
			}
			if ((uint32_t)len > APP_SIZE - off) {
				ok = false;
				break;
			}
			flash_image_write(APP_BASE + off, payload, len);
			off += len;
		}

		if (ok && fw_image_verify(NULL)) {
			send_frame(FU_ACK, NULL, 0);
			scb_reset_system();
		}
		send_frame(FU_NAK, NULL, 0);
		/* loop back: wait for another BEGIN */
	}
}
