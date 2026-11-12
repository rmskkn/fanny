/* Fan tachometer: falling-edge count over a 1 s window. */
#ifndef TACH_H
#define TACH_H

#include <stdint.h>

#define TACH_PULSES_PER_REV 2u /* Intel 4-wire fan spec */
#define TACH_WINDOW_US      1000000u

static inline unsigned tach_calc_rpm(uint32_t pulses, uint32_t elapsed_us)
{
	if (elapsed_us == 0)
		return 0;
	return (unsigned)((uint64_t)pulses * 60000000u
			   / ((uint64_t)TACH_PULSES_PER_REV * elapsed_us));
}

void tach_init(void);
/* Call from the main loop; recomputes RPM once per window. The window is
 * timed with the DWT cycle counter, and the rate uses the exact elapsed
 * time, so main-loop delays don't skew it. */
void tach_update(void);
/* ch is 0-based. Resolution is 30 RPM. */
unsigned tach_rpm(unsigned ch);

#endif
