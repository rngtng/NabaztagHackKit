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
 *      MODE readback + HDAT1 poll (no ExtRAM access anywhere).
 *   B. ExtRAM burst   -> CLOCKF/MODE readback (the #123 knock, now at 32 MHz).
 *   C. re-arm record  -> HDAT1 polls with an ExtRAM burst between each
 *      (what a real nab.record loop does via the Lua heap).
 *   D. region bisect: burst at 0xD0000000 (lua heap base) vs 0xD0010000 (mtl
 *      deliberately moved SRAM_BASE here, 0xD0000000 commented out in mem.h)
 *      vs two higher windows - is the knock address-dependent?
 *   E. trigger shape at the knocking region: 1 write / 16 writes / 8192
 *      reads-only - how much EMC traffic does it take, and do reads do it?
 *
 * Round-1 result (2026-07-27): A records (HDAT1 0x0300), B knocks to full
 * post-reset set (CLOCKF 0, MODE 0x0800, STATUS 0x38), C0/C1 rewrites take
 * but HDAT1 stays 0 under interleaved bursts. Knock == chip reset by EMC
 * traffic; writes themselves are fine.
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

static void sh_puthex32(const char *label, unsigned long v)
{
  const char *hex = "0123456789abcdef";
  char b[12];
  int i;
  b[0] = '0'; b[1] = 'x';
  for (i = 0; i < 8; i++)
    b[2 + i] = hex[(v >> (28 - 4 * i)) & 0xF];
  b[10] = '\n'; b[11] = '\0';
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
 * through an ExtRAM window (the Lua heap lives at 0xD0000000). */
static unsigned int extram_burst_at(unsigned long base, unsigned int seed)
{
  volatile unsigned int *ext = (volatile unsigned int *)base;
  unsigned int i, s = 0;
  for (i = 0; i < 8192; i++) ext[i] = (i + seed) * 2654435761u;
  for (i = 0; i < 8192; i++) s += ext[i];
  return s;
}

static unsigned int extram_burst(unsigned int seed)
{
  return extram_burst_at(0xD0000000UL, seed);
}

/* Re-arm CLOCKF and confirm it took; returns the readback. */
static uint16_t clockf_arm(void)
{
  sci_write(VS1003_CLOCKF, 0xc000);
  busy_delay(800000);
  wait_dreq();
  return sci_read(VS1003_CLOCKF);
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
  /* ~2 s so the Pi-side capture attaches before the first data line (round 1
   * lost A0-A3 to this race). */
  busy_delay(16000000);
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
  sh_puthex16("A2 CLOCKF(rb)       ", clockf_arm());

  rec_start_8k();
  sh_puthex16("A3 MODE(rec)        ", sci_read(VS1003_MODE));
  for (i = 0; i < 3; i++) {
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
  sh_puthex16("C0 CLOCKF(rb)       ", clockf_arm());
  rec_start_8k();
  sh_puthex16("C1 MODE(rec)        ", sci_read(VS1003_MODE));
  for (i = 0; i < 3; i++) {
    (void)extram_burst(i);
    busy_delay(1000000);
    sh_puthex16("C2 HDAT1            ", sci_read(VS1003_HDAT1));
  }
  sh_puthex16("C3 MODE(end)        ", sci_read(VS1003_MODE));
  sh_puthex16("C4 CLOCKF(end)      ", sci_read(VS1003_CLOCKF));
  rec_stop();

  /* --- D: is the knock address-dependent? Burst four ExtRAM windows. ------
   * 0xD0000000 = lua heap base (knocks, per B). 0xD0010000 = mtl's SRAM_BASE
   * (mem.h shows 0xD0000000 deliberately commented out - suspicion: Violet
   * hit this same bug). Then two higher windows. */
  {
    static const unsigned long bases[4] = {
      0xD0000000UL, 0xD0010000UL, 0xD0080000UL, 0xD00F0000UL
    };
    for (i = 0; i < 4; i++) {
      sh_puthex16("D0 arm CLOCKF       ", clockf_arm());
      sh_puthex16("D1 burst hi-addr    ", (uint16_t)(bases[i] >> 16));
      (void)extram_burst_at(bases[i], i);
      sh_puthex16("D2 CLOCKF(post)     ", sci_read(VS1003_CLOCKF));
    }
  }

  /* --- E: trigger shape at 0xD0000000 - how little traffic knocks it, and
   * do reads alone? ------------------------------------------------------- */
  {
    volatile unsigned int *ext = (volatile unsigned int *)0xD0000000;
    unsigned int s = 0;

    sh_puthex16("E0 arm CLOCKF       ", clockf_arm());
    ext[0] = 0xdeadbeef;                       /* single word write */
    sh_puthex16("E1 CLOCKF(1 write)  ", sci_read(VS1003_CLOCKF));

    sh_puthex16("E2 arm CLOCKF       ", clockf_arm());
    for (i = 0; i < 16; i++) ext[i] = i;       /* 16 writes */
    sh_puthex16("E3 CLOCKF(16 writes)", sci_read(VS1003_CLOCKF));

    sh_puthex16("E4 arm CLOCKF       ", clockf_arm());
    for (i = 0; i < 8192; i++) s += ext[i];    /* reads only */
    sh_puthex16("E5 CLOCKF(8k reads) ", sci_read(VS1003_CLOCKF));
    sh_puthex16("E6 read checksum    ", (uint16_t)s);
  }

  /* --- F: pin-mux. mtl main.c zeroes ALL PORTSEL1-5 at init then re-selects;
   * lua never zeroes (drivers only set_wbit on top of reset defaults). If a
   * default leaves a pin on its external-bus function, it toggles on EMC
   * WRITES only (XWR/XBWE) - matching E. Dump defaults, zero like mtl
   * (re-asserting UART0 PB0/1 + SPI0), burst, readback. */
  sh_puthex32("F1 PORTSEL1         ", get_wvalue(PORTSEL1));
  sh_puthex32("F2 PORTSEL2         ", get_wvalue(PORTSEL2));
  sh_puthex32("F3 PORTSEL3         ", get_wvalue(PORTSEL3));
  sh_puthex32("F4 PORTSEL4         ", get_wvalue(PORTSEL4));
  sh_puthex32("F5 PORTSEL5         ", get_wvalue(PORTSEL5));

  put_wvalue(PORTSEL1, 0);
  put_wvalue(PORTSEL2, 0);
  put_wvalue(PORTSEL3, 0);
  put_wvalue(PORTSEL4, 0);
  put_wvalue(PORTSEL5, 0);
  set_wbit(PORTSEL1, 0x50000);       /* UART0 on PB0/PB1 back */
  set_wbit(PORTSEL2, 0x00000015);    /* SPI0 back */

  sh_puthex16("F6 arm CLOCKF       ", clockf_arm());
  (void)extram_burst(3);
  sh_puthex16("F7 CLOCKF(post-ext) ", sci_read(VS1003_CLOCKF));

  /* --- G: single-bit isolation. Round 3 showed mtl's full mux set stops the
   * knock; mtl's comment names the mechanism: PORTSEL4|=0x40 switches XD16-31
   * (upper external data bus half - unused, BWC=0xA0 is a 16-bit bank) from
   * bus function to GPIO. Those package pins carry the audio control GPIOs on
   * this board, so as bus pins they toggle on every EMC WRITE (hi-Z on reads)
   * - matching E exactly. Set ONLY that bit. */
  set_wbit(PORTSEL4, 0x00000040);
  sh_puthex16("G0 arm CLOCKF       ", clockf_arm());
  (void)extram_burst(4);
  sh_puthex16("G1 CLOCKF(post-ext) ", sci_read(VS1003_CLOCKF));

  /* If the knock is gone, prove record now runs under write traffic. */
  rec_start_8k();
  for (i = 0; i < 3; i++) {
    (void)extram_burst(i + 9);
    busy_delay(1000000);
    sh_puthex16("G2 HDAT1            ", sci_read(VS1003_HDAT1));
  }
  rec_stop();

  sh_puts("<<FV_DONE>>\n");
  for (;;) {
  }
  return 0;
}
