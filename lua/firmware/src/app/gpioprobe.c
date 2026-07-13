/**
 * @file gpioprobe.c
 * @brief GPIO-scan probe (issue #123): find the wheel's end-of-travel click
 *        switch and check whether the audio-jack insert changes any GPIO.
 *
 * Two items from #123 need a *human with JTAG/Pi access* wiggling the
 * hardware while watching the console - this session had neither, so per the
 * CLAUDE.md peripheral-exists rule (the M6 AT45 phantom, #94) no driver is
 * built for either without a confirmed signal first. This probe is that
 * confirming step, in the same spirit as audioprobe.c (M8) and ledmap.c (M5):
 * it dumps every readable GPIO input port and reports which bytes changed, so
 * turning the wheel to its end-of-travel click, or inserting/removing a jack,
 * shows up as a diff in the console log.
 *
 * Run it debugger-attached and watch the transcript while operating the
 * rabbit by hand:
 *   task repl:firmwareV2:hw APP=gpioprobe
 *
 * The back wheel's analog reading (ADC ch.2) is a separate, already-confirmed
 * register sequence (see hal/adc.c, nab.wheel() in app/lua.c) - this probe is
 * only for the *digital* click switch and the jack, whose pins are unknown.
 */
#include "ml674061.h"
#include "common.h"

/* ---- semihosting console (M3 #91 path) ------------------------------------ */
#define SYS_WRITEC 0x03

static inline int semihost(int op, void *arg)
{
  register int r0 asm("r0") = op;
  register void *r1 asm("r1") = arg;
  asm volatile("svc #0xAB" : "+r"(r0) : "r"(r1) : "memory");
  return r0;
}

static void sh_puts(const char *s)
{
  while (*s) {
    char c = *s++;
    semihost(SYS_WRITEC, &c);
  }
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
    busy_delay(1000000UL); /* ~poll rate; no timer subsystem, see CLAUDE.md */
  }
  return 0;
}
