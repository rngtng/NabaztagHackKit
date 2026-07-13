/**
 * @file adc.h
 * @brief Analog-to-digital access for the back wheel.
 *
 * The wheel is wired to ADC channel 2 (PD2), per src/firmware/src/main.c's
 * bring-up (`ADCON1_CH2` + the PD2 -> ADC2 pin mux). Ported register sequence
 * only - the wheel's *meaning* (a volume pot) is still to be hardware-confirmed;
 * see nab.wheel() in src/app/lua.c.
 */
#ifndef _ADC_H_
#define _ADC_H_

#include <stdint.h>

/** @brief Mux PD2 to ADC ch.2 and set the conversion clock. Call once at startup. */
void init_adc(void);

/** @brief Blocking single-shot read of ADC channel 2.
 *  @return 8 most significant bits of the conversion result. */
uint8_t adc_read_ch2(void);

#endif
