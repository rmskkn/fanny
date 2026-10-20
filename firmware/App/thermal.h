/*
 * Analog inputs: AIN1..AIN6 (sensor headers), 12 V rail, VDDA and MCU temperature.
 * ADC1 scans continuously into RAM by DMA; values are low-pass filtered.
 * Sensor values are reported in mV; conversion to physical units (e.g. NTC
 * temperature) is left to the host, which knows what is plugged in.
 */
#ifndef THERMAL_H
#define THERMAL_H

#include <stdint.h>

/* Pure conversion helpers, shared with tests. raw values are 12-bit. */
static inline unsigned thermal_vdda_from_vref(unsigned vref_raw)
{
	return vref_raw ? 1200u * 4095u / vref_raw : 0; /* VREFINT typ. 1.20 V */
}

static inline unsigned thermal_raw_to_mv(unsigned raw, unsigned vdda_mv)
{
	return raw * vdda_mv / 4095u;
}

/* MCU temperature in 0.1 degC: (V25 - Vsense) / Avg_Slope + 25, V25 1.43 V, 4.3 mV/degC. */
static inline int thermal_mcu_decic(unsigned sense_mv)
{
	return (1430 - (int)sense_mv) * 100 / 43 + 250;
}

void thermal_init(void);
/* Call from the main loop; filters once per millisecond. */
void thermal_update(uint32_t now_ms);

/* k is 0-based. */
unsigned thermal_ain_raw(unsigned k);
unsigned thermal_ain_mv(unsigned k);
unsigned thermal_v12_mv(void);
unsigned thermal_vdda_mv(void);
int thermal_mcu_temp_decic(void);

#endif
