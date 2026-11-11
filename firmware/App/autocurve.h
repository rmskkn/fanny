/*
 * Autonomous mode: no host needed. While active, every channel's duty is
 * recomputed once a second from its onboard NTC thermistor (channel N
 * reads AIN N) via a compile-time temp->duty curve. An out-of-range
 * reading (sensor open or shorted) forces that channel to 100 %.
 */
#ifndef AUTOCURVE_H
#define AUTOCURVE_H

#include <stdbool.h>
#include <stdint.h>

void autocurve_set(bool on);
bool autocurve_active(void);

/* Call every main loop pass; it throttles itself to once a second. */
void autocurve_update(uint32_t now_ms);

#endif
