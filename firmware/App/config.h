/* Tunable behavior knobs, gathered in one place. Hardware addresses,
 * protocol/buffer sizes, and values derived from the clock or a table are
 * not here; they stay next to the code that depends on them. */
#ifndef CONFIG_H
#define CONFIG_H

/* autocurve.c */
/* Readings this close to either rail mean the sensor is open or shorted,
 * not a real temperature. */
#define AIN_RAW_MIN_VALID 20u
#define AIN_RAW_MAX_VALID 4075u
#define UPDATE_INTERVAL_MS 1000u
#define CURVE_TEMP_MIN_C 0
#define CURVE_TEMP_MAX_C 200
/* Bounded by protocol.c's LINE_MAX: "curve 1 " plus this many "-40:100,"
 * points must fit in one line. */
#define MAX_CURVE_POINTS 6u

/* tach.h / tach.c */
#define TACH_PULSES_PER_REV 2u /* Intel 4-wire fan spec */
#define TACH_WINDOW_US      1000000u

/* fan.c: a stopped fan may not start at a low duty, so start it at
 * FAN_KICK_PCT for FAN_KICK_MS before dropping to the requested duty. */
#define FAN_KICK_PCT 30u
#define FAN_KICK_MS  2000u

/* thermal.c */
/* 12 V divider: 10k over 3.3k. */
#define V12_DIV_NUM 133u
#define V12_DIV_DEN 33u
/* IIR filter: acc holds 2^FILTER_SHIFT times the average. ~16 ms time constant. */
#define FILTER_SHIFT 4

/* safety.h / safety.c */
#define SAFETY_HOST_TIMEOUT_MS 20000u
#define SAFETY_STALL_S         3u

/* pwm.c: NF-P12 PWM neither starts nor keeps running below ~10 % real duty,
 * so 1..100 % is mapped linearly onto 10..100 %; 0 % stays off. */
#define PWM_MIN_PCT 10u

#endif
