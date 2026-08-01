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
 * decoded audio (unlike the sine test). vlsi_rec_start/read/stop add IMA-ADPCM
 * microphone record, ported from src/firmware's init_adpcm_encode/rec_check/
 * stop_adpcm_encode.
 *
 * #265 split that playback path into vlsi_stream_start/feed/busy/stop, where
 * feed never waits (it pushes what the decoder can take and returns the count),
 * so Lua can keep the codec fed from its cooperative loop and the CPU is free
 * in between - the rabbit plays a sound AND animates/serves/answers the REPL.
 * vlsi_play() is now that same primitive with the waiting put back.
 */
#include "ml674061.h"
#include "common.h"

#include "hal/audio.h"
#include "hal/spi.h"

/* Software delay - init_vlsi runs before the tick, so no timer/DelayMs here.
 * Sized for the VS1003 reset + PLL-lock window. #269: the loop count is wall-clock
 * dependent, so it was scaled 4x (200k -> 800k) when the chip moved from the
 * ring-osc 8 MHz to the 32 MHz PLL, keeping the #123-verified real-time delay. */
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

/* Last volume requested via set_vlsi_volume(). Historically a bare SCI write
 * did not stick: with XD16-31 left on their bus function, every ExtRAM (EMC)
 * write burst hardware-reset the VS1003, knocking CLOCKF/MODE/VOLUME back to
 * defaults - so vlsi_play() re-asserts the cached value right before the SDI
 * feed. #275 fixed the root cause (PORTSEL4 mux in main.c's init_hw; isolated
 * by examples/recprobe.c), so writes now hold; the play-window re-assert is
 * kept as cheap defence in depth. */
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

static void vlsi_patch(void); /* defined below */

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
  audio_delay(800000);
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
  audio_delay(800000);
  wait_dreq();
  /* SPI0 stays at ~2 MHz. The historical "8 MHz reads garbage" observation was
   * #275: EMC write bursts hardware-reset the codec, so CLKI was back at base
   * XTAL (max SCI = CLKI/7) whenever we looked. With the PORTSEL4 fix CLOCKF
   * holds and ~8 MHz should be safe (mtl runs it), but 2 MHz is plenty for
   * SCI + the SDI feed and is the speed everything here was verified at. */

  set_vlsi_volume(0x20);
  vlsi_patch();
}

/* VLSI's own microcode patch, applied unconditionally by FW1's init_vlsi()
 * (mtl/firmware/src/hal/audio.c, named patchwma there) but never ported here.
 * Ten WRAM_ADDR/WRAM writes loading two short blocks into the codec's X-RAM -
 * the standard VLSI patch-loading idiom, not a bespoke register poke. #123
 * A/B'd it by ear against the unpatched decoder for a reported MP3-quieter-
 * than-nab.beep gap: no clear difference, but it is official and harmless
 * (X-RAM only, no persistent state), so it stays in as a low-risk default -
 * matching FW1 costs nothing and rules out a config drift between the two
 * tracks as a future explanation for any audio difference. */
static void vlsi_patch(void)
{
  static const uint16_t addr_a = 0x800e, data_a[] = {0x2801, 0x3f80, 0x0006, 0x53d7};
  static const uint16_t addr_b = 0x84fe, data_b[] = {0x2000, 0x0000, 0x3f05, 0xc024};
  uint8_t i;

  vlsi_write_sci(VS1003_WRAM_ADDR, addr_a);
  for (i = 0; i < sizeof data_a / sizeof data_a[0]; i++)
    vlsi_write_sci(VS1003_WRAM, data_a[i]);

  vlsi_write_sci(VS1003_WRAM_ADDR, addr_b);
  for (i = 0; i < sizeof data_b / sizeof data_b[0]; i++)
    vlsi_write_sci(VS1003_WRAM, data_b[i]);
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
 * mid-sample. */
#define VLSI_FLUSH_BYTES 2052

/* SDI burst size: the datasheet's contract is that a high DREQ means the
 * decoder can take at least 32 more bytes, so a burst needs one DREQ test,
 * not one per byte. */
#define SDI_BURST 32

/* Blocking feeds give up after this many consecutive "DREQ never came back"
 * waits. Without a bound vlsi_play would hang on a wedged codec - and in the
 * simulator, where DREQ is unmodelled (reads 0 forever), it would never
 * return at all. */
#define VLSI_STALL_MAX 16

static uint8_t stream_open;   /* a vlsi_stream_start() session is active */

void vlsi_stream_start(void)
{
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
  stream_open = 1;
}

uint32_t vlsi_stream_feed(const uint8_t *data, uint32_t len)
{
  uint32_t sent = 0;

  if (!stream_open)
    return 0;

  CS_AUDIO_SDI_CLEAR;
  /* Push whole bursts while DREQ says there is room, and stop the moment it
   * drops - the short return is what lets the caller keep the CPU. */
  while (sent < len && (INT_AUDIO_READ & INT_AUDIO_BIT)) {
    uint32_t n = len - sent;
    if (n > SDI_BURST)
      n = SDI_BURST;
    while (n--) {
      WriteSPI(data[sent++]);
      get_value(SPDRR0);   /* drain the RX byte each write: a long stream would
                            * otherwise overflow SPI0's RX FIFO (init_spi never
                            * clears SPI0 ORF), which stalls the feed */
    }
    CLR_WDT;
  }
  CS_AUDIO_SDI_SET;
  return sent;
}

uint8_t vlsi_stream_busy(void)
{
  /* HDAT1 carries the detected stream format while the decoder has something
   * to decode and reads 0 once it has drained (in record mode it means the
   * FIFO fill instead - hence "playback only"). */
  return stream_open && vlsi_read_sci(VS1003_HDAT1) != 0;
}

void vlsi_stream_stop(void)
{
  stream_open = 0;
  vlsi_ampli(0);
}

/* Feed the whole buffer, waiting out DREQ between short feeds. Bounded: gives
 * up after VLSI_STALL_MAX fruitless waits. Returns bytes fed. */
static uint32_t vlsi_feed_all(const uint8_t *data, uint32_t len)
{
  uint32_t sent = 0, stalls = 0;

  while (sent < len && stalls < VLSI_STALL_MAX) {
    uint32_t n = vlsi_stream_feed(data + sent, len - sent);
    sent += n;
    if (n == 0) {
      wait_dreq();
      stalls++;
    } else {
      stalls = 0;
    }
  }
  return sent;
}

void vlsi_play(const uint8_t *data, uint32_t len)
{
  /* End-of-stream flush source. Zero is the correct endFillByte for PCM/WAV,
   * so feed zeros - avoids the VS1053-only WRAM endFillByte read (0x1E06) this
   * used to do, which returns garbage on the VS1003B and injected ~2 KB of
   * noise. One burst, fed repeatedly, so the zeros cost 32 bytes of flash. */
  static const uint8_t zeros[SDI_BURST] = {0};
  uint32_t i;

  vlsi_stream_start();
  vlsi_feed_all(data, len);
  for (i = 0; i < VLSI_FLUSH_BYTES; i += SDI_BURST)
    vlsi_feed_all(zeros, SDI_BURST);
  vlsi_stream_stop();
}

void vlsi_rec_start(uint16_t sample_rate, uint16_t gain)
{
  vlsi_ampli(0);   /* mic in, speaker off: avoid feedback while recording */

  /* Sample-rate divider base: CLKI/256 = 12.288 MHz x 4 / 256 = 192 kHz (same
   * CLOCKF 0xc000 as init_vlsi / src/firmware). Must be set before entering
   * ADPCM mode. */
  vlsi_write_sci(VS1003_AICTRL0, (uint16_t)(192000UL / sample_rate));
  vlsi_write_sci(VS1003_AICTRL1, gain);

  /* ADPCM record starts on a soft reset with SM_ADPCM set (datasheet; same
   * order as src/firmware's init_adpcm_encode). CLOCKF survives soft resets. */
  vlsi_write_sci(VS1003_MODE,
                 VS1003_MODE_NATIVE | VS1003_MODE_ADPCM | VS1003_MODE_RESET);
  wait_dreq();
}

uint32_t vlsi_rec_read(uint8_t *dst, uint32_t max, unsigned long wait)
{
  unsigned long guard = 0;
  uint16_t words;
  uint32_t n = 0;

  /* HDAT1 = record-FIFO fill in 16-bit words; the 0xFF80 mask (from V1's
   * rec_check) keeps whole 128-word (256-byte) ADPCM blocks only. One block
   * is ~63 ms of 8 kHz audio, a few thousand polls - callers wanting a
   * blocking read pass a `wait` comfortably above that; 0 = single check
   * (the cooperative mode). The bounded exit is also what ends the wait
   * off-hardware (simulator: HDAT1 reads 0 forever). */
  while ((words = (uint16_t)(vlsi_read_sci(VS1003_HDAT1) & 0xFF80)) == 0) {
    if (guard++ >= wait)
      return 0;
    CLR_WDT;
  }

  if ((uint32_t)words > (max >> 1))
    words = (uint16_t)((max >> 1) & 0xFF80U);

  while (words--) {
    uint16_t val = vlsi_read_sci(VS1003_HDAT0);
    dst[n++] = (uint8_t)(val >> 8);   /* MSB first (VS10xx ADPCM app-note) */
    dst[n++] = (uint8_t)val;
    CLR_WDT;
  }
  return n;
}

void vlsi_rec_stop(void)
{
  vlsi_write_sci(VS1003_MODE, VS1003_MODE_NATIVE | VS1003_MODE_RESET);
  wait_dreq();
}
