/*
 * Board pin map. Must match hardware/kicad schematic (see docs in README.md).
 *
 *   FANn power gate  (TIM2 full remap, ~30 Hz): PA15 PB3 PB10 PB11
 *   FANn PWM signal  (TIM4, 25 kHz):           PB6  PB7 PB8  PB9
 *   FANn tach        (EXTI12..15, falling):    PB12 PB13 PB14 PB15
 *   AIN1..AIN6       (ADC1 IN0..IN5):          PA0..PA5
 *   12 V sense       (ADC1 IN8):               PB0  (47k/10k divider)
 *   USB FS                                     PA11 (DM), PA12 (DP)
 *   SWD                                        PA13, PA14
 *   Status LED       (active low)              PC13
 */
#ifndef BOARD_H
#define BOARD_H

#include <stdint.h>

#define FAN_COUNT 4
#define AIN_COUNT 6

/* Milliseconds since boot, from SysTick (Core/main.c). */
uint32_t millis(void);

#endif
