#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>
#include <libopencm3/stm32/timer.h>

#include "board.h"
#include "pwm.h"

/* Timer clock is 72 MHz for both TIM2 and TIM4 (APB1 x2). */
#define POWER_PSC     71u                         /* 1 MHz tick */
#define POWER_PERIOD  (1000000u / 30u)            /* 30 Hz: slow enough for the 4.7k gate network */
#define SIGNAL_PERIOD (72000000u / 25000u)        /* 25 kHz per Intel 4-wire fan spec */

static const enum tim_oc_id oc[FAN_COUNT] = { TIM_OC1, TIM_OC2, TIM_OC3, TIM_OC4 };

static void timer_start(uint32_t tim, uint32_t psc, uint32_t period, enum tim_oc_mode mode)
{
	timer_set_mode(tim, TIM_CR1_CKD_CK_INT, TIM_CR1_CMS_EDGE, TIM_CR1_DIR_UP);
	timer_set_prescaler(tim, psc);
	timer_set_period(tim, period - 1);
	timer_enable_preload(tim);
	for (unsigned ch = 0; ch < FAN_COUNT; ch++) {
		timer_set_oc_mode(tim, oc[ch], mode);
		timer_enable_oc_preload(tim, oc[ch]);
		timer_set_oc_value(tim, oc[ch], period); /* 100 % */
		timer_enable_oc_output(tim, oc[ch]);
	}
	timer_generate_event(tim, TIM_EGR_UG);
	timer_enable_counter(tim);
}

void pwm_init(void)
{
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_AFIO);
	rcc_periph_clock_enable(RCC_TIM2);
	rcc_periph_clock_enable(RCC_TIM4);

	/*
	 * Power: PWM1, output high (BSS138 on, P-FET on) while CNT < CCR.
	 * Signal: PWM2, output high while CNT >= CCR. The BSS138 buffer inverts,
	 * so the fan's PWM line is high for CCR/period, i.e. the requested duty.
	 */
	timer_start(TIM2, POWER_PSC, POWER_PERIOD, TIM_OCM_PWM1);
	timer_start(TIM4, 0, SIGNAL_PERIOD, TIM_OCM_PWM2);

	/* Only now take the pins: until here the board pull-ups/downs hold fans at full speed. */
	gpio_primary_remap(AFIO_MAPR_SWJ_CFG_JTAG_OFF_SW_ON, AFIO_MAPR_TIM2_REMAP_FULL_REMAP);
	gpio_set_mode(GPIOA, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL, GPIO15);
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_2_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL,
		      GPIO3 | GPIO10 | GPIO11);
	gpio_set_mode(GPIOB, GPIO_MODE_OUTPUT_10_MHZ, GPIO_CNF_OUTPUT_ALTFN_PUSHPULL,
		      GPIO6 | GPIO7 | GPIO8 | GPIO9);
}

void pwm_set_power(unsigned ch, unsigned pct)
{
	timer_set_oc_value(TIM2, oc[ch], POWER_PERIOD * pct / 100);
}

void pwm_set_signal(unsigned ch, unsigned pct)
{
	timer_set_oc_value(TIM4, oc[ch], SIGNAL_PERIOD * pct / 100);
}
