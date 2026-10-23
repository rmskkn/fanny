#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/timer.h>

#include "board.h"
#include "pwm.h"

/* Timer clock is 72 MHz (APB1 x2). */
#define SIGNAL_PERIOD (72000000u / 25000u)        /* 25 kHz per Intel 4-wire fan spec */

/*
 * TIM4_CH1 (PB6) and TIM4_CH3 (PB8) are crossed on the PCB: PB6 drives the
 * FAN3 header and PB8 drives the FAN1 header. This array maps fan index
 * (0-based, fan N = index N-1) to the TIM4 channel that actually reaches
 * that fan's header; see hardware/kicad schematic nets FANn_PWM_OUT.
 */
static const enum tim_oc_id oc[FAN_COUNT] = { TIM_OC3, TIM_OC2, TIM_OC1, TIM_OC4 };

/* FANn_DCEN, in fan order (board.h). Gates Q_inv's drive; see 3pin_dc_mode_plan.md. */
static const uint32_t dcen_port[FAN_COUNT] = { GPIOA, GPIOA, GPIOA, GPIOB };
static const uint16_t dcen_pin[FAN_COUNT]  = { GPIO6, GPIO7, GPIO8, GPIO1 };

static void timer_start(uint32_t tim, uint32_t period, const enum tim_oc_id *ocs)
{
	timer_set_mode(tim, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
	timer_set_prescaler(tim, 0);
	timer_set_period(tim, period - 1);
	timer_enable_preload(tim);
	for (unsigned ch = 0; ch < FAN_COUNT; ch++) {
		timer_set_oc_mode(tim, ocs[ch], TIM_OCM_PWM2);
		timer_enable_oc_preload(tim, ocs[ch]);
		timer_set_oc_value(tim, ocs[ch], period); /* 100 % */
		timer_enable_oc_output(tim, ocs[ch]);
	}
	timer_generate_event(tim, TIM_EGR_UG);
	timer_enable_counter(tim);
}

void pwm_init(void)
{
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_TIM4);

	/*
	 * PWM2: output high while CNT >= CCR. The BSS138 signal transistor
	 * inverts once, so the fan's PWM line is high for CCR/period, i.e. the
	 * requested duty. The same net also drives the motor switch's Q_inv
	 * gate; FANn_DCEN decides whether that drive reaches Q_hs (see
	 * 3pin_dc_mode_plan.md).
	 */
	timer_start(TIM4, SIGNAL_PERIOD, oc);

	/* Only now take the pin: until here the board pull-up holds the fan at full speed. */
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL,
		      GPIO6 | GPIO7 | GPIO8 | GPIO9);

	/* DC-mode enable: push-pull, default low (pwm mode) before any fan_set_mode() call. */
	for (unsigned ch = 0; ch < FAN_COUNT; ch++) {
		gpio_clear(dcen_port[ch], dcen_pin[ch]);
		gpio_set_mode(dcen_port[ch], GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_PUSHPULL,
			      dcen_pin[ch]);
	}
}

void pwm_set_signal(unsigned ch, unsigned pct)
{
	timer_set_oc_value(TIM4, oc[ch], SIGNAL_PERIOD * pct / 100);
}

void pwm_set_dc_enable(unsigned ch, bool enable)
{
	if (enable)
		gpio_set(dcen_port[ch], dcen_pin[ch]);
	else
		gpio_clear(dcen_port[ch], dcen_pin[ch]);
}
