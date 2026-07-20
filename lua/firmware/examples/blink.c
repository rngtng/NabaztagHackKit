/**
 * @file blink.c
 * @brief Blink the nose LED (LED_RGB_5) red forever - the first binary that
 *        touches a peripheral, validating startup + linker + toolchain plus the
 *        SPI + LED driver copied from src/firmware.
 *
 * Delay is a calibrated software busy-loop, NOT the timer-based DelayMs: there
 * is no interrupt controller or timer here yet, and it is the only delay the
 * instruction-level simulator can observe (it models no timers). LOOPS_PER_MS
 * is a first estimate for the 33 MHz core; tune on hardware if too fast/slow.
 *
 * I/O bring-up is the LED-only subset of src/firmware's init_io() for this
 * board (PCB_RELEASE == LLC2_4c): the CS_LED and MODE_LED lines are plain GPIO
 * and must be driven as outputs before the LED driver can clock the TLC594x.
 * init_spi() itself selects the SPI0/SPI1 pin functions.
 */
#include "ml674061.h"
#include "common.h"

#include "hal/spi.h"
#include "hal/led.h"

/* Approx. inner-loop iterations per millisecond on the 33 MHz ARM7TDMI.
 * First estimate only - calibrate against a stopwatch on hardware. */
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
    set_led_rgb(LED_RGB_5 | RGB_RED);   /* nose -> red */
    delay_ms(BLINK_MS);
    set_led_rgb(LED_RGB_5 | RGB_BLACK); /* nose -> off */
    delay_ms(BLINK_MS);
  }
  return 0;
}
