/*
 * Board pin map. Must match hardware/kicad schematic (see docs in README.md).
 *
 *   FANn PWM signal  (TIM4, 25 kHz):           PB8  PB7 PB6  PB9
 *     (PCB crosses TIM4_CH1/CH3: PB6 reaches the FAN3 header, PB8 reaches
 *     the FAN1 header; App/pwm.c's oc[] array maps fan index to channel)
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
