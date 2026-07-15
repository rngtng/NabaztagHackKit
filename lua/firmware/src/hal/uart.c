/**
 * @file uart.c
 * @brief Minimal polled UART0 driver (TX + RX) for firmwareV2.
 *
 * Trimmed port of the proven V1 driver (mtl/firmware/src/hal/uart.c, itself
 * OKI's 2005 code via RedoX's 2015 GCC port). Init + polled TX + polled RX; no
 * XMODEM. V1's RX was interrupt-driven (UARTIER_ERBF -> a ring-buffer ISR), but
 * firmwareV2 has no RX handler in the IRQ table, so an enabled RX IRQ would
 * fault on the first byte. getch_uart() polls the LSR data-ready bit instead
 * and IER stays 0 (init_uart), matching the polled-TX style.
 *
 * UART0 = 38400 baud, 8 data bits, 1 stop bit, no parity, no flow control
 * (see uart.h - the peripheral clock is a measured 8 MHz, so 115200 is out of
 * reach; DLL_BAUD/DLM_BAUD are chosen for 38400).
 */
#include "ml674061.h"
#include "common.h"

#include "hal/uart.h"

void putch_uart(uint8_t c)
{
  /* wait until the transmit holding register is empty */
  while ((get_value(UARTLSR0) & UARTLSR_THRE) != UARTLSR_THRE);
  put_value(UARTTHR0, c);
}

void putst_uart(uint8_t *str)
{
  while (*str)
    putch_uart(*str++);
}

int getch_uart(void)
{
  /* data-ready clears when the RX FIFO drains, so this is non-blocking:
   * -1 means "nothing yet", the caller polls again (cf. SYS_READC). */
  if ((get_value(UARTLSR0) & UARTLSR_DR) != UARTLSR_DR)
    return -1;
  return (int)(get_value(UARTRBR0) & 0xFF);
}

void init_uart(void)
{
  set_wbit(PORTSEL1, 0x50000);          /* PB0/PB1 -> UART0 secondary function */

  put_value(UARTLCR0, UARTLCR_DLAB);
  put_value(UARTDLL0, DLL_BAUD);        /* baud rate divisor */
  put_value(UARTDLM0, DLM_BAUD);
                                        /* 8 data bits, 1 stop bit, no parity */
  put_value(UARTLCR0, UARTLCR_LEN8 | UARTLCR_STB1 | UARTLCR_PDIS);
                                        /* FIFO on, clear RX+TX FIFOs */
  put_value(UARTFCR0, UARTFCR_FE | UARTFCR_RFLV1 | UARTFCR_RFCLR | UARTFCR_TFCLR);
  clr_bit(UARTFCR0, 0xC0);              /* RX FIFO trigger level: 1 byte */

  put_value(UARTIER0, 0);              /* TX-only: no interrupts (no RX handler) */
  put_value(UARTMCR0, 0);
}
