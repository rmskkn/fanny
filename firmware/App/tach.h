/* Fan tachometer: falling-edge count over a 1 s window. */
#ifndef TACH_H
#define TACH_H

#include <stdint.h>

#define TACH_PULSES_PER_REV 2u /* Intel 4-wire fan spec */
#define TACH_WINDOW_MS      1000u

static inline unsigned tach_calc_rpm(uint32_t pulses, uint32_t elapsed_ms)
{
	if (elapsed_ms == 0)
		return 0;
	return pulses * 60000u / (TACH_PULSES_PER_REV * elapsed_ms);
}

void tach_init(void);
/* Call from the main loop; recomputes RPM once per window. */
void tach_update(uint32_t now_ms);
/* ch is 0-based. Resolution is 30 RPM. In DC mode below 100 % the reading is not reliable. */
unsigned tach_rpm(unsigned ch);

#endif
