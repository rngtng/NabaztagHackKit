/**
 * @file volprobe.c
 * @brief Volume-isolation probe: play the embedded MP3 at volume 0 (loudest)
 *        then 254 (near-silent), minimal init, NO console I/O during the SDI
 *        feed (prints only between plays). If loudness clearly differs,
 *        SCI_VOLUME works on decoded audio and the lua app's "no volume" is a
 *        context problem (init_hw and/or console reads interleaved with SPI0),
 *        not the VS1003.
 *
 * Output is on UART0 (38400 8N1), read on the Pi's /dev/serial0 (see
 * uartprobe.c for the flash+listen recipe):
 *   task lua:firmware:flash APP=volprobe
 */
#include "ml674061.h"
#include "common.h"

#include "hal/spi.h"
#include "hal/led.h"
#include "hal/button.h"
#include "hal/adc.h"
#include "hal/i2c.h"
#include "hal/motor.h"
#include "hal/uart.h"
#include "tone_mp3.h"

static void sh_puts(const char *s)
{
  putst_uart((uint8_t *)s);
}

static void sh_puthex16(const char *label, uint16_t v)
{
  const char *hex = "0123456789abcdef";
  char b[8];
  b[0] = '0'; b[1] = 'x';
  b[2] = hex[(v >> 12) & 0xF];
  b[3] = hex[(v >> 8) & 0xF];
  b[4] = hex[(v >> 4) & 0xF];
  b[5] = hex[v & 0xF];
  b[6] = '\n'; b[7] = '\0';
  sh_puts(label);
  sh_puts(b);
}

static uint16_t sci_read(uint8_t reg)
{
  uint16_t v;
  CS_AUDIO_SCI_CLEAR;
  WriteSPI(0x03);
  WriteSPI(reg);
  while (get_wvalue(SPSR0) & SPSR0_RFD)
    get_value(SPDRR0);
  v = ReadSPI() << 8;
  v += ReadSPI();
  CS_AUDIO_SCI_SET;
  return v;
}

static void sci_write(uint8_t reg, uint16_t val)
{
  CS_AUDIO_SCI_CLEAR;
  WriteSPI(0x02);
  WriteSPI(reg);
  WriteSPI(val >> 8);
  WriteSPI(val);
  while (get_wvalue(SPSR0) & SPSR0_RFD)
    get_value(SPDRR0);
  CS_AUDIO_SCI_SET;
}

static void busy_delay(volatile unsigned long n)
{
  while (n--)
    CLR_WDT;
}

static void wait_dreq(void)
{
  unsigned long g = 0;
  while (!(INT_AUDIO_READ & INT_AUDIO_BIT) && ++g < 2000000UL)
    CLR_WDT;
}

static void feed_sdi(const unsigned char *data, unsigned int len)
{
  unsigned int i;
  CS_AUDIO_SDI_CLEAR;
  for (i = 0; i < len; i++) {
    wait_dreq();
    WriteSPI(data[i]);
    get_value(SPDRR0);
  }
  CS_AUDIO_SDI_SET;
}

static void flush_fill(void)
{
  int f;
  CS_AUDIO_SDI_CLEAR;
  for (f = 0; f < 2052; f++) {
    wait_dreq();
    WriteSPI(0x00);
    get_value(SPDRR0);
  }
  CS_AUDIO_SDI_SET;
}

/* Play the embedded MP3 a few times at the current volume. No console I/O here. */
static void play_mp3(void)
{
  int k;
  sci_write(0x00, 0x0c00);   /* MODE = SM_SDINEW | SM_SDISHARE */
  TURN_ON_AUDIO_AMPLIFIER;
  for (k = 0; k < 4; k++)
    feed_sdi(nab_tone_mp3, sizeof nab_tone_mp3);
  flush_fill();
  TURN_OFF_AUDIO_AMPLIFIER;
}

int main(void)
{
  init_uart();

  RST_AUDIO_AS_OUTPUT;
  CS_AUDIO_SCI_AS_OUTPUT;
  CS_AUDIO_SDI_AS_OUTPUT;
  CS_AUDIO_AMP_AS_OUTPUT;
  INT_AUDIO_AS_INPUT;
  CS_AUDIO_SCI_SET;
  CS_AUDIO_SDI_SET;
  TURN_OFF_AUDIO_AMPLIFIER;

  /* Mirror the lua app's init_hw order: LED bus + button between init_spi and
   * the audio bring-up. */
  CS_LED_AS_OUTPUT;
  MODE_LED_AS_OUTPUT;
  CS_LED_SET;
  MODE_LED_CLEAR;
  init_spi();
  init_led_rgb_driver();
  init_button();

  clr_wbit(SPCR0, SPCR0_SPE);
  put_wvalue(SPBRR0, 0x00000008);
  set_wbit(SPCR0, SPCR0_SPE);

  RST_AUDIO_CLEAR;
  busy_delay(200000);
  RST_AUDIO_SET;
  wait_dreq();

  sci_write(0x03, 0xc000);   /* CLOCKF -> PLL */
  busy_delay(200000);
  sh_puthex16("CLOCKF(before) ", sci_read(0x03));

  /* Bisect: run the post-audio peripheral inits the lua app does, then re-read
   * SCI. If CLOCKF/VOL readbacks go garbage after this, one of these inits is
   * what breaks SPI0 SCI in the full app. */
  init_adc();
  init_i2c();
  init_ears();
  sh_puthex16("CLOCKF(after all inits) ", sci_read(0x03));

  /* Mimic the lua REPL: pull input through the UART RX poll (getch_uart) the
   * way reading a line does, then re-read SCI. If the readback goes garbage
   * here, console INPUT (the one thing the app does that no probe does) is what
   * corrupts SPI0 SCI. */
  {
    int i;
    for (i = 0; i < 40; i++)
      (void)getch_uart();
  }
  sh_puthex16("CLOCKF(after READC burst) ", sci_read(0x03));

  /* ExtRAM stress: the lua app runs its heap in ExtRAM (EMC, 0xD0000000); no
   * probe does. Hammer it, then re-read SCI. If the readback corrupts, EMC bus
   * activity is what perturbs SPI0 in the full app. */
  {
    volatile unsigned int *ext = (volatile unsigned int *)0xD0000000;
    unsigned int i, s = 0;
    for (i = 0; i < 8192; i++) ext[i] = i * 2654435761u;
    for (i = 0; i < 8192; i++) s += ext[i];
    sh_puthex16("ExtRAM checksum ", (unsigned short)s);
  }
  sh_puthex16("CLOCKF(after ExtRAM stress) ", sci_read(0x03));

  /* Reset vs read-glitch: re-write CLOCKF, read back. If it reads 0xc000 the
   * register really was cleared (chip reset by EMC), not a corrupted read. */
  sci_write(0x03, 0xc000);
  sh_puthex16("CLOCKF(rewritten) ", sci_read(0x03));

  /* Does it re-corrupt on the NEXT ExtRAM burst? */
  {
    volatile unsigned int *ext = (volatile unsigned int *)0xD0000000;
    unsigned int i; for (i = 0; i < 8192; i++) ext[i] = i + 7;
  }
  sh_puthex16("CLOCKF(after 2nd ExtRAM) ", sci_read(0x03));

  /* --- volume 0 (loudest) --- */
  sci_write(0x0b, 0x0000);
  sh_puthex16("VOL_rb(want 0x0000) ", sci_read(0x0b));
  sh_puts("--- playing at VOL 0 (LOUD) ---\n");
  play_mp3();

  busy_delay(6000000);       /* audible gap, no console I/O */

  /* --- volume 254 (near silent) --- */
  sci_write(0x0b, 0xfefe);
  sh_puthex16("VOL_rb(want 0xfefe) ", sci_read(0x0b));
  sh_puts("--- playing at VOL 254 (SILENT) ---\n");
  play_mp3();

  sh_puts("<<FV_DONE>>\n");
  for (;;) {
  }
  return 0;
}
