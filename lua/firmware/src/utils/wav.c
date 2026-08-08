/**
 * @file wav.c
 * @brief The RIFF/IMA-ADPCM header nab.record wraps its recordings in (#327).
 *
 * Split out of main.c for the reason fmt.c, lcframe.c and lcread.c were:
 * main.c carries main(), so nothing can link it and nothing in it can be
 * unit-tested. Here that cost was specific - the header is a documented
 * cross-track byte contract (see utils/wav.h) built out of hand-written
 * constants, which is exactly the kind of thing that stays "true" in a README
 * long after it stopped being true in the bytes.
 *
 * Pure byte formatting: no Lua, no hardware, no allocation. The binding that
 * used to hold it (nab.rec_wav / nab.record) stays in main.c, where the
 * luaL_Buffer handling belongs.
 */
#include <string.h>

#include "utils/wav.h"

/* Little-endian 32-bit store. RIFF is little-endian everywhere and so is this
 * target, but the field writes below say so explicitly rather than aliasing a
 * uint32_t through a possibly-unaligned uint8_t* - which -Wcast-align would
 * refuse and UBSan would flag in the host test. */
static void wav_le32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

void wav_adpcm_header(uint8_t *h, uint32_t datalen)
{
  static const uint8_t fmt[32] = {
      'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
      20, 0, 0, 0,        /* fmt chunk length */
      0x11, 0,            /* format 0x0011 = IMA ADPCM */
      1, 0,               /* mono */
      0x40, 0x1F, 0, 0,   /* 8000 Hz */
      0xD7, 0x0F, 0, 0,   /* 4055 bytes/s */
      0, 1,               /* block align 256 */
      4, 0,               /* 4 bits per sample */
      2, 0,               /* extra fmt bytes */
      0xF9, 1,            /* 505 samples per block */
  };
  memcpy(h, "RIFF", 4);
  wav_le32(h + 4, datalen + 52);         /* file size - 8 */
  memcpy(h + 8, fmt, sizeof fmt);
  memcpy(h + 40, "fact", 4);
  wav_le32(h + 44, 4);
  wav_le32(h + 48, (datalen >> 8) * 505);   /* total samples: blocks * 505 */
  memcpy(h + 52, "data", 4);
  wav_le32(h + 56, datalen);
}
