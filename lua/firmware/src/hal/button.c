/**
 * @file button.c
 * @brief Head push-button low-level access (M5, issue #93).
 *
 * The Nabaztag:tag head button is a single active-low GPIO on P3.1 (the
 * INT_SWITCH line). src/firmware wires an EXINT3 interrupt for it but leaves it
 * disabled and polls instead (see push_button_value in src/firmware); we do the
 * same - a plain polled read, no IRQ/timer subsystem needed. Plain C (no Lua),
 * so it is safe to auto-compile into every firmwareV2 app; the Lua binding that
 * exposes it lives in src/app/lua.c.
 */
#include "ml674061.h"
#include "common.h"

#include "hal/button.h"

void init_button(void)
{
  INT_SWITCH_AS_INPUT;   /* P3.1 -> input */
}

uint8_t button_pressed(void)
{
  /* active-low: the bit reads 0 while pressed. */
  return !((INT_SWITCH_READ & INT_SWITCH_BIT) == INT_SWITCH_BIT);
}
