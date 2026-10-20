#include <libopencm3/stm32/adc.h>
#include <libopencm3/stm32/dma.h>
#include <libopencm3/stm32/gpio.h>
#include <libopencm3/stm32/rcc.h>

#include "board.h"
#include "thermal.h"

/* Scan order: AIN1..AIN6, 12 V sense, temperature sensor, VREFINT. */
enum { SLOT_V12 = AIN_COUNT, SLOT_TEMP, SLOT_VREF, SLOT_COUNT };

/* 12 V divider: 47k over 10k. */
#define V12_DIV_NUM 57u
#define V12_DIV_DEN 10u

/* IIR filter: acc holds 2^FILTER_SHIFT times the average. ~16 ms time constant. */
#define FILTER_SHIFT 4

static uint8_t sequence[SLOT_COUNT] = { 0, 1, 2, 3, 4, 5, 8, ADC_CHANNEL_TEMP, ADC_CHANNEL_VREF };
static volatile uint16_t samples[SLOT_COUNT];
static uint32_t acc[SLOT_COUNT];
static uint32_t last_ms;

static unsigned filtered(unsigned slot)
{
	return acc[slot] >> FILTER_SHIFT;
}

void thermal_init(void)
{
	rcc_periph_clock_enable(RCC_GPIOA);
	rcc_periph_clock_enable(RCC_GPIOB);
	rcc_periph_clock_enable(RCC_ADC1);
	rcc_periph_clock_enable(RCC_DMA1);

	gpio_set_mode(GPIOA, GPIO_MODE_INPUT, GPIO_CNF_INPUT_ANALOG,
		      GPIO0 | GPIO1 | GPIO2 | GPIO3 | GPIO4 | GPIO5);
	gpio_set_mode(GPIOB, GPIO_MODE_INPUT, GPIO_CNF_INPUT_ANALOG, GPIO0);

	dma_channel_reset(DMA1, DMA_CHANNEL1);
	dma_set_peripheral_address(DMA1, DMA_CHANNEL1, (uint32_t)&ADC_DR(ADC1));
	dma_set_memory_address(DMA1, DMA_CHANNEL1, (uint32_t)samples);
	dma_set_number_of_data(DMA1, DMA_CHANNEL1, SLOT_COUNT);
	dma_set_read_from_peripheral(DMA1, DMA_CHANNEL1);
	dma_enable_memory_increment_mode(DMA1, DMA_CHANNEL1);
	dma_set_peripheral_size(DMA1, DMA_CHANNEL1, DMA_CCR_PSIZE_16BIT);
	dma_set_memory_size(DMA1, DMA_CHANNEL1, DMA_CCR_MSIZE_16BIT);
	dma_enable_circular_mode(DMA1, DMA_CHANNEL1);
	dma_enable_channel(DMA1, DMA_CHANNEL1);

	/* ADCCLK = 72 MHz / 6 = 12 MHz; 239.5 cycles suits the 1k + 100 nF input filter. */
	rcc_set_adcpre(RCC_CFGR_ADCPRE_DIV6);
	adc_power_off(ADC1);
	adc_enable_scan_mode(ADC1);
	adc_set_continuous_conversion_mode(ADC1);
	adc_set_right_aligned(ADC1);
	adc_set_sample_time_on_all_channels(ADC1, ADC_SMPR_SMP_239DOT5CYC);
	adc_set_regular_sequence(ADC1, SLOT_COUNT, sequence);
	adc_enable_temperature_sensor();
	adc_enable_external_trigger_regular(ADC1, ADC_CR2_EXTSEL_SWSTART);
	adc_enable_dma(ADC1);

	adc_power_on(ADC1);
	for (volatile int i = 0; i < 800; i++) /* tSTAB >= 1 us */
		;
	adc_reset_calibration(ADC1);
	adc_calibrate(ADC1);
	adc_start_conversion_regular(ADC1);

	/* Seed the filters so first readings are not ramping up from zero. */
	for (volatile int i = 0; i < 20000; i++)
		;
	for (unsigned s = 0; s < SLOT_COUNT; s++)
		acc[s] = (uint32_t)samples[s] << FILTER_SHIFT;
}

void thermal_update(uint32_t now_ms)
{
	if (now_ms == last_ms)
		return;
	last_ms = now_ms;
	for (unsigned s = 0; s < SLOT_COUNT; s++)
		acc[s] += samples[s] - (acc[s] >> FILTER_SHIFT);
}

unsigned thermal_ain_raw(unsigned k)
{
	return filtered(k);
}

unsigned thermal_vdda_mv(void)
{
	return thermal_vdda_from_vref(filtered(SLOT_VREF));
}

unsigned thermal_ain_mv(unsigned k)
{
	return thermal_raw_to_mv(filtered(k), thermal_vdda_mv());
}

unsigned thermal_v12_mv(void)
{
	return thermal_raw_to_mv(filtered(SLOT_V12), thermal_vdda_mv()) * V12_DIV_NUM / V12_DIV_DEN;
}

int thermal_mcu_temp_decic(void)
{
	return thermal_mcu_decic(thermal_raw_to_mv(filtered(SLOT_TEMP), thermal_vdda_mv()));
}
