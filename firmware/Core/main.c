#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>

#include "autocurve.h"
#include "board.h"
#include "fan.h"
#include "protocol.h"
#include "safety.h"
#include "tach.h"
#include "thermal.h"
#include "usb_cdc.h"

static volatile uint32_t ms_ticks;

void sys_tick_handler(void)
{
	ms_ticks = (ms_ticks + 1) & INT32_MAX;
}

uint32_t millis(void)
{
	return ms_ticks;
}

static void emit_line(const char *line)
{
	usb_cdc_write(line);
	usb_cdc_write("\r\n");
}

#define LED_PULSE_MS 50

static void led_update(uint32_t now)
{
	/* Flash PC13 (active low) once per host line, so the LED is a comms
	 * activity indicator rather than a free-running heartbeat. */
	static bool led_on;
	static uint32_t led_on_at;

	if (protocol_activity_pending()) {
		gpio_clear(GPIOC, GPIO13);
		led_on = true;
		led_on_at = now;
	} else if (led_on && ms_since(led_on_at, now) >= LED_PULSE_MS) {
		gpio_set(GPIOC, GPIO13);
		led_on = false;
	}
}

int main(void)
{
	rcc_clock_setup_pll(&rcc_hse_configs[RCC_CLOCK_HSE8_72MHZ]);

	systick_set_clocksource(STK_CSR_CLKSOURCE_AHB);
	systick_set_reload(rcc_ahb_frequency / 1000 - 1);
	systick_interrupt_enable();
	systick_counter_enable();

	rcc_periph_clock_enable(RCC_GPIOC);
	gpio_set(GPIOC, GPIO13);
	gpio_set_mode(GPIOC, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL, GPIO13);

	/* Fans first: they leave reset at 100 % and stay there until the host takes over. */
	fan_init();
	tach_init();
	thermal_init();
	safety_init();
	protocol_init(emit_line);
	usb_cdc_init();

	for (;;) {
		uint32_t now;
		uint8_t c;

		usb_cdc_poll();
		while (usb_cdc_getc(&c))
			protocol_feed((char)c);

		/* Captured after draining incoming bytes: protocol_feed() can call
		 * safety_host_seen(), which stamps host_seen_ms with its own fresh
		 * millis() read. Reading `now` after it keeps now >= host_seen_ms;
		 * the reverse order would look like a rollover to ms_since() and
		 * trip the failsafe instantly. */
		now = millis();

		tach_update();
		fan_update(now);
		thermal_update(now);
		autocurve_update(now);
		safety_update(now);
		led_update(now);
	}
}
