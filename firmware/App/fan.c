#include "board.h"
#include "fan.h"
#include "pwm.h"
#include "tach.h"

/* A stopped fan may not start at a low duty, so start it at FAN_KICK_PCT
 * for FAN_KICK_MS before dropping to the requested duty. */
#define FAN_KICK_PCT 30u
#define FAN_KICK_MS  2000u

static enum fan_mode modes[FAN_COUNT];
static unsigned duties[FAN_COUNT];
static bool kicking[FAN_COUNT];
static uint32_t kick_start_ms[FAN_COUNT];

static void apply(unsigned ch)
{
	pwm_set_signal(ch, kicking[ch] ? FAN_KICK_PCT : duties[ch]);
	pwm_set_dc_enable(ch, modes[ch] == FAN_MODE_DC);
}

void fan_init(void)
{
	pwm_init();
	for (unsigned ch = 0; ch < FAN_COUNT; ch++) {
		modes[ch] = FAN_MODE_PWM;
		duties[ch] = 100;
		apply(ch);
	}
}

bool fan_set_duty(unsigned ch, unsigned duty)
{
	if (ch >= FAN_COUNT || duty > 100)
		return false;
	if (duty == 0 || duty >= FAN_KICK_PCT) {
		kicking[ch] = false;
	} else if (!kicking[ch] && tach_rpm(ch) == 0) {
		kicking[ch] = true;
		kick_start_ms[ch] = millis();
	}
	duties[ch] = duty;
	apply(ch);
	return true;
}

void fan_update(uint32_t now_ms)
{
	for (unsigned ch = 0; ch < FAN_COUNT; ch++) {
		if (kicking[ch] && ms_since(kick_start_ms[ch], now_ms) >= FAN_KICK_MS) {
			kicking[ch] = false;
			apply(ch);
		}
	}
}

bool fan_set_mode(unsigned ch, enum fan_mode mode)
{
	if (ch >= FAN_COUNT || (mode != FAN_MODE_PWM && mode != FAN_MODE_DC))
		return false;
	modes[ch] = mode;
	apply(ch);
	return true;
}

unsigned fan_duty(unsigned ch)
{
	return duties[ch];
}

enum fan_mode fan_mode(unsigned ch)
{
	return modes[ch];
}

void fan_force_full(void)
{
	for (unsigned ch = 0; ch < FAN_COUNT; ch++)
		fan_set_duty(ch, 100);
}
