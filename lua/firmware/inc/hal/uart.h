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
 * Baud divisor: UARTDL = F_uart / (baud * 16). init.s runs init_pll() before
 * main(), so the UART peripheral clock (= APB = CPU) is 32 MHz for every image.
 * Console at 115200 (#271, was 38400): DLL=0x11=17 -> 32e6/(16*17) = 117647 baud
 * = 115200 +2.1%, within UART tolerance and mtl's exact value. The Pi side
 * (uart_repl.py, monitor task) is 115200 to match.
 */
#ifndef _UART_H
#define _UART_H

#include <stdint.h>

#define DLM_BAUD  0x00  /**< @brief divisor latch MSB - 115200 baud @ 32 MHz F_uart */
#define DLL_BAUD  0x11  /**< @brief divisor latch LSB - 115200 baud (32e6/(16*17)) */

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
