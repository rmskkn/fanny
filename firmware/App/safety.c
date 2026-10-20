#include <libopencm3/stm32/iwdg.h>

#include "board.h"
#include "fan.h"
#include "safety.h"
#include "tach.h"

static bool failsafe = true;
static uint32_t host_seen_ms;
static uint32_t stall_check_ms;
static uint8_t stall_s[FAN_COUNT];

void safety_init(void)
{
	/* Covers the worst case main loop pass, incl. USB writes to a slow host. */
	iwdg_set_period_ms(1000);
	iwdg_start();
}

void safety_host_seen(void)
{
	host_seen_ms = millis();
	failsafe = false;
}

bool safety_failsafe_active(void)
{
	return failsafe;
}

bool safety_fan_stalled(unsigned ch)
{
	return stall_s[ch] >= SAFETY_STALL_S;
}

void safety_update(uint32_t now_ms)
{
	iwdg_reset();

	if (!failsafe && now_ms - host_seen_ms >= SAFETY_HOST_TIMEOUT_MS) {
		failsafe = true;
		fan_force_full();
	}

	if (now_ms - stall_check_ms < 1000)
		return;
	stall_check_ms = now_ms;
	for (unsigned ch = 0; ch < FAN_COUNT; ch++) {
		if (fan_duty(ch) == 0 || tach_rpm(ch) != 0)
			stall_s[ch] = 0;
		else if (stall_s[ch] < SAFETY_STALL_S)
			stall_s[ch]++;
	}
}
