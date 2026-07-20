/**
 * @file uartrxprobe.c
 * @brief UART RX bring-up probe: self-test getch_uart() via internal loopback.
 *
 * RX cannot be exercised by the Unicorn sim (no UART peripheral) nor by the
 * #203 hardware tap (TX-only wiring), so this probe uses the 16550's built-in
 * loopback (UARTMCR_LOOP): TX is internally routed to RX, letting getch_uart()
 * read back bytes putch_uart() sent - proving the RX code path on real silicon
 * with no external RX connection.
 *
 * In loopback the external TX pin idles (marking), so the result cannot be
 * transmitted while looping. The probe therefore: (1) loops back a set of test
 * bytes chosen to catch stuck/shorted data lines (0x00, 0x55, 0xAA, 0xFF),
 * (2) disables loopback, (3) spews PASS / FAIL forever on the real TX line so a
 * listener on the Pi (/dev/serial0 @38400) can read the verdict without JTAG.
 *
 * Flash + listen:
 *   task lua:firmware:flash EXAMPLE=uartrxprobe
 *   (on the Pi) stty -F /dev/serial0 38400 raw -echo; cat /dev/serial0
 * Expect "NAB-UART-RX LOOPBACK PASS" repeating; FAIL prints exp/got hex.
 */
#include <stdint.h>

#include "ml674061.h"
#include "common.h"
#include "hal/uart.h"

#define LOOPS_PER_MS 3000u
#define BANNER_MS    200u

/* Bounded spin for a looped-back byte: internal transfer is near-instant, so a
 * small cap turns a wedged RX (data-ready never sets) into a FAIL, not a hang. */
#define RX_SPINS     100000u

static void delay_ms(uint32_t ms)
{
  volatile uint32_t n = ms * LOOPS_PER_MS;
  while (n--)
    asm volatile("nop");
}

/* Round-trip one byte through the loopback: send, poll RX, compare. */
static int loopback_ok(uint8_t tx)
{
  int rx = -1;
  uint32_t spins = RX_SPINS;

  putch_uart(tx);
  while (spins-- && (rx = getch_uart()) < 0)
    ;
  return rx == (int)tx;
}

/* Append the two hex nibbles of v to buf (no newlib sprintf - macro-erased). */
static void hex2(uint8_t *buf, uint8_t v)
{
  static const char d[] = "0123456789ABCDEF";
  buf[0] = d[(v >> 4) & 0xF];
  buf[1] = d[v & 0xF];
}

int main(void)
{
  static const uint8_t tests[] = {0x00, 0x55, 0xAA, 0xFF};
  uint8_t fail_exp = 0, fail_got = 0;
  int failed = 0;
  unsigned i;

  init_uart();

  put_value(UARTMCR0, UARTMCR_LOOP);   /* internal TX->RX loopback */
  put_value(UARTFCR0, UARTFCR_FE | UARTFCR_RFCLR | UARTFCR_TFCLR);  /* flush FIFOs */

  for (i = 0; i < sizeof(tests); i++) {
    if (!loopback_ok(tests[i])) {
      failed = 1;
      fail_exp = tests[i];
      /* re-read for the report; -1 (no data) shows as 0xFF-ish, still a clear FAIL */
      fail_got = (uint8_t)getch_uart();
      break;
    }
  }

  put_value(UARTMCR0, 0);              /* back to normal: TX drives the pin */

  for (;;) {
    if (!failed) {
      putst_uart((uint8_t *)"NAB-UART-RX LOOPBACK PASS\r\n");
    } else {
      uint8_t msg[] = "NAB-UART-RX LOOPBACK FAIL exp=0x00 got=0x00\r\n";
      hex2(&msg[32], fail_exp);        /* "...exp=0x[32][33]..." */
      hex2(&msg[41], fail_got);        /* "...got=0x[41][42]..." */
      putst_uart(msg);
    }
    delay_ms(BANNER_MS);
  }

  return 0;
}
