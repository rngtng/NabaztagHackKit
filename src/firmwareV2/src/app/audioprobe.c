/**
 * @file audioprobe.c
 * @brief M8 probe (issue #116): confirm the VLSI VS1003B audio codec is alive
 *        on SPI0 before porting the full driver + Lua bindings.
 *
 * The board teardown (docs/hardware-dissection.md) photo-confirms a VS1003B, but
 * per the CLAUDE.md peripheral-exists rule (learned from the M6 AT45 phantom,
 * #94) we prove the chip actually responds over the bus before building on it.
 * This is the cheapest disqualifying test: reset the chip, read SCI_STATUS (its
 * version nibble should be 3 for a VS1003), then write VOLUME and read it back -
 * a write/read-back round-trip is proof the chip is present, not just floating
 * lines. A DREQ timeout keeps a missing/dead chip from hanging the probe.
 *
 * Audio is on SPI0 (WriteSPI/ReadSPI); the LEDs are on SPI1 - separate buses.
 * Output is ARM semihosting (the M3 #91 console), so run it debugger-attached:
 *   task repl:firmwareV2:hw APP=audioprobe
 */
#include "ml674061.h"
#include "common.h"

#include "hal/spi.h"

/* ---- semihosting console (M3 #91 path) ----------------------------------- */
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

static void sh_puthex16(uint16_t v)
{
  const char *hex = "0123456789abcdef";
  char b[7];
  b[0] = '0'; b[1] = 'x';
  b[2] = hex[(v >> 12) & 0xF];
  b[3] = hex[(v >> 8) & 0xF];
  b[4] = hex[(v >> 4) & 0xF];
  b[5] = hex[v & 0xF];
  b[6] = '\0';
  sh_puts(b);
}

/* ---- VS1003 SCI over SPI0 (ported from src/firmware/src/hal/audio.c) ------ */
/* SCI read drains the RX FIFO left by the two command-byte writes before
 * clocking the 16-bit value, exactly as vlsi_read_sci does. */
static uint16_t sci_read(uint8_t reg)
{
  uint16_t v;
  CS_AUDIO_SCI_CLEAR;
  WriteSPI(0x03);            /* VS1003_READ */
  WriteSPI(reg);
  while (get_wvalue(SPSR0) & SPSR0_RFD)
    get_value(SPDRR0);       /* clear reception buffer */
  v = ReadSPI() << 8;
  v += ReadSPI();
  CS_AUDIO_SCI_SET;
  return v;
}

static void sci_write(uint8_t reg, uint16_t val)
{
  CS_AUDIO_SCI_CLEAR;
  WriteSPI(0x02);            /* VS1003_WRITE */
  WriteSPI(reg);
  WriteSPI(val >> 8);
  WriteSPI(val);
  CS_AUDIO_SCI_SET;
}

static void busy_delay(volatile unsigned long n)
{
  while (n--)
    CLR_WDT;
}

/* Wait for DREQ (INT_AUDIO) high, bounded so a silent chip can't hang us.
 * Returns 1 if it went high, 0 on timeout. */
static int wait_dreq(void)
{
  for (volatile unsigned long i = 0; i < 2000000UL; i++) {
    if (INT_AUDIO_READ & INT_AUDIO_BIT)
      return 1;
    CLR_WDT;
  }
  return 0;
}

int main(void)
{
  /* Pin directions: RST/CS_SCI/CS_SDI/AMP are outputs, DREQ is an input. */
  RST_AUDIO_AS_OUTPUT;
  CS_AUDIO_SCI_AS_OUTPUT;
  CS_AUDIO_SDI_AS_OUTPUT;
  CS_AUDIO_AMP_AS_OUTPUT;
  INT_AUDIO_AS_INPUT;
  CS_AUDIO_SCI_SET;          /* SCI/SDI idle high */
  CS_AUDIO_SDI_SET;
  TURN_OFF_AUDIO_AMPLIFIER;  /* no sound needed for a register probe */

  init_spi();

  /* SCI reads are limited pre-PLL (chip runs at XTALI). Drop SPI0 to ~2 MHz
   * (SPBRR0=8 @ 32 MHz APB) like init_vlsi does before touching the codec. */
  clr_wbit(SPCR0, SPCR0_SPE);
  put_wvalue(SPBRR0, 0x00000008);
  set_wbit(SPCR0, SPCR0_SPE);

  /* Hardware reset pulse (RST is active low). */
  RST_AUDIO_CLEAR;
  busy_delay(200000);
  RST_AUDIO_SET;
  busy_delay(200000);

  sh_puts("M8 audio probe (VS1003B on SPI0)\n");
  sh_puts(wait_dreq() ? "DREQ: high (chip ready)\n"
                      : "DREQ: LOW - timeout, chip absent/unpowered?\n");

  uint16_t status = sci_read(0x01);   /* SCI_STATUS */
  sh_puts("SCI_STATUS=");
  sh_puthex16(status);
  sh_puts(" ver=");
  sh_puthex16((status >> 4) & 0x0F);  /* SS_VER; VS1003 = 3 */
  sh_puts("\n");

  /* Write/read-back a VOLUME value - the real aliveness test. */
  sci_write(0x0B, 0x2A2A);            /* SCI_VOLUME */
  uint16_t vol = sci_read(0x0B);
  sh_puts("VOLUME readback=");
  sh_puthex16(vol);
  sh_puts(vol == 0x2A2A ? "  -> VS1003 ALIVE\n"
                        : "  -> NO RESPONSE (lines floating?)\n");

  for (;;) {
    /* idle; the report above is the whole probe */
  }
  return 0;
}
