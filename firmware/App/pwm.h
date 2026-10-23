/* Timer and GPIO outputs for the fan control paths. Duty is in percent, 0..100. */
#ifndef PWM_H
#define PWM_H

#include <stdbool.h>

/* Starts the signal timer at 100 % on every channel and DC-mode enable low, then hands the pins to them. */
void pwm_init(void);

/*
 * 4-pin fan PWM line duty as seen by the fan (high time), TIM4, 25 kHz. Also
 * the motor-switch chop duty on channels where pwm_set_dc_enable() is true
 * (same net, see hardware/3pin_dc_mode_plan.md).
 */
void pwm_set_signal(unsigned ch, unsigned pct);

/*
 * Gates Q_inv's drive (Q_en, hardware/3pin_dc_mode_plan.md). false = pwm
 * mode: chop path blocked, Q_hs stays on via R_gpd regardless of
 * pwm_set_signal(). true = dc mode: Q_hs chops at pwm_set_signal()'s duty.
 */
void pwm_set_dc_enable(unsigned ch, bool enable);

#endif
