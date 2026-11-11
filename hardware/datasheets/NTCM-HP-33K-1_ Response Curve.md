# NTCM-HP-33K-1% response curve

SIG voltage at the ADC input, 33 kΩ pull-up to 3V3A, R25=33 kΩ, B=3950 K

**R25** 33.0 kΩ **B(25/85)** 3950 K **VDDA** 3300 mV **R_pullup** 33.0 kΩ **Range** −55…125 °C

25 °C calibration point — SIG = VDDA/2 (R_ntc = R_pullup)

Single-Beta model from the two published datasheet constants (R25, B). Accurate near 0–85 °C where the fan curve operates; drifts from the real part toward the −55/125 °C extremes since the datasheet gives no full R–T table. Same equation as `fancontrold.py`'s `NtcCurveChannel.temperature_c()`, run in reverse.