/**
 * @file blink.c
 * @brief M1 bring-up app (issue #89): the first flash-and-run binary.
 *
 * Blinks nose RGB LED 1 red on/off forever. It is the first binary that touches
 * a peripheral, so it validates the startup + linker + toolchain path proven by
 * M0 (hello.c) plus the SPI + LED driver copied from src/firmware - before any
 * Lua lands. On real hardware (confirmed over JTAG after M2), a steadily
 * blinking red nose means the whole bring-up chain works end to end.
 *
 * Delay: a calibrated software busy-loop, NOT the timer-based DelayMs from
 * src/firmware/src/utils/delay.c. That routine spins on counter_timer, which is
 * only advanced by the System Timer IRQ - and M1 brings up neither the interrupt
 * controller nor a timer (that arrives with a later milestone). A busy-loop
 * keeps this first binary self-contained (no IRQ subsystem to get wrong) and is
 * the only delay the instruction-level simulator can observe, since it models no
 * timers (see sim/simulate.py). LOOPS_PER_MS is an untested first estimate for
 * the 33 MHz core; tune it on hardware in M2 if the blink is too fast/slow.
 *
 * I/O bring-up here is the LED-only subset of src/firmware's init_io() for this
 * board (PCB_RELEASE == LLC2_4c): the CS_LED and MODE_LED lines are plain GPIO
 * and must be driven as outputs before the LED driver can clock the TLC594x.
 * init_spi() itself selects the SPI0/SPI1 pin functions.
 */
#include "ml674061.h"
#include "common.h"

#include "hal/spi.h"
#include "hal/led.h"

/* Approx. inner-loop iterations per millisecond on the 33 MHz ARM7TDMI.
 * First estimate only - calibrate against a stopwatch on hardware (M2). */
#define LOOPS_PER_MS 3000u
#define BLINK_MS     250u

/**
 * @brief Crude blocking delay via a busy-loop (no timer/IRQ needed).
 * @param [in] ms Milliseconds to spin for (approximate).
 */
static void delay_ms(uint32_t ms)
{
  volatile uint32_t n = ms * LOOPS_PER_MS;
  while (n--)
    __no_operation();   /* asm "nop" - keeps the loop from being optimised away */
}

int main(void)
{
  /* LED control lines are GPIO outputs (subset of firmware init_io, LLC2_4c). */
  CS_LED_AS_OUTPUT;
  MODE_LED_AS_OUTPUT;
  CS_LED_SET;
  MODE_LED_CLEAR;

  init_spi();               /* select SPI pins + configure SPI0/SPI1 (LED bus) */
  init_led_rgb_driver();    /* enable driver, blank all LEDs */

  for (;;) {
    set_led_rgb(LED_RGB_1 | RGB_RED);   /* nose LED 1 -> red */
    delay_ms(BLINK_MS);
    set_led_rgb(LED_RGB_1 | RGB_BLACK); /* nose LED 1 -> off */
    delay_ms(BLINK_MS);
  }
  return 0;
}
