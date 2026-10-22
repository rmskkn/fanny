#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/timer.h>

#include "board.h"
#include "pwm.h"

/* Timer clock is 72 MHz for TIM4 (APB1 x2). */
#define SIGNAL_PERIOD (72000000u / 25000u)        /* 25 kHz per Intel 4-wire fan spec */

/*
 * TIM4_CH1 (PB6) and TIM4_CH3 (PB8) are crossed on the PCB: PB6 drives the
 * FAN3 header and PB8 drives the FAN1 header. This array maps fan index
 * (0-based, fan N = index N-1) to the TIM4 channel that actually reaches
 * that fan's header; see hardware/kicad schematic nets FANn_PWM_OUT.
 */
static const enum tim_oc_id oc[FAN_COUNT] = { TIM_OC3, TIM_OC2, TIM_OC1, TIM_OC4 };

void pwm_init(void)
{
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_TIM4);

	/*
	 * PWM2: output high while CNT >= CCR. The BSS138 buffer inverts, so the
	 * fan's PWM line is high for CCR/period, i.e. the requested duty.
	 */
	timer_set_mode(TIM4, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
	timer_set_prescaler(TIM4, 0);
	timer_set_period(TIM4, SIGNAL_PERIOD - 1);
	timer_enable_preload(TIM4);
	for (unsigned ch = 0; ch < FAN_COUNT; ch++) {
		timer_set_oc_mode(TIM4, oc[ch], TIM_OCM_PWM2);
		timer_enable_oc_preload(TIM4, oc[ch]);
		timer_set_oc_value(TIM4, oc[ch], SIGNAL_PERIOD); /* 100 % */
		timer_enable_oc_output(TIM4, oc[ch]);
	}
	timer_generate_event(TIM4, TIM_EGR_UG);
	timer_enable_counter(TIM4);

	/* Only now take the pins: until here the board pull-downs hold fans at full speed. */
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL,
		      GPIO6 | GPIO7 | GPIO8 | GPIO9);
}

void pwm_set_signal(unsigned ch, unsigned pct)
{
	timer_set_oc_value(TIM4, oc[ch], SIGNAL_PERIOD * pct / 100);
}
