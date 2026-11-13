/*
 * Host-side tests for the autocurve NTC mv->degC->duty lookup (autocurve.c).
 * Black-box: drives the module through its public API and the thermal/fan
 * fakes below, same style as test_protocol.c.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>

#include "autocurve.h"
#include "board.h"
#include "fan.h"
#include "thermal.h"

/* ---- fakes ---- */

static unsigned fake_raw[FAN_COUNT] = { 2048, 2048, 2048, 2048 };
static unsigned fake_mv[FAN_COUNT] = { 1650, 1650, 1650, 1650 };
static unsigned duties[FAN_COUNT] = { 0, 0, 0, 0 };

bool fan_set_duty(unsigned ch, unsigned d) { duties[ch] = d; return true; }
unsigned thermal_ain_raw(unsigned k) { return fake_raw[k]; }
unsigned thermal_ain_mv(unsigned k) { return fake_mv[k]; }

/* ---- harness ---- */

static int failures;

static void check(const char *label, unsigned got, unsigned want)
{
	if (got != want) {
		printf("FAIL %s: got %u want %u\n", label, got, want);
		failures++;
	}
}

/* Advances the module's internal 1 s throttle past, then applies the given
 * per-channel mv/raw before running one update. */
static uint32_t now_ms = 1000;

static void set_ch(unsigned ch, unsigned mv, unsigned raw)
{
	fake_mv[ch] = mv;
	fake_raw[ch] = raw;
}

static void step(void)
{
	autocurve_update(now_ms);
	now_ms += 1000;
}

int main(void)
{
	autocurve_set(true);

	/* Above the table's top entry (1836 mv/20 C): clamps to 20 C, which
	 * is also below the duty curve's first point, so duty clamps too. */
	set_ch(0, 2500, 2048);
	step();
	check("clamp-high mv -> clamp-low duty", duties[0], 20);

	/* Exact table point: 871 mv -> 50 C -> duty curve point (50, 55). */
	set_ch(0, 871, 2048);
	step();
	check("exact table point", duties[0], 55);

	/* Between two table points and two curve points: 1300 mv -> 35 C
	 * (between 30/1470 and 40/1143) -> duty 28 (between 30/20 and 40/35). */
	set_ch(0, 1300, 2048);
	step();
	check("interpolated mv and duty", duties[0], 28);

	/* Below the table's bottom entry (282 mv/90 C): clamps to 90 C, above
	 * the duty curve's last point, so duty clamps to 100. */
	set_ch(0, 100, 2048);
	step();
	check("clamp-low mv -> clamp-high duty", duties[0], 100);

	/* Open/shorted sensor (raw outside the valid band) forces 100 %
	 * regardless of mv, even a mv that would otherwise read as cool. */
	set_ch(0, 1836, 10);
	step();
	check("invalid sensor forces full duty", duties[0], 100);

	/* Each channel reads its own AIN independently in the same update. */
	set_ch(0, 871, 2048);  /* 50 C -> 55 */
	set_ch(1, 1300, 2048); /* 35 C -> 28 */
	step();
	check("channel 0 independent", duties[0], 55);
	check("channel 1 independent", duties[1], 28);

	/* auto off: update() is a no-op, duties stay put even if mv changes. */
	autocurve_set(false);
	set_ch(0, 100, 2048);
	step();
	check("auto off is a no-op", duties[0], 55);

	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("all autocurve tests passed\n");
	return 0;
}
