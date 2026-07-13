/**
 * @file led.c
 * @author Violet - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief LEDs low level access
 */
#include "ml674061.h"
#include "common.h"

#include "hal/spi.h"
#include "hal/led.h"

uint8_t led_intensity[14];

/**
 * @brief Init of the Led RGB driver, clear all leds
 */
void init_led_rgb_driver(void)
#if (PCB_RELEASE == LLC2_3) || (PCB_RELEASE == LLC2_4c)
{
  uint8_t cmpt_led;

  //Turn ON Leds
  MODE_LED_ON_OFF_CONTROL;
  CS_LED_CLEAR;
  WriteSPI_1(0xFF);
  WriteSPI_1(0xFF);
  CS_LED_SET;
  CS_LED_CLEAR;
  //Blank Leds
  MODE_LED_DOT_CORRECTION;
  CS_LED_CLEAR;
  WriteSPI_1(0x00);
  WriteSPI_1(0x00);
  WriteSPI_1(0x00);
  WriteSPI_1(0x00);
  WriteSPI_1(0x00);
  WriteSPI_1(0x00);
  WriteSPI_1(0x00);
  WriteSPI_1(0x00);
  for(cmpt_led=0; cmpt_led<6; cmpt_led++)
    WriteSPI_1(0x00);
  CS_LED_SET;
  CS_LED_CLEAR;

  return ;
}
#elif PCB_RELEASE == LLC2_2
{
  uint8_t cmpt_led;

  //Turn ON Leds
  MODE_LED_ON_OFF_CONTROL;
  CS_LED_CLEAR;
  WriteSPI(0xFF);
  WriteSPI(0xFF);
  CS_LED_SET;
  CS_LED_CLEAR;
  //Blank Leds
  MODE_LED_DOT_CORRECTION;
  CS_LED_CLEAR;
  WriteSPI(0x00);
  WriteSPI(0x00);
  WriteSPI(0x00);
  WriteSPI(0x00);
  WriteSPI(0x00);
  WriteSPI(0x00);
  WriteSPI(0x00);
  WriteSPI(0x00);
  for(cmpt_led=0; cmpt_led<6; cmpt_led++)
    WriteSPI(0x00);
  CS_LED_SET;
  CS_LED_CLEAR;

  return ;
}
#endif

/**
 * @brief Set a RGB LED color
 *
 * @note Sometimes the BLUE and GREEN colors are inverted due to different
 *       Part Number of the Waitrony leds...
 *
 * @param [in]  color Reference and color in 32bits
 *                      bits 31->24 RGB led number
 *                      bits 23->16 RED intensity in 7bits (Max=>0x7F)
 *                      bits 15->8 GREEN intensity in 7bits (Max=>0x7F)
 *                      bits 7->0  BLUE intensity in 7bits (Max=>0x7F)
 */
void set_led_rgb(uint32_t color)
{
	uint8_t cmpt_led;
	uint8_t led_rgb = (color>>24) & 0x0F;

// Set current
// Attention aux effets de bord entre 2 courants de 2 leds,
// ne pas dépasser 127 en intensité pour une couleur

//RGB_1
	if( led_rgb == 1 )
	{
		led_intensity[2] &= 0x07;

		led_intensity[0] = ((color>>15)&0xFE) + ((color>>14)&0x01) ;
		led_intensity[1] = ((color>>6)&0xFC) + ((color>>5)&0x03) ;
		led_intensity[2] |= ((color<<3)&0xF8) ;
	}
//RGB_2
	else if( led_rgb == 2 )
	{
		led_intensity[2] &= 0xF8;
		led_intensity[5] &= 0x3F;

		led_intensity[2] |= ((color>>20)&0x07) ;
		led_intensity[3] = ((color>>12)&0xF0) + ((color>>11)&0x0F) ;
		led_intensity[4] = ((color>>3)&0xE0) + ((color>>2)&0x1F) ;
		led_intensity[5] |= ((color<<6)&0xC0) ;
	}
//RGB_3
	else if( led_rgb == 3 )
	{
		led_intensity[5] &= 0xC0;
		led_intensity[7] &= 0x01;

		led_intensity[5] |= ((color>>17)&0x3F) ;
		led_intensity[6] = ((color>>9)&0x80) + ((color>>8)&0x7F) ;
		led_intensity[7] |= ((color<<1)&0xFE) ;
	}
//RGB_4
	else if( led_rgb == 4 )
	{
//		led_intensity[7] &= 0xE0;
		led_intensity[7] &= 0xFE;		//bug update 201104
		led_intensity[10] &= 0x0F;

		led_intensity[7] |= ((color>>22)&0x01) ;
		led_intensity[8] = ((color>>14)&0xFC) + ((color>>13)&0x03) ;
		led_intensity[9] = ((color>>5)&0xF8) + ((color>>4)&0x07) ;
		led_intensity[10] |= ((color<<4)&0xF0) ;
	}
//RGB_5
	else if( led_rgb == 5 )
	{
		led_intensity[10] &= 0xF0;

		led_intensity[10] |= ((color>>19)&0x0F) ;
		led_intensity[11] = ((color>>11)&0xE0) + ((color>>10)&0x1F) ;
		led_intensity[12] = ((color>>2)&0xC0) + ((color>>1)&0x3F) ;
		led_intensity[13] = ((color<<7)&0x80) ;
	}

//Set Led intensity
	MODE_LED_DOT_CORRECTION;
	CS_LED_CLEAR;
	for(cmpt_led=0; cmpt_led<14; cmpt_led++)
        {
#if (PCB_RELEASE == LLC2_3) || (PCB_RELEASE == LLC2_4c)
	  WriteSPI_1(led_intensity[cmpt_led]);
#elif PCB_RELEASE == LLC2_2
	  WriteSPI(led_intensity[cmpt_led]);
#endif
        }
	CS_LED_SET;
	CS_LED_CLEAR;

        return ;
}

/** @brief Mapping array for the LEDs */
uint32_t convled[8]={4,2,0,3,1,0,0,0};
/** @brief Intensity conversion table */
const uint8_t convintensity[256]=
{
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,
0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,1,1,1,1,1,1,1,1,1,1,1,1,2,2,2,2,2,2,2,2,
2,2,2,2,3,3,3,3,3,3,4,4,4,4,4,4,5,5,5,5,5,5,6,6,6,6,6,7,7,7,7,8,8,8,8,9,
9,9,9,10,10,10,11,11,11,11,12,12,13,13,13,13,14,14,15,15,16,16,16,16,17,
17,18,18,19,19,19,20,20,20,21,21,22,23,23,24,24,25,25,25,26,26,27,27,28,
29,29,30,31,31,32,32,33,34,34,35,36,36,37,38,38,39,40,40,41,42,42,43,44,
44,45,46,47,47,48,49,50,50,51,52,54,54,55,56,57,57,58,59,60,61,62,63,64,
65,65,67,68,69,70,70,72,73,74,75,76,77,78,79,80,82,83,84,85,86,87,88,89,
91,92,93,95,96,96,98,99,100,102,103,105,106,107,109,110,111,113,114,116,
117,119,120,122,123,125,126
};

/**
 * @brief Set a RGB LED color, using intensity conversion table
 *
 * @note Sometimes the BLUE and GREEN colors are inverted due to different
 *       Part Number of the Waitrony leds...
 *
 * @param [in]  led   Index of the LED
 * @param [in]  color Reference and color in 32bits
 *                      bits 23->16 RED intensity in 7bits (Max=>0x7F)
 *                      bits 15->8 GREEN intensity in 7bits (Max=>0x7F)
 *                      bits 7->0  BLUE intensity in 7bits (Max=>0x7F)
 */
void set_led(uint8_t led,uint32_t color)
{
  led=convled[led&7];

  color=(convintensity[(color>>16)&255]<<16)+
        (convintensity[(color>>8)&255]<<8)+
        convintensity[color&255];
//  color=(color&0xff0000)+((color>>8)&0xff)+((color<<8)&0xff00);
  set_led_rgb(((led+1)<<24)+color);
}

