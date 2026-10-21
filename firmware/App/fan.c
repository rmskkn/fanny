#include "board.h"
#include "fan.h"
#include "pwm.h"

static unsigned duties[FAN_COUNT];

void fan_init(void)
{
	pwm_init();
	for (unsigned ch = 0; ch < FAN_COUNT; ch++)
		fan_set_duty(ch, 100);
}

bool fan_set_duty(unsigned ch, unsigned duty)
{
	if (ch >= FAN_COUNT || duty > 100)
		return false;
	duties[ch] = duty;
	pwm_set_signal(ch, duty);
	return true;
}

unsigned fan_duty(unsigned ch)
{
	return duties[ch];
}

void fan_force_full(void)
{
	for (unsigned ch = 0; ch < FAN_COUNT; ch++)
		fan_set_duty(ch, 100);
}
