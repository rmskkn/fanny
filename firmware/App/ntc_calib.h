/*
 * Per-channel mV -> degC calibration for the onboard autocurve NTC
 * conversion (see autocurve.c). One table per AIN channel so a channel's
 * thermistor/pullup/VDDA pairing can be recalibrated on its own; all four
 * start identical since every channel uses the same 10k/B3950 NTC against
 * its matching 10k AIN pullup (R27-R30) at VDDA 3300 mV.
 *
 * Each table must stay sorted by descending mv (mv falls as temp rises).
 */
#ifndef NTC_CALIB_H
#define NTC_CALIB_H

#include <stdint.h>

#include "board.h"

struct mv_point {
	int16_t mv;
	int16_t temp_c;
};

#define NTC_CALIB_LEN 8

static const struct mv_point ntc_calib_ain1[NTC_CALIB_LEN] = {
	{ 1836, 20 }, { 1470, 30 }, { 1143, 40 }, { 871, 50 },
	{ 657, 60 }, { 494, 70 }, { 372, 80 }, { 282, 90 },
};

static const struct mv_point ntc_calib_ain2[NTC_CALIB_LEN] = {
	{ 1836, 20 }, { 1470, 30 }, { 1143, 40 }, { 871, 50 },
	{ 657, 60 }, { 494, 70 }, { 372, 80 }, { 282, 90 },
};

static const struct mv_point ntc_calib_ain3[NTC_CALIB_LEN] = {
	{ 1836, 20 }, { 1470, 30 }, { 1143, 40 }, { 871, 50 },
	{ 657, 60 }, { 494, 70 }, { 372, 80 }, { 282, 90 },
};

static const struct mv_point ntc_calib_ain4[NTC_CALIB_LEN] = {
	{ 1836, 20 }, { 1470, 30 }, { 1143, 40 }, { 871, 50 },
	{ 657, 60 }, { 494, 70 }, { 372, 80 }, { 282, 90 },
};

/* Indexed 0-based by channel (channel N <- AIN N+1), matching autocurve.c's
 * loop variable. */
static const struct mv_point *const ntc_calib_table[FAN_COUNT] = {
	ntc_calib_ain1, ntc_calib_ain2, ntc_calib_ain3, ntc_calib_ain4,
};

#endif
