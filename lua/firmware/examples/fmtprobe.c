/**
 * @file fmtprobe.c
 * @brief Format-isolation probe: feed the VS1003 a REAL WAV then a REAL MP3
 *        (generated on the host, embedded as tones.h) to find out which format
 *        it actually decodes. Uses the same minimal init as playprobe.c
 *        (CLOCKF=0xc000, ver=3).
 *
 * Listen: first sound = WAV, second (after a pause) = MP3. HDAT1 is read right
 * after each feed (nonzero => the decoder recognised that format).
 *
 * Output is on UART0 (38400 8N1), read on the Pi's /dev/serial0:
 *   task lua:firmware:flash EXAMPLE=fmtprobe
 *   (on the Pi) stty -F /dev/serial0 38400 raw -echo; cat /dev/serial0
 */
#include "ml674061.h"
#include "common.h"

#include "hal/spi.h"
#include "hal/uart.h"
#include "tones.h"

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

static int wait_dreq(void)
{
  for (volatile unsigned long i = 0; i < 2000000UL; i++) {
    if (INT_AUDIO_READ & INT_AUDIO_BIT)
      return 1;
    CLR_WDT;
  }
  return 0;
}

static void feed_sdi(const unsigned char *data, unsigned int len)
{
  unsigned int i;
  CS_AUDIO_SDI_CLEAR;
  for (i = 0; i < len; i++) {
    wait_dreq();
    WriteSPI(data[i]);
    get_value(SPDRR0);   /* drain the RX byte each write so SPI0's RX FIFO
                          * cannot overflow mid-stream (init_spi never clears
                          * SPI0 ORF; a stuck overflow could wedge the feed) */
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
  }
  CS_AUDIO_SDI_SET;
}

static void play(const unsigned char *data, unsigned int len, const char *tag)
{
  int k;
  sci_write(0x00, 0x0c00);   /* MODE = SM_SDINEW, native decode */
  sci_write(0x0b, 0x0000);   /* VOLUME loudest */
  TURN_ON_AUDIO_AMPLIFIER;
  for (k = 0; k < 4; k++)    /* repeat so the clip lasts ~1.5 s (easy to hear) */
    feed_sdi(data, len);
  sh_puthex16(tag, sci_read(0x09));   /* HDAT1 right after the data feed */
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

  init_spi();
  clr_wbit(SPCR0, SPCR0_SPE);
  put_wvalue(SPBRR0, 0x00000008);
  set_wbit(SPCR0, SPCR0_SPE);

  RST_AUDIO_CLEAR;
  busy_delay(200000);
  RST_AUDIO_SET;
  sh_puts(wait_dreq() ? "DREQ ready\n" : "DREQ TIMEOUT\n");

  sci_write(0x03, 0xc000);   /* CLOCKF -> PLL */
  busy_delay(200000);
  sh_puthex16("CLOCKF_rb ", sci_read(0x03));

  (void)play; (void)tone_wav;   /* using the chunked path below instead */

  /* DREQ sanity: blast 3000 bytes (> the 2048-byte decoder FIFO) IGNORING DREQ,
   * then read the DREQ pin. If it is still HIGH the flow-control line is stuck
   * (so every feed silently overruns -> corrupt frames -> no decode). */
  sci_write(0x00, 0x0c00);
  CS_AUDIO_SDI_CLEAR;
  {
    unsigned int i;
    for (i = 0; i < 3000; i++) {
      WriteSPI(tone_mp3[i % (sizeof tone_mp3)]);
      get_value(SPDRR0);
    }
  }
  CS_AUDIO_SDI_SET;
  sh_puts((INT_AUDIO_READ & INT_AUDIO_BIT)
            ? "DREQ after 3000B blast: HIGH (stuck -> overrun)\n"
            : "DREQ after 3000B blast: LOW (flow control works)\n");

  /* Chunked MP3 feed, sampling HDAT1 every 512 bytes. If ANY sample is nonzero
   * the decoder synced (=> output problem); all-zero => it never ingests SDI in
   * native mode. Repeat the file so there is plenty to sync + be audible. */
  sh_puts("--- MP3 chunked (HDAT1 per 512B) ---\n");
  sci_write(0x00, 0x0c00);   /* MODE = SM_SDINEW */
  sci_write(0x0b, 0x0000);   /* VOLUME loudest */
  TURN_ON_AUDIO_AMPLIFIER;
  {
    unsigned int r, off, n;
    for (r = 0; r < 6; r++) {
      off = 0;
      while (off < sizeof tone_mp3) {
        n = sizeof tone_mp3 - off;
        if (n > 512) n = 512;
        feed_sdi(tone_mp3 + off, n);
        off += n;
        sh_puthex16("HDAT1 ", sci_read(0x09));
      }
    }
  }
  flush_fill();
  TURN_OFF_AUDIO_AMPLIFIER;

  sh_puts("<<FV_DONE>>\n");
  for (;;) {
  }
  return 0;
}
