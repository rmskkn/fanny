#include <libopencm3/cm3/cortex.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>

#include "board.h"
#include "tach.h"

#define TACH_LINES (EXTI12 | EXTI13 | EXTI14 | EXTI15)

static volatile uint32_t pulses[FAN_COUNT];
static unsigned rpm[FAN_COUNT];
static uint32_t window_start;

void exti15_10_isr(void)
{
	uint32_t pending = EXTI_PR & TACH_LINES;

	exti_reset_request(pending);
	for (unsigned ch = 0; ch < FAN_COUNT; ch++)
		if (pending & (EXTI12 << ch))
			pulses[ch]++;
}

void tach_init(void)
{
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_AFIO);

	/* Pull-ups to 3.3 V are on the board (fan tach is open collector). */
	gpio_set_mode(GPIOB, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, GPIO12 | GPIO13 | GPIO14 | GPIO15);
	exti_select_source(TACH_LINES, GPIOB);
	exti_set_trigger(TACH_LINES, EXTI_TRIGGER_FALLING);
	exti_enable_request(TACH_LINES);
	nvic_enable_irq(NVIC_EXTI15_10_IRQ);
}

void tach_update(uint32_t now_ms)
{
	uint32_t elapsed = now_ms - window_start;
	uint32_t count[FAN_COUNT];

	if (elapsed < TACH_WINDOW_MS)
		return;
	window_start = now_ms;

	cm_disable_interrupts();
	for (unsigned ch = 0; ch < FAN_COUNT; ch++) {
		count[ch] = pulses[ch];
		pulses[ch] = 0;
	}
	cm_enable_interrupts();

	for (unsigned ch = 0; ch < FAN_COUNT; ch++)
		rpm[ch] = tach_calc_rpm(count[ch], elapsed);
}

unsigned tach_rpm(unsigned ch)
{
	return rpm[ch];
}
