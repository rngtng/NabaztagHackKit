/**
 * @file i2c_test.c
 * @brief Host-side guards for hal/i2c.c's interrupt masking (#246, #252).
 *
 * i2c.c is register-only, so it runs natively against stubs/ml674061.h's fake
 * register file (see that header for what is and is not modelled). The CPSR
 * helpers are stubbed here with a mask counter, which is what makes the
 * masking policy - normally invisible - directly assertable.
 *
 * The register file is passive, so every transfer below takes the timeout
 * path. That is deliberate: both defects are about what happens to interrupts
 * *during* a transfer, and the timeout path is the longest such window.
 *
 * Scenarios (argv[1] selects one; all run by default):
 *
 *   nesting  #246 - a transfer must not unmask interrupts a caller had masked
 *   window   #252 - the polling loops must not run with interrupts masked
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ml674061.h"
#include "common.h"
#include "irq.h"
#include "hal/i2c.h"

/* The fake peripheral window stubs/ml674061.h points every register at. */
volatile uint32_t nab_regs[64];

/* --- stubbed interrupt control -------------------------------------------- */
/* Models the ARM CPSR I-bit. `unmask_events` counts every transition back to
 * enabled, which is how the #252 scenario sees whether the driver held the
 * mask across its whole call or released it around the polls. */

static int irq_masked;
static int unmask_events;

void __disable_interrupt(void)
{
  irq_masked = 1;
}

void __enable_interrupt(void)
{
  if (irq_masked)
    unmask_events++;
  irq_masked = 0;
}

uint32_t irq_disable_save(void)
{
  uint32_t prev = irq_masked ? 0x80 : 0;
  irq_masked = 1;
  return prev;
}

void irq_restore(uint32_t prev)
{
  if (!prev)
    __enable_interrupt();
}

/* --- assert harness ------------------------------------------------------- */

static int failures;

#define CHECK(cond, msg)                                                      \
  do {                                                                        \
    if (!(cond)) {                                                            \
      printf("  FAIL: %s\n", (msg));                                          \
      failures++;                                                             \
    }                                                                         \
  } while (0)

static void reset_bus(void)
{
  memset((void *)nab_regs, 0, sizeof nab_regs);
  irq_masked = 0;
  unmask_events = 0;
}

/* ---------------------------------------------------------------------------
 * #246: write_i2c/read_i2c used __enable_interrupt() unconditionally on every
 * exit path instead of the save/restore pair. A caller that already held
 * interrupts masked got them silently re-enabled underneath it, mid-critical-
 * section.
 *
 * Not reachable from today's call sites - both are main-context with
 * interrupts on - but config_save() already masks IRQs across a ~63 ms flash
 * write, and #218/#233's config portal puts flash writes and I2C in the same
 * flow. sys/src/irq.c grew irq_disable_save/irq_restore for exactly this and
 * led.c was converted; i2c.c was not.
 * ------------------------------------------------------------------------- */
static void scen_nesting(void)
{
  uint8_t data[4] = {0x01, 0x02, 0x03, 0x04};

  printf("scenario nesting: a transfer must not unmask a caller's critical section\n");

  /* Caller holds a critical section across the transfer. */
  reset_bus();
  uint32_t saved = irq_disable_save();
  CHECK(irq_masked, "precondition: the caller's section masks interrupts");
  write_i2c(0xA0, data, sizeof data);
  CHECK(irq_masked, "write_i2c must leave the caller's mask in place");
  irq_restore(saved);
  CHECK(!irq_masked, "the caller's restore re-enables");

  reset_bus();
  saved = irq_disable_save();
  read_i2c(0xA0, data, sizeof data);
  CHECK(irq_masked, "read_i2c must leave the caller's mask in place");
  irq_restore(saved);
  CHECK(!irq_masked, "the caller's restore re-enables after a read");

  /* The ordinary case - called with interrupts on - must still end with them
   * on. Preserving nesting is worthless if it strands the normal path masked. */
  reset_bus();
  write_i2c(0xA0, data, sizeof data);
  CHECK(!irq_masked, "a transfer from unmasked context ends unmasked");
  reset_bus();
  read_i2c(0xA0, data, sizeof data);
  CHECK(!irq_masked, "a read from unmasked context ends unmasked");
}

/* ---------------------------------------------------------------------------
 * #252: the whole transfer ran inside one __disable_interrupt() window,
 * including the bus-busy and completion polls. Those polls are the long part -
 * they are what a slow or wedged bus stretches - and blocking the 1 ms tick
 * across them makes counter_timer lose ground. Everything built on the tick
 * drifts with it: nab.time(), nab.wait(), the LED fade engine, and the wifi
 * stack's 200 ms rt2501_timer cadence.
 *
 * Nothing on this device drives I2C from an ISR (event.h is explicit that the
 * event core is single-context), so a tick landing mid-poll is harmless: the
 * poll only reads a status flag.
 *
 * The assertion is structural - the driver must release the mask more than
 * once per transfer - because "how much time was spent masked" is not
 * observable from here. One unmask per call is the old shape: mask at entry,
 * unmask at exit.
 * ------------------------------------------------------------------------- */
static void scen_window(void)
{
  uint8_t data[4] = {0x01, 0x02, 0x03, 0x04};

  printf("scenario window: the polling loops must not run masked\n");

  reset_bus();
  write_i2c(0xA0, data, sizeof data);
  CHECK(unmask_events > 1,
        "write_i2c must unmask around its polls, not hold the mask throughout");
  if (unmask_events <= 1)
    printf("        (%d unmask events; the whole transfer ran masked)\n",
           unmask_events);

  reset_bus();
  read_i2c(0xA0, data, sizeof data);
  CHECK(unmask_events > 1,
        "read_i2c must unmask around its polls, not hold the mask throughout");

  /* And the register manipulation between polls must still be fenced - a fix
   * that simply deleted all the masking would pass the check above. Prove the
   * driver does still mask at some point during the call. */
  reset_bus();
  write_i2c(0xA0, data, sizeof data);
  CHECK(unmask_events > 0, "write_i2c does still take the mask at least once");
}

int main(int argc, char **argv)
{
  const char *only = (argc > 1) ? argv[1] : NULL;

  if (!only || strcmp(only, "nesting") == 0) scen_nesting();
  if (!only || strcmp(only, "window") == 0)  scen_window();

  if (failures) {
    printf("i2c_test: %d check(s) FAILED\n", failures);
    return 1;
  }
  printf("i2c_test: all checks passed\n");
  return 0;
}
