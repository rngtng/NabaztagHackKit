/**
 * @file delay.h
 * @brief 1 ms system tick + busy-wait delay (M11a, #143).
 *
 * V2 shim for src/firmware's utils/delay.h so the vendored usb/ sources keep
 * their includes verbatim. Implemented in sys/src/tick.c: the system timer
 * fires INT_SYSTEM_TIMER every 1 ms and the ISR increments counter_timer -
 * the first live IRQ on firmwareV2. Call init_tick() before DelayMs().
 */
#ifndef _DELAY_H_
#define _DELAY_H_

#include <stdint.h>

void init_tick(void);
void DelayMs(uint16_t cmpt_ms);

/**
 * @brief Install the single callback the 1 ms tick ISR runs (NULL removes it).
 *
 * The inversion of #325: sys/ owns the timer and knows nothing about who wants
 * it, so a driver that needs per-millisecond work registers here instead of
 * being called by name from the ISR. Today the only caller is hal/led.c's
 * led_fade(), which installs the #102 background fade engine when a fade is
 * actually armed - so an image that only ever calls set_led() never takes
 * led_fade_tick's address and --gc-sections keeps the engine out.
 *
 * Runs in interrupt context, so the callback must be short and must do its own
 * rate limiting (led_fade_tick does). init_tick() does NOT clear it -
 * registration legitimately happens first.
 */
void tick_set_hook(void (*fn)(void));
extern volatile uint32_t counter_timer;   /* ms since init_tick() */
extern volatile uint32_t counter_timer_s; /* whole seconds thereof */

#endif
