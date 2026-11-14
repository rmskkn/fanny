# NTC (10 kΩ, B=3950) response curve

SIG voltage at the ADC input, 10 kΩ pull-up to 3V3A, R25=10 kΩ, B=3950 K

**R25** 10.0 kΩ **B(25/85)** 3950 K **VDDA** 3300 mV **R_pullup** 10.0 kΩ **Range** −55…125 °C

25 °C calibration point — SIG = VDDA/2 (R_ntc = R_pullup)

This is the probe bundled with the Skip0ner 1X4L fan-1248 conf v1 module
(10 kΩ, B=3950; no separate manufacturer part number published). Resistance
points come from the "10k 3950" column of Focusensing's
[10k thermistor reference chart](https://www.focusensing.com/10k-thermistor-chart-type-2-vs-type-3-vs-3950-tables_n196),
not a single-Beta model, so they hold up over the full published range
rather than drifting near −55/125 °C. Same equation as
`host/fancontrold.py`'s `NtcCurveChannel.temperature_c()` run in reverse,
and the same values as `firmware/App/ntc_calib.h`'s lookup table.

| °C | R_ntc (Ω) | SIG (mV) |
|---|---|---|
| 20 | 12501 | 1833 |
| 30 | 8051 | 1472 |
| 40 | 5315 | 1145 |
| 50 | 3588 | 871 |
| 60 | 2473 | 654 |
| 70 | 1738 | 489 |
| 80 | 1243 | 365 |
| 90 | 904 | 274 |
