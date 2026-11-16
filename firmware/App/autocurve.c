#include <limits.h>

#include "autocurve.h"
#include "board.h"
#include "config.h"
#include "curve_store.h"
#include "fan.h"
#include "ntc_calib.h"
#include "thermal.h"

struct fan_curve {
	uint8_t count;
	int16_t temp_c[MAX_CURVE_POINTS];
	uint8_t duty[MAX_CURVE_POINTS];
	uint32_t crc;
};

static struct fan_curve curves[FAN_COUNT];

static bool active;
static uint32_t last_update_ms;

/* Standard CRC-32 (poly 0xEDB88320, reflected), bit by bit: the table is a
 * handful of bytes, not worth a lookup table. */
static uint32_t crc32_update(uint32_t crc, uint8_t byte)
{
	crc ^= byte;
	for (int i = 0; i < 8; i++)
		crc = (crc >> 1) ^ ((crc & 1) ? 0xEDB88320u : 0);
	return crc;
}

/* Covers count and exactly the first count points, so unused tail slots
 * never affect it. */
static uint32_t curve_crc(const struct fan_curve *c)
{
	uint32_t crc = 0xFFFFFFFFu;

	crc = crc32_update(crc, c->count);
	for (unsigned i = 0; i < c->count; i++) {
		crc = crc32_update(crc, (uint8_t)(c->temp_c[i] & 0xFF));
		crc = crc32_update(crc, (uint8_t)((c->temp_c[i] >> 8) & 0xFF));
		crc = crc32_update(crc, c->duty[i]);
	}
	return ~crc;
}

void autocurve_init(void)
{
	curve_store_read(curves, sizeof curves);
}

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

static unsigned duty_for_curve(const struct fan_curve *c, int temp)
{
	if (temp <= c->temp_c[0])
		return c->duty[0];
	if (temp >= c->temp_c[c->count - 1])
		return c->duty[c->count - 1];
	for (unsigned i = 0; i + 1 < c->count; i++) {
		int t0 = c->temp_c[i], t1 = c->temp_c[i + 1];

		if (temp >= t0 && temp <= t1) {
			int d0 = c->duty[i], d1 = c->duty[i + 1];

			return d0 + (2 * (temp - t0) * (d1 - d0) + (t1 - t0)) / (2 * (t1 - t0));
		}
	}
	return c->duty[c->count - 1]; /* unreachable */
}

int autocurve_ntc_temp_c(unsigned ch, bool *valid)
{
	*valid = ain_valid(ch);
	return temp_c_for_mv(ch, thermal_ain_mv(ch));
}

static bool parse_int(const char **s, int *out)
{
	bool neg = **s == '-';
	int v = 0;

	if (neg)
		(*s)++;
	if (**s < '0' || **s > '9')
		return false;
	while (**s >= '0' && **s <= '9') {
		int digit = *(*s)++ - '0';

		/* Reject rather than silently wrap: a run of digits long enough
		 * to overflow int must not produce a garbage but "valid" int. */
		if (v > (INT_MAX - digit) / 10)
			return false;
		v = v * 10 + digit;
	}
	*out = neg ? -v : v;
	return true;
}

const char *autocurve_set_curve(unsigned ch, const char *text)
{
	struct fan_curve c = { 0 };
	const char *s = text;

	if (ch >= FAN_COUNT)
		return "bad channel";
	while (*s) {
		int temp, duty;

		if (c.count == MAX_CURVE_POINTS)
			return "too many points";
		if (!parse_int(&s, &temp) || *s != ':')
			return "bad point, want temp:duty";
		if (temp < CURVE_TEMP_MIN_C || temp > CURVE_TEMP_MAX_C)
			return "temp out of range";
		s++;
		if (!parse_int(&s, &duty) || duty < 0 || duty > 100)
			return "duty must be 0-100";
		if (c.count > 0 && temp <= c.temp_c[c.count - 1])
			return "points must be sorted by rising temp";
		c.temp_c[c.count] = (int16_t)temp;
		c.duty[c.count] = (uint8_t)duty;
		c.count++;
		if (*s == ',')
			s++;
		else if (*s != '\0')
			return "bad point, want temp:duty";
	}
	if (c.count == 0)
		return "curve needs at least one point";
	c.crc = curve_crc(&c);
	curves[ch] = c;
	/* Whole-array rewrite: the flash page has no sub-page erase, so every
	 * channel's current table is written back, not just ch's. */
	curve_store_write(curves, sizeof curves);
	return NULL;
}

enum curve_status autocurve_curve_status(unsigned ch)
{
	/* An erased or never-written flash slot reads back as 0xFF fill, so
	 * count must be range-checked before it is trusted as an array index. */
	if (ch >= FAN_COUNT || curves[ch].count == 0 || curves[ch].count > MAX_CURVE_POINTS)
		return CURVE_NONE;
	return curve_crc(&curves[ch]) == curves[ch].crc ? CURVE_OK : CURVE_BAD;
}

/* Only meaningful when autocurve_curve_status(ch) == CURVE_OK; callers
 * (protocol.c's `curve` query) check that first. i is 0-based. */
unsigned autocurve_curve_count(unsigned ch)
{
	return curves[ch].count;
}

int autocurve_curve_temp(unsigned ch, unsigned i)
{
	return curves[ch].temp_c[i];
}

unsigned autocurve_curve_duty(unsigned ch, unsigned i)
{
	return curves[ch].duty[i];
}

#ifdef AUTOCURVE_TEST_HOOKS
void autocurve_corrupt_for_test(unsigned ch)
{
	curves[ch].duty[0] ^= 0xFF;
}
#endif

void autocurve_update(uint32_t now_ms)
{
	if (!active)
		return;
	if (ms_since(last_update_ms, now_ms) < UPDATE_INTERVAL_MS)
		return;
	last_update_ms = now_ms;

	for (unsigned ch = 0; ch < FAN_COUNT; ch++) {
		if (!ain_valid(ch) || autocurve_curve_status(ch) != CURVE_OK) {
			fan_set_duty(ch, 100);
			continue;
		}
		fan_set_duty(ch, duty_for_curve(&curves[ch], temp_c_for_mv(ch, thermal_ain_mv(ch))));
	}
}
