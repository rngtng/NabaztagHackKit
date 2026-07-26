/**
 * @file clock.c
 * @brief System clock bring-up (#269): 8 MHz crystal -> PLL -> 32 MHz.
 *
 * init.s boots on the internal ring oscillator (~8 MHz measured on UART0) and
 * never brings up the PLL, leaving the whole chip 4x slow + imprecise. This is
 * a port of mtl/firmware main.c's init_pll(): configure PLLA/PLLB, enable the
 * PLL'd sysclock, switch the clock source to it, stop the ring oscillator.
 * LLC2_4c only - the mtl LLC2_2 (no-PLL) path is not shipped. A wrong PLL config
 * hangs the chip (JTAG-recoverable).
 */
#include <stdint.h>

#include "ml674061.h"
#include "common.h"
#include "clock.h"

void init_pll(void)
{
  /* Configure PLLA and PLLB (PLLDIVA=1, DVCOA=16, DREFA=1, SVCOA=3, PLLDIVC=3). */
  put_wvalue(PLL1, 0x31013110);
  put_wvalue(PLL2, 0x00030101);
  /* CLKCNT: APB=CPU, sysclock+ring-osc+PLLA active, source=ring osc, CLKDIVA=1/4. */
  put_wvalue(CLKCNT, 0x000D0109);
  /* Wait for PLLA to lock before switching the CPU onto it. mtl omits this and
   * relies on instruction timing between the enable and the switch (its -Os
   * codegen + init_io() happen to space them enough); on our -O1 build the
   * switch landed before lock and the source stayed on the crystal (verified:
   * console only decoded at 9600 = 8 MHz without this wait, cleanly at 38400 =
   * 32 MHz with it). ~hundreds of ms at the current 8 MHz - one-time boot cost. */
  { volatile uint32_t i; for (i = 0; i < 2000000; i++) __asm__ volatile(""); }
  clr_wbit(CLKCNT, 0x00000300);   /* source = PLL'd sysclock */
  clr_wbit(CLKCNT, 0x00040000);   /* stop ring oscillator */
  set_wbit(PECLKCNT, 0x18000000); /* GPIO11/12 peripheral clock => XD16..XD31 */
  set_wbit(PECLKCNT, 0x08000000); /* peripheral clock enable (mtl) */
}
