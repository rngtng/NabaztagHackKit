/**
 * @file uart.h
 * @brief Minimal polled UART0 driver (TX + RX) for firmwareV2.
 *
 * OKI ML67Q4051 UART0 on port PB0 (TX) / PB1 (RX), 38400 baud 8N1, no flow
 * control. Trimmed port of the proven V1 driver (mtl/firmware/src/hal/uart.c):
 * polled TX + polled RX, no interrupts, no XMODEM. V1 drove RX off an interrupt
 * (UARTIER_ERBF -> a ring-buffer ISR); firmwareV2 has no RX handler wired into
 * the IRQ table, so an enabled RX IRQ would fault on the first byte - RX is read
 * by polling the LSR data-ready bit instead (getch_uart), leaving IER at 0.
 *
 * RX carries the Lua REPL's input on the wire (getch_uart); TX carries print()
 * and the prompt - this UART is the console. See lua/firmware/README.md.
 *
 * Baud divisor: UARTDL = F_uart / (baud * 16). The UART peripheral clock was
 * MEASURED at 8.00 MHz (NOT the 33 MHz CPU clock, nor the 32 MHz the V1 header
 * claimed): a 0x55 stream at a known divisor gave a 1.566 ms bit period. At
 * 8 MHz, 115200 is unreachable (the V1 DLL=0x11 yields ~29.4 kbaud, near no
 * standard rate - hence pure garble on the wire). DLL=13 -> 8e6/(16*13) =
 * 38462 baud = 38400 +0.16%, a clean fit any host UART decodes.
 */
#ifndef _UART_H
#define _UART_H

#include <stdint.h>

#define DLM_BAUD  0x00  /**< @brief divisor latch MSB - 38400 baud @ 8 MHz F_uart */
#define DLL_BAUD  0x0D  /**< @brief divisor latch LSB - 38400 baud (8e6/(16*13)) */

/** @brief Configure UART0 pins + 38400 8N1, polled (no interrupts). */
void init_uart(void);
/** @brief Blocking write of one byte (spins on THR-empty). */
void putch_uart(uint8_t c);
/** @brief Blocking write of a NUL-terminated string. */
void putst_uart(uint8_t *str);
/** @brief Non-blocking read of one byte: 0-255 if RX FIFO had data, else -1. */
int getch_uart(void);
/** @brief Non-consuming peek: 1 if a byte waits in the RX FIFO, else 0. */
uint8_t rxrdy_uart(void);

#endif
