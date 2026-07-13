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

//Led number definitions
#define LED_RGB_1		0x01000000
#define LED_RGB_2		0x02000000
#define LED_RGB_3		0x03000000
#define LED_RGB_4		0x04000000
#define LED_RGB_5		0x05000000

/*************/
/* Functions */
/*************/
void init_led_rgb_driver(void);
void set_led_rgb(uint32_t color);
void set_led(uint8_t led,uint32_t color);

#endif
