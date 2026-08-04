/**
 * @file adc_test.c
 * @brief Host-side bounds guard for the wheel ADC read (src/hal/adc.c).
 *
 * adc.c is register-only, so it runs natively against stubs/ml674061.h exactly
 * like i2c.c does. As that header says, the fake register window is PASSIVE: a
 * conversion driven against it never sees its completion flag clear, which is
 * precisely the wedged-converter path these scenarios care about. A
 * completes-normally scenario cannot be modelled by a passive map (nothing
 * clears ADCON1_STS but the hardware), so the non-vacuous half here is the
 * init scenario, which asserts the exact mux/clock bits.
 *
 * Scenarios (argv[1] selects one; all run by default):
 *
 *   init     init_adc programs the PD2 mux and the conversion clock
 *   wedged   a converter that never finishes must not spin forever
 *
 * `wedged` is the guard. adc_read_ch2 polls
 *
 *     while (get_hvalue(ADCON1) & ADCON1_STS) CLR_WDT;
 *
 * with no bound and no escape, and it FEEDS THE WATCHDOG while it waits - so a
 * converter that never answers is not a slow read, it is a permanent hang that
 * the watchdog has been explicitly stopped from breaking. The rest of this
 * layer already learned that lesson twice: hal/rfid.c was bounded in #253 for
 * a wedged I2C bus, and hal/audio.c's wait_dreq() carries an explicit guard
 * count for the same reason ("bounded so a wedged chip cannot hang us").
 *
 * The reach is not limited to a script that calls nab.wheel(). lib/audio's
 * volume knob (audio.volume:step -> drv.wheel -> nab.wheel) is written to be
 * handed to sched.pump, so on a rabbit using it the wheel is read from inside
 * every nab.wait/nab.delay/nab.play and from the REPL's idle loop - and one
 * wedged read there takes the whole cooperative reactor with it, with no
 * watchdog reboot to recover.
 *
 * A hang is the failure mode under test, so the scenario cannot simply call
 * the function and assert on its return: it arms alarm(2) first and reports
 * the timeout as the failure. On a bounded implementation the alarm is
 * cancelled and never fires.
 */
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#include "ml674061.h"
#include "common.h"
#include "hal/adc.h"

/* The fake peripheral window stubs/ml674061.h maps every register into. */
volatile uint32_t nab_regs[64];

/* --- assert harness ------------------------------------------------------- */

static int failures;

#define CHECK(cond, msg)                                                      \
  do {                                                                        \
    if (!(cond)) {                                                            \
      printf("  FAIL: %s\n", (msg));                                          \
      failures++;                                                             \
    }                                                                         \
  } while (0)

static void eq_hex(unsigned long got, unsigned long want, const char *label)
{
  if (got != want) {
    printf("  FAIL: %s: got 0x%lx, want 0x%lx\n", label, got, want);
    failures++;
  }
}

/* --- init: the register sequence, asserted positively --------------------- */

static void scen_init(void)
{
  printf("scenario init: init_adc programs the PD2 mux and the ADC clock\n");

  memset((void *)nab_regs, 0, sizeof nab_regs);
  init_adc();

  /* PORTSEL2 packs two mux-select bits per pin from bit 16 for the PD group;
   * PD2's field is bits 21:20, and only that field may be touched - PD0/PD1
   * are EXINT2/3, and EXINT3 shares its pin with the head button. */
  eq_hex(*(volatile uint32_t *)PORTSEL2, 0x00100000,
         "init_adc sets only PD2's mux field");
  eq_hex(*(volatile uint16_t *)ADCON0, 0x0000,
         "init_adc selects select-mode, not scan-mode");
  eq_hex(*(volatile uint16_t *)ADCON2, ADCON2_CLK32,
         "init_adc sets the CPUCLK/32 conversion clock");
}

/* --- wedged: the read must terminate -------------------------------------- */

static const char TIMEOUT_MSG[] =
    "  FAIL: adc_read_ch2 never returned on a converter that does not finish"
    " (unbounded poll, watchdog fed)\n";

static void on_timeout(int sig)
{
  (void)sig;
  /* async-signal-safe: no printf, no exit handlers (ASan's included) */
  ssize_t n = write(1, TIMEOUT_MSG, sizeof TIMEOUT_MSG - 1);
  (void)n;
  _exit(1);
}

static void scen_wedged(void)
{
  printf("scenario wedged: a converter that never finishes must not hang\n");

  memset((void *)nab_regs, 0, sizeof nab_regs);
  init_adc();

  /* A plausible reading sitting in the result register, so a bounded
   * implementation has something real to hand back if it chooses to. */
  *(volatile uint16_t *)ADR2 = (uint16_t)(200 << 2);

  /* The handler _exit()s, which skips stdio's flush - so everything printed so
   * far (including the other scenario's output) has to be on the wire first. */
  fflush(stdout);
  signal(SIGALRM, on_timeout);
  alarm(2);
  uint8_t v = adc_read_ch2();
  alarm(0);

  /* Reaching this line at all is the assertion - the alarm handler above is
   * what reports the failure. The value is not pinned: a bounded
   * implementation may reasonably report a last-known reading, a zero or a
   * sentinel; what it must not do is never come back. */
  printf("  adc_read_ch2 returned %u on a wedged converter\n", (unsigned)v);

  /* The start bit was actually asserted, so this did not pass by never having
   * driven a conversion in the first place. */
  CHECK((*(volatile uint16_t *)ADCON1 & ADCON1_STS) != 0,
        "the read really did start a conversion");
}

int main(int argc, char **argv)
{
  const char *only = (argc > 1) ? argv[1] : NULL;

  if (!only || strcmp(only, "init") == 0)   scen_init();
  if (!only || strcmp(only, "wedged") == 0) scen_wedged();

  if (failures) {
    printf("adc_test: %d check(s) FAILED\n", failures);
    return 1;
  }
  printf("adc_test: all checks passed\n");
  return 0;
}
