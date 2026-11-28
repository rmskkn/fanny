/* SPDX-License-Identifier: GPL-3.0-only */
/* USB CDC-ACM virtual serial port (libopencm3, polled from the main loop). */
#ifndef USB_CDC_H
#define USB_CDC_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

void usb_cdc_init(void);
void usb_cdc_poll(void);

/* Pops one received byte; false when the RX buffer is empty. */
bool usb_cdc_getc(uint8_t *c);

/* Sends len bytes, arbitrary content (App/fw_update.c's binary frames).
 * Dropped when no host has the port open (DTR low) or the host stops
 * reading. */
void usb_cdc_write_bytes(const void *data, size_t len);

/* Sends a NUL-terminated string; same drop behavior as above. */
void usb_cdc_write(const char *s);

#endif
