/**
 * @file clock.h
 * @brief System clock bring-up: 8 MHz crystal -> PLL -> 32 MHz (#269).
 */
#ifndef _CLOCK_H
#define _CLOCK_H

/**
 * @brief Bring the CPU/APB clock up to 32 MHz via the PLL.
 *
 * init.s boots on the ring oscillator (~8 MHz), then calls this before main() -
 * so the whole image (product AND every bring-up example) runs at the real
 * 32 MHz and one UART divisor (115200) serves all. Not called from C.
 */
void init_pll(void);

#endif /* _CLOCK_H */
