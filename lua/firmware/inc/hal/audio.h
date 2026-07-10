/**
 * @file audio.h
 * @brief VLSI VS1003B audio codec over SPI0.
 *
 * Trimmed from src/firmware/src/hal/audio.c (Violet / RedoX GCC port) for the
 * firmwareV2 Lua runtime: chip bring-up, SCI register access, volume, amplifier,
 * the built-in sine-test tone (the cheapest "make a sound" - no decoder feed
 * needed), SDI playback (#123), and IMA-ADPCM microphone record (#116 mic
 * half). Audio is on SPI0 (WriteSPI/ReadSPI); the LEDs are on SPI1.
 */
#ifndef _AUDIO_H_
#define _AUDIO_H_

/* VS1003 SCI registers (subset). */
#define VS1003_MODE      0x00
#define VS1003_STATUS    0x01
#define VS1003_CLOCKF    0x03
#define VS1003_WRAM      0x06  /* WRAM data window (indirect, via WRAM_ADDR) */
#define VS1003_WRAM_ADDR 0x07
#define VS1003_HDAT0     0x08  /* decode data / bitrate; record: FIFO data word */
#define VS1003_HDAT1     0x09  /* detected stream format (0 = nothing decoding);
                                * record: FIFO fill level in 16-bit words */
#define VS1003_VOLUME    0x0B
#define VS1003_AICTRL0   0x0C  /* record: sample-rate divider (CLKI/256 / rate) */
#define VS1003_AICTRL1   0x0D  /* record: gain, 1024 = 1x; 0 = automatic (AGC) */

/* SCI_MODE, as the full 16-bit value written over SCI (high byte = SM_SDINEW/
 * SM_SDISHARE native-SPI bits, low byte = SM_RESET/SM_TESTS). */
#define VS1003_MODE_NATIVE  0x0C00  /* SM_SDINEW | SM_SDISHARE */
#define VS1003_MODE_RESET   0x0004  /* SM_RESET  */
#define VS1003_MODE_TESTS   0x0020  /* SM_TESTS (enables the sine test) */
#define VS1003_MODE_ADPCM   0x3000  /* SM_ADPCM | SM_ADPCM_HP (mic record mode) */

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
 * play - the VS1003B decodes MP3/WMA/WAV/MIDI, so unlike vlsi_sine this is real
 * decoded audio and SCI_VOLUME actually attenuates it. Blocking: soft-resets
 * the decoder, waits on DREQ per byte (bounded) like vlsi_sine's control feed,
 * then flushes the decoder's tail with zero endFillBytes before returning.
 * Turns the amplifier on/off around playback. */
void vlsi_play(const uint8_t *data, uint32_t len);

/* Microphone record, IMA ADPCM (#116 mic half; port of src/firmware's
 * init_adpcm_encode/rec_check/stop_adpcm_encode). The VS1003 encodes the mic
 * input into 256-byte IMA-ADPCM blocks (505 samples each) that we drain over
 * SCI - amplifier is turned off for the duration (feedback).
 *
 * rec_start: enter ADPCM record mode (soft reset with SM_ADPCM set, per the
 * datasheet). sample_rate in Hz (the V1 stack records at 8000); gain: 1024 =
 * 1x, 512 = 0.5x, ..., 0 = automatic gain control (what V1 uses). */
void vlsi_rec_start(uint16_t sample_rate, uint16_t gain);

/* Read recorded data into dst (at most max bytes): polls until the codec has
 * at least one full 256-byte block buffered (at most `wait` extra polls; 0 =
 * check once and return - the cooperative/non-blocking mode), then drains all
 * full blocks that fit. Returns bytes written; 0 = nothing buffered within
 * the bound (not yet a block's worth of audio, a wedged chip, or the
 * simulator where HDAT1 always reads 0). Words arrive big-endian (MSB first),
 * the layout the VS10xx ADPCM app-note specifies and the V1 RIFF wrapper
 * uploads. */
uint32_t vlsi_rec_read(uint8_t *dst, uint32_t max, unsigned long wait);

/* Leave record mode (soft reset back to decode mode; playback works again). */
void vlsi_rec_stop(void);

#endif
