/**
 * @file utils/wav.h
 * @brief The IMA-ADPCM WAV wrapper `nab.record` puts around the VS1003's
 *        record stream (#327).
 *
 * 60 bytes of RIFF built by hand, and the only thing in the recording path
 * with a *contract* rather than a mechanism: `firmware/README.md` claims this
 * header is byte-for-byte the one `mtl/lib/hw/reclib.mtl`'s `_reclib_mkriff`
 * builds around the same codec stream for the V1 stack, "so anything that
 * accepts a V1 recording accepts this one". That is a cross-track promise with
 * a real consumer, made out of magic numbers (52, 505, the 8 kHz block layout)
 * that rot silently - and while it lived in `main.c` nothing could link it to
 * check. `test/host/wav_test.c` now asserts all 60 bytes, with the MTL string
 * transcribed into it so the promise is checked rather than restated.
 *
 * No Lua and no hardware here on purpose: the whole file is `uint8_t *` in,
 * bytes out, so the test needs neither a `lua_State` nor a stub.
 *
 * ## Why `nab.rec_wav` is still a C binding
 *
 * It is `string -> string` and touches no hardware, so it sits on the wrong
 * side of the seam and could be `audio.wav()` in `lib/audio/` for zero flash.
 * The extraction above was done WITHOUT that change, deliberately: `nab.record`
 * (the blocking convenience path) needs this header in C either way, so the C
 * side cannot go away - dropping the binding would buy 96 B and one fewer seam
 * name at the cost of breaking every script using it. That is a product
 * decision about the API, not a consequence of moving a file, and #327 asked
 * for the two not to be bundled. Reopen it on its own terms.
 */
#ifndef _WAV_H_
#define _WAV_H_

#include <stdint.h>

/** @brief Size of the header wav_adpcm_header() writes, in bytes. */
#define WAV_HEADER_LEN 60

/**
 * @brief Write the 60-byte IMA-ADPCM WAV header for a recording.
 *
 * 8 kHz mono, 4 bits/sample, 256-byte blocks of 505 samples (~4055 B/s) -
 * the VS1003's record format, unchanged since the V1 stack.
 *
 * @param h        OUT: at least WAV_HEADER_LEN bytes; the ADPCM data follows.
 * @param datalen  size of the ADPCM data that follows, in bytes.
 *
 * `datalen` is expected to be a whole number of 256-byte blocks - that is what
 * the codec delivers, and what `nab.rec_wav` enforces at the seam. A partial
 * block is written out faithfully in the RIFF sizes (which are byte counts)
 * but rounds DOWN in the `fact` sample count, because samples are counted as
 * `blocks * 505` and there is no per-block sample count to interpolate. The
 * file still plays; its last partial block just is not announced.
 */
void wav_adpcm_header(uint8_t *h, uint32_t datalen);

#endif /* _WAV_H_ */
