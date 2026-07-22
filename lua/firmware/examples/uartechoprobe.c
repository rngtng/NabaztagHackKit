/**
 * @file uartechoprobe.c
 * @brief UART external-RX probe: echo each received byte back, case-flipped.
 *
 * uartrxprobe proved the RX code path via internal loopback (no wire). This
 * probe proves the *physical* RX line (#203 tap extended host->device, the
 * step-3 wiring): it reads bytes off the real RX pin with getch_uart() and
 * transmits each back XOR 0x20 - so an ASCII letter comes back with its case
 * flipped ('a'->'A'). The transform is deliberate: a plain echo could be faked
 * by a TX/RX short at the connector, but a case-flipped echo can only come from
 * the MCU actually reading and re-transmitting the byte.
 *
 * Flash + test (on the Pi, TX+RX both wired to /dev/serial0):
 *   task lua:firmware:flash EXAMPLE=uartechoprobe
 *   sudo stty -F /dev/serial0 38400 raw -echo
 *   printf 'abcXYZ' > /dev/serial0   # then read: expect 'ABCxyz'
 *
 * Emits a one-shot banner on boot so a listener sees the link is alive before
 * any input arrives; then it is a pure echo loop (no more unsolicited output,
 * so it does not race the echoed bytes).
 */
#include <stdint.h>

#include "hal/uart.h"

int main(void)
{
  init_uart();

  putst_uart((uint8_t *)"NAB-UART-ECHO ready (echoes input ^0x20)\r\n");

  for (;;) {
    int c = getch_uart();
    if (c >= 0)
      putch_uart((uint8_t)(c ^ 0x20));
  }

  return 0;
}
