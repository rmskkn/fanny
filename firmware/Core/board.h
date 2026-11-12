/*
 * Board pin map. Must match hardware/kicad schematic (see docs in README.md).
 *
 *   FANn PWM signal  (TIM4, 25 kHz):           PB8  PB7 PB6  PB9
 *     (PCB crosses TIM4_CH1/CH3: PB6 reaches the FAN3 header, PB8 reaches
 *     the FAN1 header; App/pwm.c's oc[] array maps fan index to channel)
 *   FANn DC-mode enable (plain GPIO, gates
 *     the motor switch's chop path):           PA6  PA7  PA8  PB1
 *     (low/high-Z = pwm mode, chop blocked, Q_hs held on; see
 *     hardware/3pin_dc_mode_plan.md and App/pwm.c)
 *   FAN1..3 tach     (TIM1_CH2, TIM2_CH1, TIM3_CH2
 *     external clock; TIM2/TIM3 partial remap,
 *     JTAG off, SWD kept):                     PA9  PA15 PB5
 *   FAN4 tach        (EXTI15, both edges):     PB15
 *   AIN1..AIN4       (ADC1 IN0..IN3):          PA0..PA3
 *   12 V sense       (ADC1 IN4):               PA4  (10k/3.3k divider)
 *     (moved off PB0 2026-11-11: PB0 channel read dead/stuck, debug pending)
 *   USB FS                                     PA11 (DM), PA12 (DP)
 *   SWD                                        PA13, PA14
 *   Status LED       (active low)              PC13
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

#define FAN_COUNT 4
#define AIN_COUNT 4

/* Milliseconds since boot, from SysTick (Core/main.c). Runs 0..INT32_MAX
 * and rolls over to 0 every ~24.8 days. */
uint32_t millis(void);

/* Milliseconds from `prev` to `now`. now < prev means millis() rolled over
 * from INT32_MAX to 0 in between: count the ticks up to INT32_MAX plus the
 * ticks after 0 (the +1 is the INT32_MAX -> 0 step itself). */
static inline uint32_t ms_since(uint32_t prev, uint32_t now)
{
	return now < prev ? INT32_MAX - prev + now + 1 : now - prev;
}

#endif
