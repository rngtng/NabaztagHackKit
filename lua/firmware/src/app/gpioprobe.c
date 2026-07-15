/**
 * @file gpioprobe.c
 * @brief GPIO-scan probe: find the wheel's end-of-travel click switch and check
 *        whether the audio-jack insert changes any GPIO. Dumps every readable
 *        GPIO input port and reports which bytes changed, so turning the wheel
 *        to its click, or inserting/removing a jack, shows up as a diff.
 *
 * Output is on UART0 (38400 8N1), read on the Pi's /dev/serial0; watch the
 * transcript while operating the rabbit by hand:
 *   task lua:firmware:flash APP=gpioprobe
 *   (on the Pi) stty -F /dev/serial0 38400 raw -echo; cat /dev/serial0
 *
 * The back wheel's analog reading (ADC ch.2) is a separate, already-confirmed
 * register sequence (see hal/adc.c, nab.wheel() in app/lua.c) - this probe is
 * only for the *digital* click switch and the jack, whose pins are unknown.
 */
#include "ml674061.h"
#include "common.h"

#include "hal/uart.h"

static void sh_puts(const char *s)
{
  putst_uart((uint8_t *)s);
}

static void sh_puthex8(uint8_t v)
{
  const char *hex = "0123456789abcdef";
  char b[3] = {hex[(v >> 4) & 0xF], hex[v & 0xF], '\0'};
  sh_puts(b);
}

static void busy_delay(volatile unsigned long n)
{
  while (n--)
    CLR_WDT;
}

/* Every port's 8-bit input register (PI0..PI14 - PA..PO), in order. */
#define NUM_PORTS 15
static const volatile uint8_t *const port_input[NUM_PORTS] = {
  (const volatile uint8_t *)PI0,  (const volatile uint8_t *)PI1,
  (const volatile uint8_t *)PI2,  (const volatile uint8_t *)PI3,
  (const volatile uint8_t *)PI4,  (const volatile uint8_t *)PI5,
  (const volatile uint8_t *)PI6,  (const volatile uint8_t *)PI7,
  (const volatile uint8_t *)PI8,  (const volatile uint8_t *)PI9,
  (const volatile uint8_t *)PI10, (const volatile uint8_t *)PI11,
  (const volatile uint8_t *)PI12, (const volatile uint8_t *)PI13,
  (const volatile uint8_t *)PI14,
};
static const char port_name[NUM_PORTS] = {
  'A', 'B', 'C', 'D', 'E', 'F', 'G', 'H', 'I', 'J', 'K', 'L', 'M', 'N', 'O',
};

int main(void)
{
  uint8_t prev[NUM_PORTS];
  uint8_t first = 1;
  int i;

  init_uart();

  sh_puts("#123 GPIO scan probe - wiggle the wheel to its click and the audio "
          "jack now, watch for diffs\n");

  for (;;) {
    for (i = 0; i < NUM_PORTS; i++) {
      uint8_t cur = *port_input[i];
      if (first || cur != prev[i]) {
        char name[2] = {port_name[i], '\0'};
        sh_puts("P");
        sh_puts(name);
        sh_puts("=0x");
        sh_puthex8(cur);
        sh_puts(cur != prev[i] && !first ? " *CHANGED*\n" : "\n");
      }
      prev[i] = cur;
    }
    first = 0;
    busy_delay(1000000UL); /* ~poll rate; no timer subsystem */
  }
  return 0;
}
