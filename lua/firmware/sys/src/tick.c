/**
 * @file tick.c
 * @brief 1 ms system tick - firmwareV2's first live IRQ.
 *
 * The USB host stack needs V1's 1 ms counter_timer (URB timeouts, DelayMs in
 * enumeration) - mirrors src/firmware main.c's timer setup: system timer
 * reload 0xF830 = 1 ms @ 32 MHz, ISR on INT_SYSTEM_TIMER (0). init.s already
 * starts the timer (10 ms, ring-osc path); init_tick() reprograms it to 1 ms
 * and unmasks the interrupt. Verify tick liveness on hardware before trusting
 * anything built on it.
 */
#include "ml674061.h"
#include "common.h"
#include "irq.h"
#include "utils/delay.h"

volatile uint32_t counter_timer;
volatile uint32_t counter_timer_s;
static volatile uint32_t counter_timer_sbuf;

static void tick_handler(void)
{
  counter_timer++;
  if (++counter_timer_sbuf >= 1000) {
    counter_timer_s++;
    counter_timer_sbuf = 0;
  }
  put_value(TMOVF, TMOVF_OVF); /* clear overflow flag */
}

void init_tick(void)
{
  counter_timer = 0;
  counter_timer_s = 0;
  counter_timer_sbuf = 0;

  put_value(TMEN, 0x00);            /* stop timer while reprogramming */
  put_value(TMOVF, TMOVF_OVF);      /* clear stale overflow */
  put_hvalue(TMRLR, 0xF830);        /* 1 ms @ 32 MHz (V1 main.c value) */

  IRQ_HANDLER_TABLE[INT_SYSTEM_TIMER] = tick_handler;
  set_wbit(ILC0, ILC0_ILR0 & ILC0_INT_LV7);

  put_value(TMEN, TMEN_TCEN);
  __enable_interrupt();
}

/* The Unicorn simulator models no timer, so counter_timer stays frozen there
 * and a pure tick-wait would hang. The spins bound turns that into a rough
 * busy-wait: it only bites while the counter has not moved off its starting
 * value, and 30000 spins take several ms on the real 33 MHz part - long past
 * the first 1 ms tick edge - so hardware timing is untouched. */
#define DELAY_SPINS_PER_MS 30000UL

void DelayMs(uint16_t cmpt_ms)
{
  uint32_t t = counter_timer;
  unsigned long spins = (unsigned long)cmpt_ms * DELAY_SPINS_PER_MS;
  while (cmpt_ms > (counter_timer - t)) {
    CLR_WDT;
    if (counter_timer == t && spins-- == 0)
      break; /* tick frozen (simulator): bounded fallback elapsed */
  }
}
