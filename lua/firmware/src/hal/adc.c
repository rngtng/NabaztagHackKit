/**
 * @file adc.c
 * @brief Analog-to-digital access for the back wheel (issue #123).
 *
 * Trimmed port of the ADC bring-up in src/firmware/src/main.c (`ADCON0`/
 * `ADCON1`/`ADCON2` init, PD2 -> ADC2 pin mux) plus the register sequence in
 * src/firmware/src/hal/audio.c's `get_adc_value`. Select-mode (not scan-mode)
 * single-channel conversion, matching the original.
 */
#include "ml674061.h"
#include "common.h"

#include "hal/adc.h"

void init_adc(void)
{
  /* PORTSEL2 packs 2 mux-select bits per pin starting at bit 16 for the PD
   * group; field value 1 = secondary function. Set only PD2's field (bits
   * 21:20) to ADC2 - PD0/PD1 (EXINT2/3, the latter shared with the head
   * button's P3.1) are left untouched. */
  set_wbit(PORTSEL2, 0x00100000);

  put_hvalue(ADCON0, 0x0000);        /* select mode (not scan) */
  put_hvalue(ADCON2, ADCON2_CLK32);  /* conversion clock: 33MHz/32, >=800ns/conv */
}

uint8_t adc_read_ch2(void)
{
  set_hbit(ADCON1, ADCON1_STS | ADCON1_CH2);
  while (get_hvalue(ADCON1) & ADCON1_STS)
    CLR_WDT;
  return (uint8_t)(get_hvalue(ADR2) >> 2);
}
