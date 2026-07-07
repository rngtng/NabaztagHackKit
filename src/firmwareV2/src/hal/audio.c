/**
 * @file audio.c
 * @brief VLSI VS1003B audio codec over SPI0 (M8, #116).
 *
 * Trimmed port of src/firmware/src/hal/audio.c (Violet / RedoX). Keeps the SCI
 * read/write protocol, chip bring-up, volume, amplifier, and the built-in sine
 * test verbatim in behaviour; drops the SDI-stream playback / ADPCM record
 * paths (future work). SCI framing was hardware-verified by the M8 probe
 * (SS_VER=3, VOLUME write/read-back). See inc/hal/audio.h.
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
  CS_AUDIO_SCI_SET;
}

/* Feed a small fixed buffer to the SDI (data) interface, respecting DREQ. Used
 * for the 8-byte sine-test control sequences. */
static void vlsi_feed_sdi(const uint8_t *data, uint32_t len)
{
  uint32_t i = 0;
  CS_AUDIO_SDI_CLEAR;
  while (i < len && (INT_AUDIO_READ & INT_AUDIO_BIT))
    WriteSPI(data[i++]);
  CS_AUDIO_SDI_SET;
}

void set_vlsi_volume(uint8_t volume)
{
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

  /* Hardware reset pulse (active low). */
  RST_AUDIO_CLEAR;
  audio_delay(200000);
  RST_AUDIO_SET;
  audio_delay(200000);

  /* Bring the internal clock up via the PLL, then run SPI0 faster (~8 MHz). */
  vlsi_write_sci(VS1003_CLOCKF, 0xc000);
  clr_wbit(SPCR0, SPCR0_SPE);
  put_wvalue(SPBRR0, 0x00000002);
  set_wbit(SPCR0, SPCR0_SPE);

  /* Native SPI mode + soft reset, then a moderate default volume. */
  vlsi_write_sci(VS1003_MODE, VS1003_MODE_NATIVE | VS1003_MODE_RESET);
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
