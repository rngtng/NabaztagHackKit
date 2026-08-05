/**
 * @file adc.c
 * @brief Analog-to-digital access for the back wheel.
 *
 * Trimmed port of mtl/firmware's ADC bring-up. Select-mode (not scan-mode)
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

/* Give up on a converter that never reports done. A select-mode conversion at
 * ADCON2_CLK32 takes under a microsecond, so this bound is ~5 orders of
 * magnitude of slack - it only bites when the ADC is wedged.
 *
 * The bound is the point: the poll feeds the watchdog while it waits, so an
 * unbounded wait is not a slow read but a permanent hang with the one thing
 * that could have broken it explicitly suppressed. Any polling loop that clears
 * the watchdog needs a guard count for the same reason. */
#define ADC_POLL_MAX 100000UL

uint8_t adc_read_ch2(void)
{
  unsigned long guard = ADC_POLL_MAX;

  set_hbit(ADCON1, ADCON1_STS | ADCON1_CH2);
  while ((get_hvalue(ADCON1) & ADCON1_STS) && --guard)
    CLR_WDT;
  /* Out of budget: report the last conversion result rather than invent one -
   * ADR2 still holds whatever the ADC last completed, and the caller (a volume
   * knob) wants a plausible reading, not a sentinel it has no way to spot. */
  return (uint8_t)(get_hvalue(ADR2) >> 2);
}
