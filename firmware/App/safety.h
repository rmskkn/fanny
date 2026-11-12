/*
 * Safety: independent watchdog, host-loss failsafe and stall detection.
 *
 * Fans run at 100 % until the host sends its first command, and return to
 * 100 % when no command arrives for SAFETY_HOST_TIMEOUT_MS. The watchdog
 * resets the MCU if the main loop hangs; the board pull-ups keep the fans at
 * full speed while the MCU is in reset.
 */
#ifndef SAFETY_H
#define SAFETY_H

#include <stdbool.h>
#include <stdint.h>

#define SAFETY_HOST_TIMEOUT_MS 20000u
#define SAFETY_STALL_S         3u

void safety_init(void);
void safety_update(uint32_t now_ms);

/* Any valid host command counts as a keepalive and ends the failsafe. */
void safety_host_seen(void);
bool safety_failsafe_active(void);

/* Disable the host-silence failsafe (e.g. while autocurve mode drives the
 * fans with no host at all) or re-enable it. Stall detection and the
 * watchdog are unaffected. Disabling also clears an active failsafe. */
void safety_set_host_check(bool enabled);

/* Duty > 0 but no tach pulses for SAFETY_STALL_S seconds. ch is 0-based. */
bool safety_fan_stalled(unsigned ch);

#endif
