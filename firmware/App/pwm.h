/* Timer outputs for the two fan control paths. Duty is in percent, 0..100. */
#ifndef PWM_H
#define PWM_H

/* Starts both timers at 100 % on every channel, then hands the pins to them. */
void pwm_init(void);

/* High-side P-MOSFET duty (fan supply). TIM2, ~30 Hz. */
void pwm_set_power(unsigned ch, unsigned pct);

/* 4-pin fan PWM line duty as seen by the fan (high time). TIM4, 25 kHz. */
void pwm_set_signal(unsigned ch, unsigned pct);

#endif
