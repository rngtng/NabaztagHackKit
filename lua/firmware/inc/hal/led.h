/**
 * @file led.h
 * @author Violet - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief LEDs low level access
 */
#ifndef _LED_H_
#define _LED_H_

//Led Driver
#define MODE_LED_DOT_CORRECTION   MODE_LED_SET
#define MODE_LED_ON_OFF_CONTROL   MODE_LED_CLEAR

//Color definitions
#define RGB_BLACK	0x00000000
#define RGB_RED		0x007F0000

#if (PCB_RELEASE == LLC2_3) || (PCB_RELEASE == LLC2_4c)
#define RGB_GREEN	0x00007F00
#define RGB_BLUE	0x0000007F
#elif PCB_RELEASE == LLC2_2
#define RGB_GREEN	0x0000007F
#define RGB_BLUE	0x00007F00
#endif

#define RGB_WHITE	0x007F7F7F

// Led number definitions - raw TLC5922 channel index in bits 31->24.
// Physical positions verified on hardware (LLC2_4c) with the ledmap probe:
// 1-4 are the four belly "directional cones", 5 is the nose. This is the RAW
// channel map; set_led()/led_fade() apply a separate logical remap via
// convled[] in led.c (invert it to target a known physical LED - see the M5 LED
// binding in src/app/lua.c, which does exactly that so fades hit the same LEDs
// nab.led() lights by name).
#define LED_RGB_1		0x01000000	/* belly (upper) */
#define LED_RGB_2		0x02000000	/* belly bottom  */
#define LED_RGB_3		0x03000000	/* belly left    */
#define LED_RGB_4		0x04000000	/* belly right   */
#define LED_RGB_5		0x05000000	/* nose          */

//Fade engine step interval (ms) - one interpolation step + one SPI flush per
//tick, driven from the 1 ms System Timer IRQ (hal/timer.c).
#define LED_FADE_TICK_MS	10

/*************/
/* Functions */
/*************/
void init_led_rgb_driver(void);
void set_led_rgb(uint32_t color);
void set_led(uint8_t led,uint32_t color);
void led_fade(uint8_t led,uint32_t color,uint32_t ms);
void led_fade_tick(void);

#endif
