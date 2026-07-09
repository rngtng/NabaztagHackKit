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
extern volatile uint32_t counter_timer;   /* ms since init_tick() */
extern volatile uint32_t counter_timer_s; /* whole seconds thereof */

#endif
