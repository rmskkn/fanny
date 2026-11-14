/*
 * Per-channel mV -> degC calibration for the onboard autocurve NTC
 * conversion (see autocurve.c). One table per AIN channel so a channel's
 * thermistor/pullup/VDDA pairing can be recalibrated on its own; all four
 * use the nominal 10k/B3950 NTC against its matching 10k AIN pullup
 * (R27-R30) at VDDA 3300 mV.
 *
 * Resistance points taken from the "10k 3950" column of the reference
 * chart at
 * https://www.focusensing.com/10k-thermistor-chart-type-2-vs-type-3-vs-3950-tables_n196,
 * converted to mv via mv = 3300 * R / (R + 10000).
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
	{ 1833, 20 }, { 1472, 30 }, { 1145, 40 }, { 871, 50 },
	{ 654, 60 }, { 489, 70 }, { 365, 80 }, { 274, 90 },
};

static const struct mv_point ntc_calib_ain2[NTC_CALIB_LEN] = {
	{ 1833, 20 }, { 1472, 30 }, { 1145, 40 }, { 871, 50 },
	{ 654, 60 }, { 489, 70 }, { 365, 80 }, { 274, 90 },
};

static const struct mv_point ntc_calib_ain3[NTC_CALIB_LEN] = {
	{ 1833, 20 }, { 1472, 30 }, { 1145, 40 }, { 871, 50 },
	{ 654, 60 }, { 489, 70 }, { 365, 80 }, { 274, 90 },
};

static const struct mv_point ntc_calib_ain4[NTC_CALIB_LEN] = {
	{ 1833, 20 }, { 1472, 30 }, { 1145, 40 }, { 871, 50 },
	{ 654, 60 }, { 489, 70 }, { 365, 80 }, { 274, 90 },
};

/* Indexed 0-based by channel (channel N <- AIN N+1), matching autocurve.c's
 * loop variable. */
static const struct mv_point *const ntc_calib_table[FAN_COUNT] = {
	ntc_calib_ain1, ntc_calib_ain2, ntc_calib_ain3, ntc_calib_ain4,
};

#endif
