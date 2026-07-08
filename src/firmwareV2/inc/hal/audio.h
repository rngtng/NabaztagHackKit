/**
 * @file audio.h
 * @brief VLSI VS1003B audio codec over SPI0 (M8, #116).
 *
 * Trimmed from src/firmware/src/hal/audio.c (Violet / RedoX GCC port) for the
 * firmwareV2 Lua runtime: chip bring-up, SCI register access, volume, amplifier,
 * and the built-in sine-test tone (the cheapest "make a sound" - no decoder feed
 * needed). Playback/record (SDI streaming, ADPCM) are future work. Audio is on
 * SPI0 (WriteSPI/ReadSPI); the LEDs are on SPI1.
 */
#ifndef _AUDIO_H_
#define _AUDIO_H_

/* VS1003 SCI registers (subset). */
#define VS1003_MODE      0x00
#define VS1003_STATUS    0x01
#define VS1003_CLOCKF    0x03
#define VS1003_WRAM      0x06  /* WRAM data window (indirect, via WRAM_ADDR) */
#define VS1003_WRAM_ADDR 0x07
#define VS1003_VOLUME    0x0B

/* SCI_MODE, as the full 16-bit value written over SCI (high byte = SM_SDINEW/
 * SM_SDISHARE native-SPI bits, low byte = SM_RESET/SM_TESTS). */
#define VS1003_MODE_NATIVE  0x0C00  /* SM_SDINEW | SM_SDISHARE */
#define VS1003_MODE_RESET   0x0004  /* SM_RESET  */
#define VS1003_MODE_TESTS   0x0020  /* SM_TESTS (enables the sine test) */

/* Reset + PLL clock, and bring up SPI0 for the codec. Call once before use. */
void init_vlsi(void);

/* SCI (command) register access over SPI0. */
uint16_t vlsi_read_sci(uint8_t reg);
void vlsi_write_sci(uint8_t reg, uint16_t val);

/* Output volume: 0x00 = loudest, 0xFE = quietest (per channel). */
void set_vlsi_volume(uint8_t volume);

/* External audio amplifier on/off. */
void vlsi_ampli(uint8_t on);

/* Built-in sine test: on!=0 enters test mode and starts a tone whose pitch is
 * set by freq_n (VS10xx sine-skip byte); on==0 stops it and leaves test mode. */
void vlsi_sine(uint8_t freq_n, uint8_t on);

/* Stream a buffer (e.g. a WAV/MP3/ADPCM-WAV file) over SDI for the decoder to
 * play - the VS1003B decodes MP3/WMA/WAV/MIDI (docs/hardware-dissection.md), so
 * unlike vlsi_sine this is real decoded audio and SCI_VOLUME actually
 * attenuates it. Blocking: waits on DREQ per byte (bounded) like vlsi_sine's
 * control feed, then flushes the decoder's tail with VS10xx endFillByte before
 * returning. Turns the amplifier on/off around playback. Issue #123 follow-up
 * to M8 (#116); unverified on hardware pending JTAG/Pi access. */
void vlsi_play(const uint8_t *data, uint32_t len);

#endif
