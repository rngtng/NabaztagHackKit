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

void DelayMs(uint16_t cmpt_ms)
{
  uint32_t t = counter_timer;
  while (cmpt_ms > (counter_timer - t))
    CLR_WDT;
}
