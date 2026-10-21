/* Timer output for the 4-pin fan PWM signal. Duty is in percent, 0..100. */
#ifndef PWM_H
#define PWM_H

/* Starts the timer at 100 % on every channel, then hands the pins to it. */
void pwm_init(void);

/* 4-pin fan PWM line duty as seen by the fan (high time). TIM4, 25 kHz. */
void pwm_set_signal(unsigned ch, unsigned pct);

#endif
