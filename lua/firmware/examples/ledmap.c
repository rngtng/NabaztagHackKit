/**
 * @file ledmap.c
 * @brief LED-map probe: light every LED_RGB_* a distinct colour at once so a
 *        single flash reveals which physical LED each raw channel index drives
 *        (the raw LED_RGB_n -> physical map inherited from src/firmware is
 *        unverified for this board, LLC2_4c). set_led_rgb accumulates into
 *        led_intensity[] (each call masks only its own LED's bytes), so setting
 *        all five in a row holds them lit simultaneously. Read the map off the
 *        board:
 *
 *   LED_RGB_1 = RED    LED_RGB_2 = GREEN   LED_RGB_3 = BLUE
 *   LED_RGB_4 = WHITE  LED_RGB_5 = YELLOW (RED+GREEN)
 *
 * GREEN/BLUE can read swapped on some Waitrony LED batches (see led.c), but all
 * five stay mutually distinct - which is all the position map needs.
 *
 * I/O bring-up mirrors blink.c (the LED-only subset of firmware init_io for
 * LLC2_4c). Kept as a permanent diagnostic for board bring-up / new revisions.
 */
#include "ml674061.h"
#include "common.h"

#include "hal/spi.h"
#include "hal/led.h"

#define RGB_YELLOW (RGB_RED | RGB_GREEN)

int main(void)
{
  /* LED control lines are GPIO outputs (subset of firmware init_io, LLC2_4c). */
  CS_LED_AS_OUTPUT;
  MODE_LED_AS_OUTPUT;
  CS_LED_SET;
  MODE_LED_CLEAR;

  init_spi();               /* select SPI pins + configure SPI0/SPI1 (LED bus) */
  init_led_rgb_driver();    /* enable driver, blank all LEDs */

  /* Each call masks+ORs only its own LED's bytes, so all five stay lit. */
  set_led_rgb(LED_RGB_1 | RGB_RED);
  set_led_rgb(LED_RGB_2 | RGB_GREEN);
  set_led_rgb(LED_RGB_3 | RGB_BLUE);
  set_led_rgb(LED_RGB_4 | RGB_WHITE);
  set_led_rgb(LED_RGB_5 | RGB_YELLOW);

  for (;;) {
    /* steady - hold all five so the physical map can be read off the hardware */
  }
  return 0;
}
