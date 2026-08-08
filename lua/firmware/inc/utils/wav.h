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
 * (the blocking convenience path) needed this header in C either way, so the C
 * side could not go away.
 *
 * **That reason expired with #333**, which deleted `nab.record`: this binding is
 * now the ONLY caller of `wav_adpcm_header`, and `lib/audio/record.lua` reaches
 * it only to get its own bytes wrapped. So the 96 B and the seam name really are
 * recoverable now, and what is left holding the binding here is just the API
 * break - #327 asked for the move and the break not to be bundled, and that
 * still stands. But it is a plain product decision with nothing propping it up.
 * Reopen it on its own terms (#330's third item).
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
