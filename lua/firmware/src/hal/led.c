/**
 * @file led.c
 * @author Violet - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief LEDs low level access
 *
 * The driver is a TLC5922-style 16-channel constant-current sink: brightness
 * is set through its 7-bit dot-correction shift register (16 x 7 = 112 bits,
 * MSB first). Channels 0..14 carry LED1 R,G,B .. LED5 R,G,B; channel 15 is
 * unused. A shadow image of the register lives in led_intensity[]; writes
 * pack into the shadow and a flush shifts all 14 bytes out in one go.
 *
 * Re-synced from mtl/firmware (issue #102 / PR #45): the gamma-2.2 intensity
 * table (no low-end dead zone), the generic led_pack/led_flush packer, and the
 * background fade engine. TWO firmwareV2-local differences from the sibling:
 *   1. counter_timer comes from hal/timer.c (the 1 ms System Timer IRQ, #102),
 *      not mtl/firmware's utils/delay.c.
 *   2. led_fade_tick() runs in the timer ISR (firmwareV2's Lua REPL has no main
 *      loop to tick it from), so the main-context writers here mask that IRQ
 *      around their shadow-register mutation + SPI flush. mtl/firmware ticks
 *      from its main loop and needs no such guard.
 */
#include "ml674061.h"
#include "common.h"

#include "hal/spi.h"
#include "hal/led.h"
#include "utils/delay.h" /* counter_timer (1 ms System Timer tick, tick.c) */
#include "irq.h"         /* irq_disable_save/irq_restore - fade ISR guard */

#if (PCB_RELEASE == LLC2_3) || (PCB_RELEASE == LLC2_4c)
#define LED_WRITE_SPI WriteSPI_1
#elif PCB_RELEASE == LLC2_2
#define LED_WRITE_SPI WriteSPI
#endif

/** @brief Shadow of the driver's dot-correction shift register */
uint8_t led_intensity[14];

/** @brief Mapping array for the LEDs (logical index -> physical LED 0..4) */
uint32_t convled[8]={4,2,0,3,1,0,0,0};

/** @brief Intensity conversion table
 *
 * Gamma 2.2, 8-bit input to 7-bit dot-correction code. Any non-zero input
 * maps to at least 1 so slow fades dim all the way down instead of cutting
 * to black early (the previous table mapped inputs 0..51 to 0).
 */
const uint8_t convintensity[256]=
{
0,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,1,
1,1,2,2,2,2,2,2,2,2,2,3,3,3,3,3,
3,3,4,4,4,4,4,4,5,5,5,5,5,5,6,6,
6,6,6,7,7,7,7,8,8,8,8,9,9,9,9,10,
10,10,10,11,11,11,12,12,12,13,13,13,13,14,14,14,
15,15,15,16,16,17,17,17,18,18,18,19,19,20,20,20,
21,21,22,22,22,23,23,24,24,25,25,26,26,26,27,27,
28,28,29,29,30,30,31,31,32,32,33,33,34,34,35,36,
36,37,37,38,38,39,40,40,41,41,42,42,43,44,44,45,
46,46,47,47,48,49,49,50,51,51,52,53,53,54,55,55,
56,57,58,58,59,60,60,61,62,63,63,64,65,66,66,67,
68,69,70,70,71,72,73,74,74,75,76,77,78,79,79,80,
81,82,83,84,85,85,86,87,88,89,90,91,92,93,94,95,
95,96,97,98,99,100,101,102,103,104,105,106,107,108,109,110,
111,112,113,114,115,116,117,118,119,121,122,123,124,125,126,127
};

/* Fade engine state, indexed by physical LED 0..4, channels R,G,B.
 * led_cur holds the currently displayed color in 8-bit-per-channel space
 * (pre-gamma) so fades interpolate with full resolution. */
static uint8_t  led_cur[5][3];
static uint8_t  led_from[5][3];
static uint8_t  led_to[5][3];
static uint32_t led_fade_start[5];
static uint32_t led_fade_len[5];  /* ms; 0 = no fade running */

/**
 * @brief Pack a 7-bit value into the shift register shadow
 *
 * @param [in] chan Driver channel 0..14 (phys_led*3 + R/G/B)
 * @param [in] val  Dot-correction code, 7 bits
 */
static void led_pack(uint8_t chan, uint8_t val)
{
  uint8_t byte=(chan*7)>>3;
  uint8_t off=(chan*7)&7;
  uint16_t v=((uint16_t)(val&0x7F))<<(9-off);
  uint16_t mask=((uint16_t)0x7F)<<(9-off);

  led_intensity[byte]=(led_intensity[byte]&~(mask>>8))|(v>>8);
  led_intensity[byte+1]=(led_intensity[byte+1]&~(mask&0xFF))|(v&0xFF);
}

/** @brief Shift the whole 14-byte register image out to the driver */
static void led_flush(void)
{
  uint8_t i;

  MODE_LED_DOT_CORRECTION;
  CS_LED_CLEAR;
  for(i=0;i<14;i++)
    LED_WRITE_SPI(led_intensity[i]);
  CS_LED_SET;
  CS_LED_CLEAR;
}

/** @brief Gamma-convert and pack the three channels of one physical LED */
static void led_apply(uint8_t phys)
{
  uint8_t ch;

  for(ch=0;ch<3;ch++)
    led_pack(phys*3+ch,convintensity[led_cur[phys][ch]]);
}

/**
 * @brief Init of the Led RGB driver, clear all leds
 */
void init_led_rgb_driver(void)
{
  uint8_t i;

  //Turn ON Leds
  MODE_LED_ON_OFF_CONTROL;
  CS_LED_CLEAR;
  LED_WRITE_SPI(0xFF);
  LED_WRITE_SPI(0xFF);
  CS_LED_SET;
  CS_LED_CLEAR;
  //Blank Leds
  for(i=0;i<14;i++)
    led_intensity[i]=0;
  led_flush();
}

/**
 * @brief Set a RGB LED color (raw 7-bit codes, no gamma), cancel any fade
 *
 * @note Sometimes the BLUE and GREEN colors are inverted due to different
 *       Part Number of the Waitrony leds...
 *
 * @param [in]  color Reference and color in 32bits
 *                      bits 31->24 RGB led number (1..5)
 *                      bits 23->16 RED intensity in 7bits (Max=>0x7F)
 *                      bits 15->8 GREEN intensity in 7bits (Max=>0x7F)
 *                      bits 7->0  BLUE intensity in 7bits (Max=>0x7F)
 */
void set_led_rgb(uint32_t color)
{
  uint8_t phys=((color>>24)&0x0F)-1;
  uint8_t ch;
  uint32_t irq;

  if (phys>4) return;
  irq=irq_disable_save();          /* keep the fade ISR out of the flush */
  led_fade_len[phys]=0;
  for(ch=0;ch<3;ch++)
  {
    uint8_t v=(color>>(16-8*ch))&0x7F;
    /* keep the 8-bit fade state roughly in sync (7->8 bit expand) */
    led_cur[phys][ch]=(v<<1)|(v>>6);
    led_pack(phys*3+ch,v);
  }
  led_flush();
  irq_restore(irq);
}

/**
 * @brief Set a RGB LED color, using intensity conversion table
 *
 * Cancels any fade running on that LED.
 *
 * @note Sometimes the BLUE and GREEN colors are inverted due to different
 *       Part Number of the Waitrony leds...
 *
 * @param [in]  led   Index of the LED
 * @param [in]  color Reference and color in 32bits
 *                      bits 23->16 RED intensity (0..255)
 *                      bits 15->8 GREEN intensity (0..255)
 *                      bits 7->0  BLUE intensity (0..255)
 */
void set_led(uint8_t led,uint32_t color)
{
  uint8_t phys=convled[led&7];
  uint8_t ch;
  uint32_t irq;

  irq=irq_disable_save();
  led_fade_len[phys]=0;
  for(ch=0;ch<3;ch++)
    led_cur[phys][ch]=(color>>(16-8*ch))&0xFF;
  led_apply(phys);
  led_flush();
  irq_restore(irq);
}

/**
 * @brief Fade a RGB LED from its current color to a target color
 *
 * The fade runs in the background, advanced by led_fade_tick() from the
 * 1 ms System Timer IRQ. A set_led() or another led_fade() on the same LED
 * replaces it.
 *
 * @param [in]  led   Index of the LED (as set_led)
 * @param [in]  color Target color, 8 bits per channel (as set_led)
 * @param [in]  ms    Fade duration in milliseconds
 */
void led_fade(uint8_t led,uint32_t color,uint32_t ms)
{
  uint8_t phys=convled[led&7];
  uint8_t ch;
  uint32_t irq;

  if (ms==0)
  {
    set_led(led,color);
    return;
  }
  irq=irq_disable_save();          /* publish the fade atomically vs the ISR */
  led_fade_len[phys]=0;            /* pause this LED while we set it up */
  for(ch=0;ch<3;ch++)
  {
    led_from[phys][ch]=led_cur[phys][ch];
    led_to[phys][ch]=(color>>(16-8*ch))&0xFF;
  }
  led_fade_start[phys]=counter_timer;
  led_fade_len[phys]=ms;           /* arm last: the ISR acts only when len!=0 */
  irq_restore(irq);
}

/**
 * @brief Advance running fades; call from the 1 ms System Timer IRQ
 *
 * Rate-limited to one interpolation step - and at most one SPI flush for
 * all LEDs together - per LED_FADE_TICK_MS. Runs in ISR context, so it does
 * not guard against itself; main-context writers mask this IRQ instead.
 */
void led_fade_tick(void)
{
  static uint32_t last_tick;
  uint8_t phys,ch,active=0;

  if ((counter_timer-last_tick)<LED_FADE_TICK_MS) return;
  last_tick=counter_timer;
  for(phys=0;phys<5;phys++)
  {
    uint32_t len=led_fade_len[phys];
    uint32_t t;
    if (len==0) continue;
    t=counter_timer-led_fade_start[phys];
    if (t>=len)
    {
      led_fade_len[phys]=0;
      t=len;
    }
    for(ch=0;ch<3;ch++)
      led_cur[phys][ch]=led_from[phys][ch]
        +((int32_t)led_to[phys][ch]-(int32_t)led_from[phys][ch])*(int32_t)t/(int32_t)len;
    led_apply(phys);
    active=1;
  }
  if (active) led_flush();
}
