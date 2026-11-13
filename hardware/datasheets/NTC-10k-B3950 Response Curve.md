# NTC (10 kΩ, B=3950) response curve

SIG voltage at the ADC input, 10 kΩ pull-up to 3V3A, R25=10 kΩ, B=3950 K

**R25** 10.0 kΩ **B(25/85)** 3950 K **VDDA** 3300 mV **R_pullup** 10.0 kΩ **Range** −55…125 °C

25 °C calibration point — SIG = VDDA/2 (R_ntc = R_pullup)

This is the probe bundled with the Skip0ner 1X4L fan-1248 conf v1 module
(10 kΩ, B=3950; no separate manufacturer part number published). Single-Beta
model from those two constants. Accurate near 0-85 °C where the fan curve
operates; drifts from the real part toward the −55/125 °C extremes since no
full R-T table is published. Same equation as `host/fancontrold.py`'s
`NtcCurveChannel.temperature_c()` run in reverse, and the same values as
`firmware/App/ntc_calib.h`'s lookup table.

| °C | R_ntc (Ω) | SIG (mV) |
|---|---|---|
| 20 | 12535 | 1836 |
| 30 | 8037 | 1470 |
| 40 | 5301 | 1143 |
| 50 | 3588 | 871 |
| 60 | 2486 | 657 |
| 70 | 1760 | 494 |
| 80 | 1270 | 372 |
| 90 | 934 | 282 |
