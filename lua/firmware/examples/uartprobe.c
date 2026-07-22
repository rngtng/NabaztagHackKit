/**
 * @file uartprobe.c
 * @brief UART bring-up probe: spew a known banner on UART0 forever.
 *
 * The minimal UART0 link check (predates the full console, #203/#207). Proves
 * the physical UART link off the board: it initialises UART0 (38400 8N1 on
 * OKI port PB0=TX / PB1=RX) and transmits a recognisable banner in a loop, so
 * a listener on the other end (e.g. a Raspberry Pi on /dev/serial0 @38400)
 * can confirm data, baud, and wiring/GND without a JTAG session.
 *
 * Deliberately self-contained like blink.c: a calibrated busy-loop delay, no
 * IRQ/timer subsystem. The ARM7TDMI startup (sys/asm/init.s) has already done
 * clock + external-memory-controller init before main().
 *
 * Flash + listen:
 *   task lua:firmware:flash EXAMPLE=uartprobe
 *   (on the Pi) stty -F /dev/serial0 38400 raw -echo; cat /dev/serial0
 * Expect the banner line repeating ~5x/second.
 */
#include <stdint.h>

#include "hal/uart.h"

/* Approx. inner-loop iterations per millisecond on the 33 MHz ARM7TDMI
 * (same first estimate as blink.c - exact timing is irrelevant for a probe). */
#define LOOPS_PER_MS 3000u
#define BANNER_MS    200u

static void delay_ms(uint32_t ms)
{
  volatile uint32_t n = ms * LOOPS_PER_MS;
  while (n--)
    asm volatile("nop");
}

int main(void)
{
  init_uart();

  for (;;) {
    putst_uart((uint8_t *)"NAB-UART-PROBE alive @38400 8N1\r\n");
    delay_ms(BANNER_MS);
  }

  return 0;
}
