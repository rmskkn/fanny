#include "board.h"
#include "fan.h"
#include "pwm.h"

static enum fan_mode modes[FAN_COUNT];
static unsigned duties[FAN_COUNT];

static void apply(unsigned ch)
{
	unsigned duty = duties[ch];

	if (modes[ch] == FAN_MODE_PWM) {
		/* Many 4-pin fans keep spinning at 0 % PWM; cutting the supply really stops them. */
		pwm_set_power(ch, duty ? 100 : 0);
		pwm_set_signal(ch, duty);
	} else {
		pwm_set_power(ch, duty);
		pwm_set_signal(ch, 100);
	}
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
	duties[ch] = duty;
	apply(ch);
	return true;
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
