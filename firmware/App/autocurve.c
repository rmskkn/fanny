#include <math.h>

#include "autocurve.h"
#include "board.h"
#include "fan.h"
#include "thermal.h"

/* Divider bias resistor on each AIN channel equals the NTC's R25 (10 k),
 * matching host/fancontrol.conf.example's defaults and board.h's AIN bias
 * resistors (R27-R30). */
#define NTC_R25   10000.0f
#define NTC_BETA  3950.0f
#define NTC_T25_K (25.0f + 273.15f)

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

static float ntc_temp_c(unsigned ch)
{
	float mv = (float)thermal_ain_mv(ch);
	float vdda = (float)thermal_vdda_mv();
	float r_ntc = NTC_R25 * mv / (vdda - mv);
	float inv_t = 1.0f / NTC_T25_K + logf(r_ntc / NTC_R25) / NTC_BETA;

	return 1.0f / inv_t - 273.15f;
}

static unsigned duty_for_temp(float temp)
{
	if (temp <= curve[0].temp_c)
		return curve[0].duty;
	if (temp >= curve[CURVE_LEN - 1].temp_c)
		return curve[CURVE_LEN - 1].duty;
	for (unsigned i = 0; i + 1 < CURVE_LEN; i++) {
		int t0 = curve[i].temp_c, t1 = curve[i + 1].temp_c;

		if (temp >= t0 && temp <= t1) {
			float frac = (temp - t0) / (float)(t1 - t0);

			return (unsigned)(curve[i].duty + frac * (curve[i + 1].duty - curve[i].duty) + 0.5f);
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
			fan_set_duty(ch, duty_for_temp(ntc_temp_c(ch)));
		else
			fan_set_duty(ch, 100);
	}
}
