/*
 * Autonomous mode: no host needed. While active, every channel's duty is
 * recomputed once a second from its onboard NTC thermistor (channel N
 * reads AIN N) via that channel's own temp->duty table, set at runtime by
 * the `curve` host command (see protocol.h) from its config and kept in
 * flash (curve_store.c) so it survives a reset. A channel with no table
 * set, or whose stored table fails its CRC check, and any channel whose
 * sensor reading is out of range (open or shorted probe), runs at 100 %.
 */
#ifndef AUTOCURVE_H
#define AUTOCURVE_H

#include <stdbool.h>
#include <stdint.h>

/* Call once at boot, before autocurve_set(), to load any curve tables
 * saved by a previous session. */
void autocurve_init(void);

void autocurve_set(bool on);
bool autocurve_active(void);

/* Call every main loop pass; it throttles itself to once a second. */
void autocurve_update(uint32_t now_ms);

/* NTC temperature for channel ch's AIN, via the same calibration table and
 * validity check autocurve_update() uses. *valid is false when the reading
 * is out of range (sensor open or shorted); temp_c is then the table's
 * clamped boundary value, not a real reading. */
int autocurve_ntc_temp_c(unsigned ch, bool *valid);

enum curve_status {
	CURVE_NONE, /* never set since boot */
	CURVE_OK,
	CURVE_BAD,  /* stored table fails its CRC check */
};

/* Parses "temp:duty,temp:duty,..." (points sorted by rising temp, duty
 * 0-100) and stores it for channel ch with a fresh CRC. Returns NULL on
 * success, else an error string; a rejected table leaves ch's existing one
 * (if any) untouched. ch is 0-based. */
const char *autocurve_set_curve(unsigned ch, const char *text);

enum curve_status autocurve_curve_status(unsigned ch);

/* Channel ch's current table, for the `curve` query command (protocol.c)
 * to echo back. Valid only when autocurve_curve_status(ch) == CURVE_OK;
 * callers must check that first. ch and i are 0-based. */
unsigned autocurve_curve_count(unsigned ch);
int autocurve_curve_temp(unsigned ch, unsigned i);
unsigned autocurve_curve_duty(unsigned ch, unsigned i);

#ifdef AUTOCURVE_TEST_HOOKS
/* Flips a byte in channel ch's stored table without updating its CRC, so
 * tests can prove the CRC check catches corruption. ch is 0-based. */
void autocurve_corrupt_for_test(unsigned ch);
#endif

#endif
