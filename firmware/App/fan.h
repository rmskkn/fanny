/* Per-channel fan duty, mapped onto the 4-pin fan PWM signal. Fans are powered straight from 12 V. */
#ifndef FAN_H
#define FAN_H

#include <stdbool.h>

/* All channels start at 100 %. */
void fan_init(void);

/* ch is 0-based. Return false on out-of-range arguments. */
bool fan_set_duty(unsigned ch, unsigned duty);

unsigned fan_duty(unsigned ch);

/* Failsafe: every channel to 100 %. */
void fan_force_full(void);

#endif
