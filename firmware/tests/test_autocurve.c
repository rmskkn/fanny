/* SPDX-License-Identifier: GPL-3.0-only */
/*
 * Host-side tests for the autocurve NTC mv->degC->duty lookup (autocurve.c).
 * Black-box: drives the module through its public API and the thermal/fan
 * fakes below, same style as test_protocol.c.
 */
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

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

/* Stands in for curve_store.c's flash page; starts erased (0xFF fill), same
 * as real flash, so autocurve_init() sees the same boot state it would on
 * fresh hardware. */
static uint8_t fake_flash[256];

void curve_store_read(void *data, size_t size) { memcpy(data, fake_flash, size); }
void curve_store_write(const void *data, size_t size) { memcpy(fake_flash, data, size); }

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

static void step_host(bool host_lost)
{
	autocurve_update(now_ms, host_lost);
	now_ms += 1000;
}

static void step(void)
{
	step_host(false);
}

int main(void)
{
	/* Starts erased, like real flash on first boot. */
	memset(fake_flash, 0xFF, sizeof(fake_flash));
	autocurve_init();
	check("erased flash boots with auto off", autocurve_active(), false);

	/* Both channels use the nominal 10k/B3950 mv->degC table quoted in
	 * the comments below; these checks use channels 1 and 2. */
	autocurve_set(true);

	/* No curve table has been set yet for either channel: a valid sensor
	 * reading still forces 100 %, same as an invalid one. */
	set_ch(1, 871, 2048); /* would be 50 C -> 55 if a table were set */
	step();
	check("no table set forces full duty", duties[1], 100);
	check("curve status is none", autocurve_curve_status(1), CURVE_NONE);

	/* A malformed table is rejected and leaves the channel without one. */
	check("empty curve rejected", autocurve_set_curve(1, "") != NULL, true);
	check("duty out of range rejected", autocurve_set_curve(1, "30:101") != NULL, true);
	check("non-ascending temps rejected", autocurve_set_curve(1, "40:20,30:35") != NULL, true);
	check("bad syntax rejected", autocurve_set_curve(1, "30-20") != NULL, true);
	/* A temp that parses fine as an int but doesn't fit the int16_t it's
	 * stored in must be rejected, not silently narrowed into garbage. */
	check("out-of-range temp rejected", autocurve_set_curve(1, "40000:50") != NULL, true);
	check("rejected table leaves channel unset", autocurve_curve_status(1), CURVE_NONE);
	/* Only the 0-200 degC domain is a valid curve point, not just whatever
	 * fits in int16_t. */
	check("negative temp rejected", autocurve_set_curve(1, "-1:50") != NULL, true);
	check("temp above 200 rejected", autocurve_set_curve(1, "201:50") != NULL, true);

	if (autocurve_set_curve(1, "30:20,40:35,50:55,60:75,70:100") != NULL)
		check("set_curve on channel 1 failed", 1, 0);
	if (autocurve_set_curve(2, "30:20,40:35,50:55,60:75,70:100") != NULL)
		check("set_curve on channel 2 failed", 1, 0);
	check("curve status is ok", autocurve_curve_status(1), CURVE_OK);

	/* Simulates a reset: reloading from the (fake) flash page gets the
	 * same table back without the host resending it. */
	autocurve_init();
	check("curve survives reinit", autocurve_curve_status(1), CURVE_OK);
	check("channel 2's curve survives too", autocurve_curve_status(2), CURVE_OK);
	check("auto on survives reinit", autocurve_active(), true);
	/* The page as it stands with auto on, for the reset check further down. */
	uint8_t flash_auto_on[sizeof fake_flash];
	memcpy(flash_auto_on, fake_flash, sizeof flash_auto_on);

	/* Read-back accessors (protocol.c's `curve` query uses these) report
	 * exactly the points that were set, in order. */
	check("stored point count", autocurve_curve_count(1), 5);
	check("stored point 0 temp", (unsigned)autocurve_curve_temp(1, 0), 30);
	check("stored point 0 duty", autocurve_curve_duty(1, 0), 20);
	check("stored point 4 temp", (unsigned)autocurve_curve_temp(1, 4), 70);
	check("stored point 4 duty", autocurve_curve_duty(1, 4), 100);

	/* Above the table's top entry (1833 mv/20 C): clamps to 20 C, which
	 * is also below the duty curve's first point, so duty clamps too. */
	set_ch(1, 2500, 2048);
	step();
	check("clamp-high mv -> clamp-low duty", duties[1], 20);

	/* Exact table point: 871 mv -> 50 C -> duty curve point (50, 55). */
	set_ch(1, 871, 2048);
	step();
	check("exact table point", duties[1], 55);

	/* Between two table points and two curve points: 1300 mv -> ~35 C
	 * (between 30/1472 and 40/1145) -> duty 28 (between 30/20 and 40/35). */
	set_ch(1, 1300, 2048);
	step();
	check("interpolated mv and duty", duties[1], 28);

	/* Below the table's bottom entry (274 mv/90 C): clamps to 90 C, above
	 * the duty curve's last point, so duty clamps to 100. */
	set_ch(1, 100, 2048);
	step();
	check("clamp-low mv -> clamp-high duty", duties[1], 100);

	/* Open/shorted sensor (raw outside the valid band) forces 100 %
	 * regardless of mv, even a mv that would otherwise read as cool. */
	set_ch(1, 1833, 10);
	step();
	check("invalid sensor forces full duty", duties[1], 100);

	/* Each channel reads its own AIN independently in the same update. */
	set_ch(1, 871, 2048);  /* 50 C -> 55 */
	set_ch(2, 1300, 2048); /* 35 C -> 28 */
	step();
	check("channel 1 independent", duties[1], 55);
	check("channel 2 independent", duties[2], 28);

	/* A table that fails its CRC check (corrupted in storage, not just
	 * never set) is treated the same as a missing one: 100 %. */
	autocurve_corrupt_for_test(1);
	check("corrupted table detected", autocurve_curve_status(1), CURVE_BAD);
	set_ch(1, 871, 2048); /* would be 50 C -> 55 with a good table */
	step();
	check("corrupted table forces full duty", duties[1], 100);
	check("corruption on ch 1 does not affect ch 2", autocurve_curve_status(2), CURVE_OK);

	/* auto off: update() is a no-op, duties stay put even if mv changes.
	 * duties[1] is 100 here, left over from the corrupted-table check above. */
	autocurve_set(false);
	set_ch(1, 100, 2048);
	step();
	check("auto off is a no-op", duties[1], 100);
	autocurve_init();
	check("auto off survives reinit", autocurve_active(), false);

	/* A reset with auto on saved must come back with auto on, even though
	 * the RAM state right before it says off. */
	memcpy(fake_flash, flash_auto_on, sizeof fake_flash);
	autocurve_init();
	check("saved auto on restored at boot", autocurve_active(), true);
	autocurve_set(false);

	/* Host-loss fallback with auto off: the saved curves take over, and a
	 * channel with no table still runs at 100 %. Tables 1 and 2 are good
	 * here (the restored page predates the corruption above). */
	set_ch(1, 871, 2048); /* 50 C -> 55 */
	duties[1] = 0;
	step();
	check("auto off with host present is a no-op", duties[1], 0);
	step_host(true);
	check("host-loss fallback runs the curve", duties[1], 55);
	check("fallback channel with no table runs full", duties[0], 100);

	/* One corrupted table in the fallback sends every fan to 100 %, the
	 * good channel 1 included. */
	autocurve_corrupt_for_test(2);
	step_host(true);
	check("fallback with corrupted table: ch 1 full", duties[1], 100);
	check("fallback with corrupted table: ch 2 full", duties[2], 100);

	/* `auto on` keeps the per-channel rule: only the bad channel goes full. */
	autocurve_set(true);
	step();
	check("auto on with corrupted ch 2 still runs ch 1", duties[1], 55);
	autocurve_set(false);
	if (autocurve_set_curve(2, "30:20,40:35,50:55,60:75,70:100") != NULL)
		check("restoring channel 2's curve failed", 1, 0);

	/* A normal, human-scale curve spanning a wide 0-200 degC gradation is
	 * unaffected by the out-of-range check above: every point here is
	 * well within int16_t, so all five must still be accepted and stored. */
	if (autocurve_set_curve(3, "0:10,50:40,100:70,150:90,200:100") != NULL)
		check("set_curve with 0-200 gradation failed", 1, 0);
	check("0-200 gradation curve status is ok", autocurve_curve_status(3), CURVE_OK);
	check("0-200 gradation point count", autocurve_curve_count(3), 5);
	check("0-200 gradation point 0 temp", (unsigned)autocurve_curve_temp(3, 0), 0);
	check("0-200 gradation point 0 duty", autocurve_curve_duty(3, 0), 10);
	check("0-200 gradation point 2 temp", (unsigned)autocurve_curve_temp(3, 2), 100);
	check("0-200 gradation point 2 duty", autocurve_curve_duty(3, 2), 70);
	check("0-200 gradation point 4 temp", (unsigned)autocurve_curve_temp(3, 4), 200);
	check("0-200 gradation point 4 duty", autocurve_curve_duty(3, 4), 100);

	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("all autocurve tests passed\n");
	return 0;
}
