/**
 * @file audio.c
 * @brief VLSI VS1003B audio codec over SPI0.
 *
 * Trimmed port of src/firmware/src/hal/audio.c (Violet / RedoX). Keeps the SCI
 * read/write protocol, chip bring-up, volume, amplifier, and the built-in sine
 * test verbatim in behaviour. SCI framing was hardware-verified (SS_VER=3,
 * VOLUME write/read-back). See inc/hal/audio.h.
 *
 * vlsi_play() does real SDI-stream playback, so SCI_VOLUME actually attenuates
 * decoded audio (unlike the sine test). ADPCM record is dropped (future work).
 */
#include "ml674061.h"
#include "common.h"

#include "hal/audio.h"
#include "hal/spi.h"

/* Software delay - firmwareV2 has no timer/DelayMs yet (see blink.c). Sized for
 * the ~1 ms reset window; a few hundred k loops at 33 MHz. */
static void audio_delay(volatile unsigned long n)
{
  while (n--)
    CLR_WDT;
}

uint16_t vlsi_read_sci(uint8_t reg)
{
  uint16_t received_short;
  CS_AUDIO_SCI_CLEAR;
  WriteSPI(0x03);            /* VS1003_READ */
  WriteSPI(reg);
  while (get_wvalue(SPSR0) & SPSR0_RFD)
    get_value(SPDRR0);       /* drain the RX FIFO left by the command bytes */
  received_short = ReadSPI() << 8;
  received_short += ReadSPI();
  CS_AUDIO_SCI_SET;
  return received_short;
}

void vlsi_write_sci(uint8_t reg, uint16_t val)
{
  CS_AUDIO_SCI_CLEAR;
  WriteSPI(0x02);            /* VS1003_WRITE */
  WriteSPI(reg);
  WriteSPI(val >> 8);
  WriteSPI(val);
  /* Each WriteSPI clocks in a byte that WriteSPI itself never consumes. Drain
   * the RX FIFO here so it cannot fill across successive SCI writes and shift
   * later reads (observed: SCI_STATUS mis-read + a corrupted SDI feed). */
  while (get_wvalue(SPSR0) & SPSR0_RFD)
    get_value(SPDRR0);
  CS_AUDIO_SCI_SET;
}

/* Wait for DREQ (INT_AUDIO) high, bounded so a wedged chip cannot hang us. */
static void wait_dreq(void)
{
  unsigned long guard = 0;
  while (!(INT_AUDIO_READ & INT_AUDIO_BIT) && ++guard < 1000000UL)
    CLR_WDT;
}

/* Feed a small fixed control buffer to the SDI (data) interface. Waits for DREQ
 * before each byte (bounded) rather than aborting when it is momentarily low -
 * the 8-byte sine-test sequence must be delivered in full, and DREQ can dip
 * right after the preceding SCI writes. */
static void vlsi_feed_sdi(const uint8_t *data, uint32_t len)
{
  uint32_t i;
  CS_AUDIO_SDI_CLEAR;
  for (i = 0; i < len; i++) {
    wait_dreq();
    WriteSPI(data[i]);
    get_value(SPDRR0);   /* drain the RX byte each write: a long stream would
                          * otherwise overflow SPI0's RX FIFO (init_spi never
                          * clears SPI0 ORF), which stalls the feed */
  }
  CS_AUDIO_SDI_SET;
}

/* Last volume requested via set_vlsi_volume(). The single SCI write below does
 * not durably stick: the Lua heap lives in ExtRAM, and EMC bus activity between
 * a bare VOLUME write and the next decode clears the VS1003's SCI config
 * (CLOCKF/VOLUME reproducibly knocked to 0 by an ExtRAM burst). So we cache the
 * value and re-assert it inside vlsi_play(), in the same post-EMC
 * window where MODE is rewritten - that is the only reason MODE/CLOCKF survive,
 * and VOLUME needs the identical treatment to actually attenuate playback. */
static uint8_t vlsi_volume = 0x20;

void set_vlsi_volume(uint8_t volume)
{
  vlsi_volume = volume;
  vlsi_write_sci(VS1003_VOLUME, (volume << 8) | volume);
}

void vlsi_ampli(uint8_t on)
{
  if (on)
    TURN_ON_AUDIO_AMPLIFIER;
  else
    TURN_OFF_AUDIO_AMPLIFIER;
}

void init_vlsi(void)
{
  /* Pin directions: RST/CS_SCI/CS_SDI/AMP are outputs, DREQ (INT_AUDIO) input. */
  RST_AUDIO_AS_OUTPUT;
  CS_AUDIO_SCI_AS_OUTPUT;
  CS_AUDIO_SDI_AS_OUTPUT;
  CS_AUDIO_AMP_AS_OUTPUT;
  INT_AUDIO_AS_INPUT;
  CS_AUDIO_SCI_SET;
  CS_AUDIO_SDI_SET;
  TURN_OFF_AUDIO_AMPLIFIER;

  /* SPI0 slow (~2 MHz @ 32 MHz APB) while the codec PLL is still at XTALI. */
  clr_wbit(SPCR0, SPCR0_SPE);
  put_wvalue(SPBRR0, 0x00000008);
  set_wbit(SPCR0, SPCR0_SPE);

  /* Hardware reset pulse (active low), then wait for the chip to signal ready
   * (DREQ high) before any SCI write. A fixed delay here raced the VS1003's
   * boot and the first CLOCKF write was being dropped, leaving the core at base
   * XTAL - fast enough for the fixed sine test but far too slow to decode a
   * real stream (playback came out slow + static). */
  RST_AUDIO_CLEAR;
  audio_delay(200000);
  RST_AUDIO_SET;
  wait_dreq();

  /* Native SPI mode + soft reset for a clean decoder state; wait ready. */
  vlsi_write_sci(VS1003_MODE, VS1003_MODE_NATIVE | VS1003_MODE_RESET);
  wait_dreq();

  /* Bring the internal clock up via the PLL. Done AFTER the soft reset (so a
   * reset can't clear it) and while SPI is still slow (the chip only tolerates
   * fast SCI once CLKI is multiplied). Let the PLL lock, then run SPI0 faster
   * (~8 MHz). */
  vlsi_write_sci(VS1003_CLOCKF, 0xc000);
  audio_delay(200000);
  wait_dreq();
  /* Keep SPI0 slow (~2 MHz) - do NOT speed up to ~8 MHz. At 8 MHz, SCI reads
   * returned garbage and playback was slow+static, which only happens if CLKI
   * never left base XTAL (max SCI = CLKI/7). Staying slow lets us read
   * registers back reliably to confirm whether CLOCKF took. */

  set_vlsi_volume(0x20);
}

void vlsi_sine(uint8_t freq_n, uint8_t on)
{
  if (on) {
    const uint8_t start[8] = {0x53, 0xEF, 0x6E, freq_n, 0x00, 0x00, 0x00, 0x00};
    vlsi_write_sci(VS1003_MODE, VS1003_MODE_NATIVE | VS1003_MODE_TESTS);
    vlsi_feed_sdi(start, 8);
  } else {
    const uint8_t stop[8] = {0x45, 0x78, 0x69, 0x74, 0x00, 0x00, 0x00, 0x00};
    vlsi_feed_sdi(stop, 8);
    vlsi_write_sci(VS1003_MODE, VS1003_MODE_NATIVE);
  }
}

/* VS10xx end-of-stream flush length: clock out >=2048 endFillBytes so the
 * decoder's internal buffers finish draining rather than being cut off
 * mid-sample. Bounded like the rest of the SDI feed path. */
#define VLSI_FLUSH_BYTES 2052

void vlsi_play(const uint8_t *data, uint32_t len)
{
  uint32_t i;

  /* Ensure decode (native SPI) mode without a soft reset - the PLL clock is
   * set once in init_vlsi and a reset here (at the fast post-init SPI rate)
   * would risk dropping it back to base XTAL. */
  vlsi_write_sci(VS1003_MODE, VS1003_MODE_NATIVE);
  wait_dreq();

  /* Re-assert the volume here, right before the SDI feed. A bare write from
   * nab.volume() does not survive the EMC traffic the Lua heap generates before
   * playback; rewriting it in this window - like MODE above - is what
   * makes nab.volume actually attenuate the decoded stream. No EMC access falls
   * between this write and the feed below, so it lands. */
  vlsi_write_sci(VS1003_VOLUME, (vlsi_volume << 8) | vlsi_volume);

  vlsi_ampli(1);
  vlsi_feed_sdi(data, len);

  /* End-of-stream flush. Zero is the correct endFillByte for PCM/WAV, so feed
   * zeros - avoids the VS1053-only WRAM endFillByte read (0x1E06) this used to
   * do, which returns garbage on the VS1003B and injected ~2 KB of noise. */
  CS_AUDIO_SDI_CLEAR;
  for (i = 0; i < VLSI_FLUSH_BYTES; i++) {
    wait_dreq();
    WriteSPI(0x00);
    get_value(SPDRR0);   /* drain, same as vlsi_feed_sdi */
  }
  CS_AUDIO_SDI_SET;

  vlsi_ampli(0);
}
