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

static void step(void)
{
	autocurve_update(now_ms);
	now_ms += 1000;
}

int main(void)
{
	/* Starts erased, like real flash on first boot. */
	memset(fake_flash, 0xFF, sizeof(fake_flash));
	autocurve_init();

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
	check("rejected table leaves channel unset", autocurve_curve_status(1), CURVE_NONE);

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

	if (failures) {
		printf("%d failure(s)\n", failures);
		return 1;
	}
	printf("all autocurve tests passed\n");
	return 0;
}
