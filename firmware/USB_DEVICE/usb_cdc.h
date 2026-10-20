/* USB CDC-ACM virtual serial port (libopencm3, polled from the main loop). */
#ifndef USB_CDC_H
#define USB_CDC_H

#include <stdbool.h>
#include <stdint.h>

void usb_cdc_init(void);
void usb_cdc_poll(void);

/* Pops one received byte; false when the RX buffer is empty. */
bool usb_cdc_getc(uint8_t *c);

/* Sends a string. Dropped when no host has the port open (DTR low) or the host stops reading. */
void usb_cdc_write(const char *s);

#endif
