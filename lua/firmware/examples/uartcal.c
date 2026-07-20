/**
 * @file uartcal.c
 * @brief UART baud-calibration probe: spew 0x55 at a KNOWN divisor.
 *
 * The OKI ML67Q4051 UART peripheral clock is not the 33 MHz CPU clock, so the
 * "115200 @32 MHz" divisor (DLL=0x11) does not actually produce 115200 on the
 * wire. This app sets a KNOWN, deliberately LOW baud divisor and transmits
 * 0x55 ('U') continuously. 0x55 = 0b01010101, so with the start(0)/stop(1)
 * bits the line becomes a clean alternating square wave at baud/2 - trivial to
 * time on a slow logic capture (Pi gpiomon). Then:
 *
 *   F_uart = measured_baud * 16 * DIVISOR
 *
 * pins the real peripheral clock, from which the correct divisor for any target
 * baud follows. DIVISOR here = 0x030D = 781 (DLM=0x03, DLL=0x0D) -> ~2400 baud
 * if F_uart ~= 30 MHz, slow enough that gpiomon on the Model A+ captures every
 * edge. Not a shipping app - a one-shot measurement tool.
 */
#include <stdint.h>

#include "ml674061.h"
#include "common.h"

#define CAL_DLM  0x03
#define CAL_DLL  0x0D   /* 0x030D = 781 */

int main(void)
{
  set_wbit(PORTSEL1, 0x50000);          /* PB0/PB1 -> UART0 secondary function */

  put_value(UARTLCR0, UARTLCR_DLAB);
  put_value(UARTDLL0, CAL_DLL);
  put_value(UARTDLM0, CAL_DLM);
  put_value(UARTLCR0, UARTLCR_LEN8 | UARTLCR_STB1 | UARTLCR_PDIS);
  put_value(UARTFCR0, UARTFCR_FE | UARTFCR_RFLV1 | UARTFCR_RFCLR | UARTFCR_TFCLR);
  clr_bit(UARTFCR0, 0xC0);
  put_value(UARTIER0, 0);
  put_value(UARTMCR0, 0);

  for (;;) {
    while ((get_value(UARTLSR0) & UARTLSR_THRE) != UARTLSR_THRE);
    put_value(UARTTHR0, 0x55);
  }

  return 0;
}
