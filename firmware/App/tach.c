#include <stdbool.h>

#include <libopencm3/cm3/dwt.h>
#include <libopencm3/cm3/nvic.h>
#include <libopencm3/stm32/exti.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/timer.h>

#include "board.h"
#include "tach.h"

/*
 * FAN1..FAN3 are counted in hardware: each tach line clocks its own timer in
 * external clock mode 1, so no edge needs the CPU. The chip has no fourth
 * free timer (TIM4 is the PWM), so FAN4 is counted by EXTI15.
 */
#define HW_COUNTERS 3u

static const uint32_t counter_tim[HW_COUNTERS] = { TIM1, TIM2, TIM3 };
static uint16_t counter_last[HW_COUNTERS];

static volatile uint32_t ch4_pulses;
static uint32_t ch4_last;
static bool ch4_low;

static unsigned rpm[FAN_COUNT];
static uint32_t window_start;

/* Both edges trigger. The pending bit is cleared first, so an edge arriving
 * while the handler runs pends it again instead of being lost. A spike
 * shorter than the entry latency has ended by the time the pin is read, the
 * level matches the stored state, and nothing is counted. */
void exti15_10_isr(void)
{
	bool low;

	EXTI_PR = EXTI15;
	low = !(GPIOB_IDR & GPIO15);
	if (low != ch4_low) {
		ch4_low = low;
		ch4_pulses += low;
	}
}

/* Count falling edges on the timer's TIx input; CK_INT_N_8 drops spikes
 * shorter than ~111 ns. */
static void counter_init(uint32_t tim, enum tim_ic_id ic, enum tim_ic_input in,
			 uint32_t falling, uint8_t trigger)
{
	timer_set_period(tim, 0xFFFF);
	timer_ic_set_input(tim, ic, in);
	timer_ic_set_filter(tim, ic, TIM_IC_CK_INT_N_8);
	TIM_CCER(tim) |= falling;
	timer_slave_set_trigger(tim, trigger);
	timer_slave_set_mode(tim, TIM_SMCR_SMS_ECM1);
	timer_enable_counter(tim);
}

void tach_init(void)
{
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_AFIO);
	rcc_periph_clock_enable(RCC_TIM1);
	rcc_periph_clock_enable(RCC_TIM2);
	rcc_periph_clock_enable(RCC_TIM3);

	/* JTAG off frees PA15 (SWD on PA13/PA14 stays). TIM2 partial remap 1
	 * puts TIM2_CH1 on PA15; TIM3 partial remap puts TIM3_CH2 on PB5. */
	gpio_primary_remap(AFIO_MAPR_SWJ_CFG_JTAG_OFF_SW_ON,
			   AFIO_MAPR_TIM2_REMAP_PARTIAL_REMAP1 | AFIO_MAPR_TIM3_REMAP_PARTIAL_REMAP);

	/* Pull-ups to 3.3 V are on the board (fan tach is open collector). */
	gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, GPIO9 | GPIO15);
	gpio_set_mode(GPIOB, GPIO_MODE_INPUT, GPIO_CNF_INPUT_FLOAT, GPIO5 | GPIO15);

	counter_init(TIM1, TIM_IC2, TIM_IC_IN_TI2, TIM_CCER_CC2P, TIM_SMCR_TS_TI2FP2); /* PA9 */
	counter_init(TIM2, TIM_IC1, TIM_IC_IN_TI1, TIM_CCER_CC1P, TIM_SMCR_TS_TI1FP1); /* PA15 */
	counter_init(TIM3, TIM_IC2, TIM_IC_IN_TI2, TIM_CCER_CC2P, TIM_SMCR_TS_TI2FP2); /* PB5 */

	ch4_low = !(GPIOB_IDR & GPIO15);
	exti_select_source(EXTI15, GPIOB);
	exti_set_trigger(EXTI15, EXTI_TRIGGER_BOTH);
	exti_enable_request(EXTI15);
	nvic_enable_irq(NVIC_EXTI15_10_IRQ);

	dwt_enable_cycle_counter();
	window_start = dwt_read_cycle_counter();
}

void tach_update(void)
{
	uint32_t cycles_per_us = rcc_ahb_frequency / 1000000u;
	uint32_t now = dwt_read_cycle_counter();
	uint32_t elapsed = now - window_start; /* unsigned wraparound is exact */
	uint32_t elapsed_us;
	uint32_t p4;

	if (elapsed < TACH_WINDOW_US * cycles_per_us)
		return;
	window_start = now;
	elapsed_us = elapsed / cycles_per_us;

	for (unsigned i = 0; i < HW_COUNTERS; i++) {
		uint16_t cnt = (uint16_t)TIM_CNT(counter_tim[i]);

		rpm[i] = tach_calc_rpm((uint16_t)(cnt - counter_last[i]), elapsed_us);
		counter_last[i] = cnt;
	}

	p4 = ch4_pulses; /* aligned 32-bit read is atomic; the ISR only adds */
	rpm[3] = tach_calc_rpm(p4 - ch4_last, elapsed_us);
	ch4_last = p4;
}

unsigned tach_rpm(unsigned ch)
{
	return rpm[ch];
}
