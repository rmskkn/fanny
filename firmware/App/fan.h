/* Per-channel fan state: mode and duty, mapped onto the signal and motor-switch PWM. */
#ifndef FAN_H
#define FAN_H

#include <stdbool.h>
#include <stdint.h>

enum fan_mode {
	FAN_MODE_PWM, /* 4-pin: motor switch held on, speed via the 25 kHz signal line */
	FAN_MODE_DC,  /* 3-pin: signal line released, speed via the motor switch */
};

/* All channels start in PWM mode at 100 %. */
void fan_init(void);

/* ch is 0-based. Return false on out-of-range arguments. A duty below 30 %
 * on a fan reading 0 rpm runs at 30 % for 2 s first; fan_duty() reports the
 * requested duty throughout. */
bool fan_set_duty(unsigned ch, unsigned duty);
/* Call from the main loop; ends the start-up kick. */
void fan_update(uint32_t now_ms);
bool fan_set_mode(unsigned ch, enum fan_mode mode);

unsigned fan_duty(unsigned ch);
enum fan_mode fan_mode(unsigned ch);

/* Failsafe: every channel to 100 %, modes kept. */
void fan_force_full(void);

#endif
