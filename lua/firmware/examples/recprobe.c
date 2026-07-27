/**
 * @file recprobe.c
 * @brief Record-isolation probe for #275: run the ADPCM record bring-up as
 *        pure C (no Lua, no ExtRAM heap) and bisect the ExtRAM/EMC knock.
 *
 * Mirrors the mtl main() probe that produced the "working" reference capture
 * (MODE=0x3c00, HDAT1 climbs), then re-adds the lua app's one missing factor -
 * ExtRAM (EMC) traffic - in controlled steps, extending volprobe's #123 ladder:
 *
 *   A. bare bring-up:  CLOCKF write -> immediate readback; rec_start ->
 *      MODE readback + HDAT1 poll x8 (no ExtRAM access anywhere).
 *   B. ExtRAM burst   -> CLOCKF/MODE readback (the #123 knock, now at 32 MHz).
 *   C. re-arm record  -> HDAT1 polls with an ExtRAM burst between each
 *      (what a real nab.record loop does via the Lua heap).
 *
 * All SCI transfers run at SPBRR0=0x20 (~0.5 MHz) - the only speed that reads
 * back reliably while CLKI is at base XTAL (#275); still fine at 49 MHz.
 * Delays are 32 MHz-scaled (busy_delay 800000 ~= the #123-verified window).
 *
 *   task lua:firmware:flash EXAMPLE=recprobe CAPTURE=1
 */
#include "ml674061.h"
#include "common.h"

#include "hal/spi.h"
#include "hal/uart.h"
#include "hal/audio.h"

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

static uint16_t sci_read(uint8_t reg)
{
  uint16_t v;
  wait_dreq();
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
  wait_dreq();
  CS_AUDIO_SCI_CLEAR;
  WriteSPI(0x02);
  WriteSPI(reg);
  WriteSPI(val >> 8);
  WriteSPI(val);
  while (get_wvalue(SPSR0) & SPSR0_RFD)
    get_value(SPDRR0);
  CS_AUDIO_SCI_SET;
}

/* One EMC burst ~ what a Lua allocation sweep does: 8192 word writes + reads
 * through the ExtRAM window the Lua heap lives in (0xD0000000). */
static unsigned int extram_burst(unsigned int seed)
{
  volatile unsigned int *ext = (volatile unsigned int *)0xD0000000;
  unsigned int i, s = 0;
  for (i = 0; i < 8192; i++) ext[i] = (i + seed) * 2654435761u;
  for (i = 0; i < 8192; i++) s += ext[i];
  return s;
}

static void rec_start_8k(void)
{
  sci_write(VS1003_AICTRL0, 192000UL / 8000);   /* 0x0018 */
  sci_write(VS1003_AICTRL1, 0);                 /* AGC */
  sci_write(VS1003_MODE,
            VS1003_MODE_NATIVE | VS1003_MODE_ADPCM | VS1003_MODE_RESET);
  busy_delay(800000);                           /* post-reset settle */
  wait_dreq();
}

static void rec_stop(void)
{
  sci_write(VS1003_MODE, VS1003_MODE_NATIVE | VS1003_MODE_RESET);
  busy_delay(800000);
  wait_dreq();
}

int main(void)
{
  int i;

  init_uart();
  sh_puts("NAB-REC-PROBE (#275)\n");

  RST_AUDIO_AS_OUTPUT;
  CS_AUDIO_SCI_AS_OUTPUT;
  CS_AUDIO_SDI_AS_OUTPUT;
  CS_AUDIO_AMP_AS_OUTPUT;
  INT_AUDIO_AS_INPUT;
  CS_AUDIO_SCI_SET;
  CS_AUDIO_SDI_SET;
  TURN_OFF_AUDIO_AMPLIFIER;

  init_spi();

  /* SCI at ~0.5 MHz for reliable readback at base CLKI. */
  clr_wbit(SPCR0, SPCR0_SPE);
  put_wvalue(SPBRR0, 0x00000020);
  set_wbit(SPCR0, SPCR0_SPE);

  /* Hardware reset pulse, 32 MHz-scaled window (see hal/audio.c #269 note). */
  RST_AUDIO_CLEAR;
  busy_delay(800000);
  RST_AUDIO_SET;
  wait_dreq();

  sh_puthex16("A0 STATUS(reset)    ", sci_read(VS1003_STATUS));
  sh_puthex16("A1 MODE(reset)      ", sci_read(VS1003_MODE));

  /* --- A: bare bring-up, zero ExtRAM access ------------------------------ */
  sci_write(VS1003_CLOCKF, 0xc000);
  busy_delay(800000);
  wait_dreq();
  sh_puthex16("A2 CLOCKF(rb)       ", sci_read(VS1003_CLOCKF));

  rec_start_8k();
  sh_puthex16("A3 MODE(rec)        ", sci_read(VS1003_MODE));
  for (i = 0; i < 8; i++) {
    busy_delay(1000000);
    sh_puthex16("A4 HDAT1            ", sci_read(VS1003_HDAT1));
  }
  rec_stop();

  /* --- B: one ExtRAM burst, then readback (the #123 knock, now @32 MHz) -- */
  sh_puthex16("B0 ext checksum     ", (uint16_t)extram_burst(7));
  sh_puthex16("B1 CLOCKF(post-ext) ", sci_read(VS1003_CLOCKF));
  sh_puthex16("B2 MODE(post-ext)   ", sci_read(VS1003_MODE));
  sh_puthex16("B3 STATUS(post-ext) ", sci_read(VS1003_STATUS));

  /* --- C: record while ExtRAM traffic interleaves (a real nab.record) ---- */
  sci_write(VS1003_CLOCKF, 0xc000);
  busy_delay(800000);
  wait_dreq();
  sh_puthex16("C0 CLOCKF(rb)       ", sci_read(VS1003_CLOCKF));
  rec_start_8k();
  sh_puthex16("C1 MODE(rec)        ", sci_read(VS1003_MODE));
  for (i = 0; i < 8; i++) {
    (void)extram_burst(i);
    busy_delay(1000000);
    sh_puthex16("C2 HDAT1            ", sci_read(VS1003_HDAT1));
  }
  sh_puthex16("C3 MODE(end)        ", sci_read(VS1003_MODE));
  sh_puthex16("C4 CLOCKF(end)      ", sci_read(VS1003_CLOCKF));
  rec_stop();

  sh_puts("<<FV_DONE>>\n");
  for (;;) {
  }
  return 0;
}
