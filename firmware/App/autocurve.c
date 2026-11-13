#include "autocurve.h"
#include "board.h"
#include "fan.h"
#include "ntc_calib.h"
#include "thermal.h"

/* Readings this close to either rail mean the sensor is open or shorted,
 * not a real temperature. */
#define AIN_RAW_MIN_VALID 20u
#define AIN_RAW_MAX_VALID 4075u

#define UPDATE_INTERVAL_MS 1000u

struct curve_point {
	int16_t temp_c;
	uint8_t duty;
};

/* Mirrors host/fancontrol.conf.example's documented default curve,
 * applied to every channel via its same-numbered AIN (fan N <- AIN N). */
static const struct curve_point curve[] = {
	{ 30, 20 }, { 40, 35 }, { 50, 55 }, { 60, 75 }, { 70, 100 },
};
#define CURVE_LEN (sizeof(curve) / sizeof(curve[0]))

static bool active;
static uint32_t last_update_ms;

void autocurve_set(bool on)
{
	active = on;
}

bool autocurve_active(void)
{
	return active;
}

static bool ain_valid(unsigned ch)
{
	unsigned raw = thermal_ain_raw(ch);

	return raw >= AIN_RAW_MIN_VALID && raw <= AIN_RAW_MAX_VALID;
}

static int temp_c_for_mv(unsigned ch, unsigned mv)
{
	const struct mv_point *table = ntc_calib_table[ch];

	if ((int)mv >= table[0].mv)
		return table[0].temp_c;
	if ((int)mv <= table[NTC_CALIB_LEN - 1].mv)
		return table[NTC_CALIB_LEN - 1].temp_c;
	for (unsigned i = 0; i + 1 < NTC_CALIB_LEN; i++) {
		int mv0 = table[i].mv, mv1 = table[i + 1].mv;

		if ((int)mv <= mv0 && (int)mv >= mv1) {
			int t0 = table[i].temp_c, t1 = table[i + 1].temp_c;

			return t0 + ((int)mv - mv0) * (t1 - t0) / (mv1 - mv0);
		}
	}
	return table[NTC_CALIB_LEN - 1].temp_c; /* unreachable */
}

static unsigned duty_for_temp(int temp)
{
	if (temp <= curve[0].temp_c)
		return curve[0].duty;
	if (temp >= curve[CURVE_LEN - 1].temp_c)
		return curve[CURVE_LEN - 1].duty;
	for (unsigned i = 0; i + 1 < CURVE_LEN; i++) {
		int t0 = curve[i].temp_c, t1 = curve[i + 1].temp_c;

		if (temp >= t0 && temp <= t1) {
			int d0 = curve[i].duty, d1 = curve[i + 1].duty;

			return d0 + (2 * (temp - t0) * (d1 - d0) + (t1 - t0)) / (2 * (t1 - t0));
		}
	}
	return curve[CURVE_LEN - 1].duty; /* unreachable */
}

void autocurve_update(uint32_t now_ms)
{
	if (!active)
		return;
	if (ms_since(last_update_ms, now_ms) < UPDATE_INTERVAL_MS)
		return;
	last_update_ms = now_ms;

	for (unsigned ch = 0; ch < FAN_COUNT; ch++) {
		if (ain_valid(ch))
			fan_set_duty(ch, duty_for_temp(temp_c_for_mv(ch, thermal_ain_mv(ch))));
		else
			fan_set_duty(ch, 100);
	}
}
