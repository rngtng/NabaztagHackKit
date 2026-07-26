/**
 * @file clock.h
 * @brief System clock bring-up: 8 MHz crystal -> PLL -> 32 MHz (#269).
 */
#ifndef _CLOCK_H
#define _CLOCK_H

/**
 * @brief Bring the CPU/APB clock up to 32 MHz via the PLL.
 *
 * init.s boots on the ring oscillator (~8 MHz). Call this FIRST in main() -
 * before init_uart()/init_tick()/any peripheral - so every clock-derived
 * divisor is computed against the final 32 MHz. Shared by the product firmware
 * and the bring-up examples (a probe that skips it runs at 8 MHz).
 */
void init_pll(void);

#endif /* _CLOCK_H */
