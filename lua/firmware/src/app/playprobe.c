/**
 * @file playprobe.c
 * @brief Decode-isolation probe: does the VS1003 actually decode a PCM stream,
 *        and does CLOCKF (the PLL clock) stick?
 *
 * audioprobe.c proved the chip is alive and SCI read/write work at slow SPI.
 * This probe adds the minimum decode path (CLOCKF -> readback -> feed a tiny
 * 16-bit PCM WAV over SDI) with the SAME minimal init, so decode is tested
 * without the Lua app's full init_hw (LED/button/ADC/I2C). Everything is
 * self-contained (its own SCI/SDI helpers) so it does not depend on audio.c.
 *
 *   task repl:firmwareV2:hw APP=playprobe   (listen; watch the console)
 */
#include "ml674061.h"
#include "common.h"

#include "hal/spi.h"

#define SYS_WRITEC 0x03

static inline int semihost(int op, void *arg)
{
  register int r0 asm("r0") = op;
  register void *r1 asm("r1") = arg;
  asm volatile("svc #0xAB" : "+r"(r0) : "r"(r1) : "memory");
  return r0;
}

static void sh_puts(const char *s)
{
  while (*s) {
    char c = *s++;
    semihost(SYS_WRITEC, &c);
  }
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

static void feed_sdi(const uint8_t *data, uint32_t len)
{
  uint32_t i;
  CS_AUDIO_SDI_CLEAR;
  for (i = 0; i < len; i++) {
    wait_dreq();
    WriteSPI(data[i]);
  }
  CS_AUDIO_SDI_SET;
}

/* Tiny mono 16-bit PCM WAV, ~200 ms 100 Hz square wave @ 8 kHz. */
#define HZ      8000
#define SAMPLES 1600
#define PERIOD  40
#define AMP     8000
static uint8_t wav[44 + SAMPLES * 2];

static void put16(uint8_t *p, uint16_t v) { p[0] = v; p[1] = v >> 8; }
static void put32(uint8_t *p, uint32_t v) { p[0] = v; p[1] = v >> 8; p[2] = v >> 16; p[3] = v >> 24; }

static void build_wav(void)
{
  uint32_t i;
  uint32_t datalen = SAMPLES * 2;
  __builtin_memcpy(wav + 0, "RIFF", 4);
  put32(wav + 4, 36 + datalen);
  __builtin_memcpy(wav + 8, "WAVE", 4);
  __builtin_memcpy(wav + 12, "fmt ", 4);
  put32(wav + 16, 16);
  put16(wav + 20, 1);
  put16(wav + 22, 1);
  put32(wav + 24, HZ);
  put32(wav + 28, HZ * 2);
  put16(wav + 32, 2);
  put16(wav + 34, 16);
  __builtin_memcpy(wav + 36, "data", 4);
  put32(wav + 40, datalen);
  for (i = 0; i < SAMPLES; i++) {
    int s = ((i / PERIOD) & 1) ? AMP : -AMP;
    put16(wav + 44 + i * 2, (uint16_t)s);
  }
}

int main(void)
{
  int k;

  RST_AUDIO_AS_OUTPUT;
  CS_AUDIO_SCI_AS_OUTPUT;
  CS_AUDIO_SDI_AS_OUTPUT;
  CS_AUDIO_AMP_AS_OUTPUT;
  INT_AUDIO_AS_INPUT;
  CS_AUDIO_SCI_SET;
  CS_AUDIO_SDI_SET;
  TURN_OFF_AUDIO_AMPLIFIER;

  init_spi();

  /* slow SPI (~2 MHz) for pre-PLL bring-up + reliable readback */
  clr_wbit(SPCR0, SPCR0_SPE);
  put_wvalue(SPBRR0, 0x00000008);
  set_wbit(SPCR0, SPCR0_SPE);

  RST_AUDIO_CLEAR;
  busy_delay(200000);
  RST_AUDIO_SET;
  sh_puts(wait_dreq() ? "DREQ ready\n" : "DREQ TIMEOUT\n");

  sh_puthex16("STATUS ", sci_read(0x01));

  /* Set the PLL clock and read it straight back (slow SPI = trustworthy). */
  sci_write(0x03, 0xc000);           /* CLOCKF */
  busy_delay(200000);
  sh_puthex16("CLOCKF_rb ", sci_read(0x03));

  /* Native decode mode, full volume. */
  sci_write(0x00, 0x0800);           /* MODE = SM_SDINEW */
  sci_write(0x0b, 0x0000);           /* VOLUME = loudest */
  sh_puthex16("MODE_rb ", sci_read(0x00));
  sh_puthex16("VOL_rb ", sci_read(0x0b));

  build_wav();

  /* Play it a few times so it is easy to hear. */
  for (k = 0; k < 4; k++) {
    TURN_ON_AUDIO_AMPLIFIER;
    feed_sdi(wav, sizeof wav);
    /* zero endFillByte flush */
    CS_AUDIO_SDI_CLEAR;
    { int f; for (f = 0; f < 2052; f++) { wait_dreq(); WriteSPI(0x00); } }
    CS_AUDIO_SDI_SET;
    TURN_OFF_AUDIO_AMPLIFIER;
  }

  sh_puthex16("HDAT1 ", sci_read(0x09));
  sh_puthex16("HDAT0 ", sci_read(0x08));

  sh_puts("<<FV_DONE>>\n");
  for (;;) {
  }
  return 0;
}
