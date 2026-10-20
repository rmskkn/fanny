#include <libopencm3/cm3/systick.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>

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
	ms_ticks++;
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

static void led_update(uint32_t now)
{
	/* 1 Hz blink in normal operation, 4 Hz while failsafe holds fans at 100 %. */
	uint32_t half_period = safety_failsafe_active() ? 125 : 500;

	if ((now / half_period) & 1)
		gpio_set(GPIOC, GPIO13);
	else
		gpio_clear(GPIOC, GPIO13);
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
		uint32_t now = millis();
		uint8_t c;

		usb_cdc_poll();
		while (usb_cdc_getc(&c))
			protocol_feed((char)c);

		tach_update(now);
		thermal_update(now);
		safety_update(now);
		led_update(now);
	}
}
