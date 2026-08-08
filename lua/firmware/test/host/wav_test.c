/**
 * @file wav_test.c
 * @brief Host-side tests for src/utils/wav.c - the recording's RIFF header (#327).
 *
 * The header is 60 hand-written bytes, and `firmware/README.md` promises they
 * are byte-for-byte the ones `mtl/lib/hw/reclib.mtl` builds for the V1 stack,
 * "so anything that accepts a V1 recording accepts this one". Nothing checked
 * that, because the code lived in `main.c` - the one TU nothing can link.
 *
 * So the point of this file is not that `wav_adpcm_header` runs. It is that
 * every byte it writes is named by a second, independent source:
 *
 *   * `mtl` transcribes `_reclib_mkriff`'s literal and its concatenation order
 *     off the other track and compares all 60 bytes against it - the
 *     cross-track claim checked rather than restated;
 *   * `fields` recomputes the four little-endian sizes from the RIFF/fact/data
 *     definitions here in the test, so a changed constant in wav.c fails even
 *     if someone regenerates a golden from the code;
 *   * `header` pins the whole header for one known length, and `edges` pins
 *     the two lengths the arithmetic can trip over.
 *
 * ASan/UBSan are the free half: the writer is a run of raw stores into a
 * caller-supplied buffer, so every header below is built into a heap
 * allocation of EXACTLY WAV_HEADER_LEN bytes and an overrun aborts the run
 * instead of passing it.
 *
 * Scenarios (argv[1] selects one; all run by default):
 *
 *   header  the full 60-byte header for a known datalen, byte for byte
 *   fields  the four LE size fields, recomputed independently
 *   mtl     byte-for-byte against mtl/lib/hw/reclib.mtl's _reclib_mkriff
 *   edges   datalen = 0, and a length that is not a whole 256-byte block
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/wav.h"

/* --- assert harness (same shape as lcread_test.c) ------------------------- */

static int failures;

static void eq_u32(uint32_t got, uint32_t want, const char *label)
{
  if (got != want) {
    printf("  FAIL: %s: got %lu, want %lu\n", label, (unsigned long)got,
           (unsigned long)want);
    failures++;
  }
}

/* On a mismatch, say WHICH byte: "60 bytes differ" is not a useful report when
 * a byte contract is the whole subject. */
static void eq_bytes(const uint8_t *got, const uint8_t *want, size_t n,
                     const char *label)
{
  size_t i;

  for (i = 0; i < n; i++) {
    if (got[i] != want[i]) {
      printf("  FAIL: %s: byte %u is 0x%02x, want 0x%02x\n", label,
             (unsigned)i, got[i], want[i]);
      failures++;
      return;
    }
  }
}

/* Read a little-endian 32-bit field back, independently of wav.c's writer. */
static uint32_t le32(const uint8_t *p)
{
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16) |
         ((uint32_t)p[3] << 24);
}

/* Build a header into a heap buffer of exactly WAV_HEADER_LEN bytes, so ASan
 * makes "writes 60 bytes, no more" an assertion. Caller frees. */
static uint8_t *build(uint32_t datalen)
{
  uint8_t *h = malloc(WAV_HEADER_LEN);

  if (h == NULL) {
    printf("  FAIL: out of memory\n");
    exit(2);
  }
  memset(h, 0xAA, WAV_HEADER_LEN); /* so "not written" != "written zero" */
  wav_adpcm_header(h, datalen);
  return h;
}

/* --- header: the full 60 bytes for one known length ----------------------- */
/* 512 bytes = 2 blocks: the smallest length where both the byte sizes
 * (512 + 52 = 564 = 0x0234) and the sample count (2 * 505 = 1010 = 0x03F2)
 * are wide enough that a byte-order slip shows up. */
static void scen_header(void)
{
  static const uint8_t want[WAV_HEADER_LEN] = {
      'R',  'I',  'F',  'F',
      0x34, 0x02, 0x00, 0x00,   /* 564 = 512 + 52 (file size - 8) */
      'W',  'A',  'V',  'E',  'f', 'm', 't', ' ',
      0x14, 0x00, 0x00, 0x00,   /* fmt chunk length 20 */
      0x11, 0x00,               /* format 0x0011 = IMA ADPCM */
      0x01, 0x00,               /* mono */
      0x40, 0x1f, 0x00, 0x00,   /* 8000 Hz */
      0xd7, 0x0f, 0x00, 0x00,   /* 4055 bytes/s */
      0x00, 0x01,               /* block align 256 */
      0x04, 0x00,               /* 4 bits per sample */
      0x02, 0x00,               /* 2 extra fmt bytes */
      0xf9, 0x01,               /* 505 samples per block */
      'f',  'a',  'c',  't',
      0x04, 0x00, 0x00, 0x00,   /* fact chunk length 4 */
      0xf2, 0x03, 0x00, 0x00,   /* 1010 samples = 2 blocks * 505 */
      'd',  'a',  't',  'a',
      0x00, 0x02, 0x00, 0x00,   /* 512 data bytes */
  };
  uint8_t *h = build(512);

  eq_bytes(h, want, WAV_HEADER_LEN, "header: a 512-byte recording");
  free(h);
}

/* --- fields: the four sizes, recomputed from the format ------------------- */
static void scen_fields(void)
{
  static const uint32_t lens[] = {256, 512, 4096, 0x100000};
  unsigned i;

  for (i = 0; i < sizeof lens / sizeof lens[0]; i++) {
    uint32_t datalen = lens[i];
    uint8_t *h = build(datalen);
    char label[64];

    /* RIFF size counts everything after the 8-byte "RIFF"+size prefix: the 52
     * header bytes still to come, plus the data. */
    snprintf(label, sizeof label, "fields: riff size @%lu",
             (unsigned long)datalen);
    eq_u32(le32(h + 4), datalen + 52, label);

    snprintf(label, sizeof label, "fields: fact size @%lu",
             (unsigned long)datalen);
    eq_u32(le32(h + 44), 4, label);

    /* fact = total decoded samples. One 256-byte IMA block carries 505 samples
     * (the 4-byte preamble's one sample + 504 nibbles), and the codec delivers
     * whole blocks. */
    snprintf(label, sizeof label, "fields: sample count @%lu",
             (unsigned long)datalen);
    eq_u32(le32(h + 48), (datalen / 256) * 505, label);

    snprintf(label, sizeof label, "fields: data size @%lu",
             (unsigned long)datalen);
    eq_u32(le32(h + 56), datalen, label);

    /* The chunk ids are positional: a field shifted by four bytes would still
     * satisfy the arithmetic above if nothing pinned where the ids sit. */
    snprintf(label, sizeof label, "fields: chunk ids @%lu",
             (unsigned long)datalen);
    eq_bytes(h, (const uint8_t *)"RIFF", 4, label);
    eq_bytes(h + 8, (const uint8_t *)"WAVEfmt ", 8, label);
    eq_bytes(h + 40, (const uint8_t *)"fact", 4, label);
    eq_bytes(h + 52, (const uint8_t *)"data", 4, label);
    free(h);
  }
}

/* --- mtl: the cross-track contract ---------------------------------------- */
/* mtl/lib/hw/reclib.mtl wraps the same VS1003 record stream for the V1 stack:
 *
 *   fun _reclib_mkriff ldata=
 *       let liststrlen ldata 0 -> len in
 *       (strcatlist
 *           "RIFF" ::
 *           (itobin4 len+52) ::
 *           "WAVEfmt \$14\0\0\0\$11\0\1\0\$40\$1f\0\0\$d7\$0f\0\0\0\1\4\0\2\0\$f9\01" ::
 *           "fact\4\0\0\0" ::
 *           (itobin4 (len>>8)*505) ::
 *           "data" ::
 *           (itobin4 len) ::
 *           nil
 *       ) :: ldata;;
 *
 * Assembled below in those same seven pieces, in that order - the comparison
 * is against the OTHER track's layout, not a second spelling of this one.
 * `\$xx` is MTL hex, a bare `\d` decimal, and itobin4 is the little-endian
 * 32-bit store. When the two diverge this fails, and the README's "anything
 * that accepts a V1 recording accepts this one" is the claim to change. */
static void scen_mtl(void)
{
  /* The 32 bytes of the MTL fmt literal, transcribed escape by escape. */
  static const uint8_t mtl_fmt[32] = {
      'W',  'A',  'V',  'E',  'f',  'm',  't',  ' ',
      0x14, 0x00, 0x00, 0x00,   /* \$14\0\0\0 */
      0x11, 0x00, 0x01, 0x00,   /* \$11\0\1\0 */
      0x40, 0x1f, 0x00, 0x00,   /* \$40\$1f\0\0 */
      0xd7, 0x0f, 0x00, 0x00,   /* \$d7\$0f\0\0 */
      0x00, 0x01, 0x04, 0x00,   /* \0\1\4\0 */
      0x02, 0x00, 0xf9, 0x01,   /* \2\0\$f9\01 */
  };
  /* "fact\4\0\0\0" - the fact chunk id and its 4-byte length, as one literal. */
  static const uint8_t mtl_fact[8] = {'f', 'a', 'c', 't', 0x04, 0, 0, 0};
  const uint32_t len = 1024; /* 4 blocks - a recording of a couple of seconds */
  uint8_t want[WAV_HEADER_LEN];
  uint8_t *w = want;
  uint8_t *h;

  /* itobin4: little-endian 32-bit. Written out here rather than shared with
   * le32() above so the transcription stands on its own. */
#define ITOBIN4(v)                          \
  do {                                      \
    uint32_t v_ = (uint32_t)(v);            \
    *w++ = (uint8_t)v_;                     \
    *w++ = (uint8_t)(v_ >> 8);              \
    *w++ = (uint8_t)(v_ >> 16);             \
    *w++ = (uint8_t)(v_ >> 24);             \
  } while (0)

  memcpy(w, "RIFF", 4);       w += 4;
  ITOBIN4(len + 52);
  memcpy(w, mtl_fmt, 32);     w += 32;
  memcpy(w, mtl_fact, 8);     w += 8;
  ITOBIN4((len >> 8) * 505);
  memcpy(w, "data", 4);       w += 4;
  ITOBIN4(len);
#undef ITOBIN4

  /* If the transcription is not 60 bytes it is not _reclib_mkriff's header,
   * and comparing against it would prove nothing. */
  eq_u32((uint32_t)(w - want), WAV_HEADER_LEN,
         "mtl: the transcribed pieces come to 60 bytes");

  h = build(len);
  eq_bytes(h, want, WAV_HEADER_LEN, "mtl: header matches _reclib_mkriff");
  free(h);
}

/* --- edges: the two lengths the arithmetic can trip over ------------------ */
static void scen_edges(void)
{
  uint8_t *h;

  /* Empty recording. nab.record returns header-only when the codec never
   * delivers (simulator, wedged chip), so these bytes really do go out: 52
   * bytes of chunks after the RIFF prefix, no samples, no data - well-formed
   * enough that a player opens the file instead of choking on it. */
  h = build(0);
  eq_u32(le32(h + 4), 52, "edges: an empty recording announces 52 bytes");
  eq_u32(le32(h + 48), 0, "edges: an empty recording announces 0 samples");
  eq_u32(le32(h + 56), 0, "edges: an empty recording announces 0 data bytes");
  eq_bytes(h, (const uint8_t *)"RIFF", 4, "edges: an empty recording is RIFF");
  free(h);

  /* A partial trailing block cannot arrive through nab.rec_wav (the binding
   * rejects a length that is not a multiple of 256) but can through
   * nab.record, whose loop stops on whatever the codec last handed back. Byte
   * sizes stay exact; the sample count rounds DOWN to whole blocks, because
   * samples are counted as blocks * 505 and a partial block announces none.
   * Pinned so that changing it is a decision rather than an accident. */
  h = build(512 + 100);
  eq_u32(le32(h + 56), 612, "edges: a partial block, data size is exact");
  eq_u32(le32(h + 4), 664, "edges: a partial block, riff size is exact");
  eq_u32(le32(h + 48), 2 * 505,
         "edges: a partial block rounds the sample count down");
  free(h);
}

int main(int argc, char **argv)
{
  const char *only = (argc > 1) ? argv[1] : NULL;
  int ran = 0;

  if (!only || strcmp(only, "header") == 0) { scen_header(); ran++; }
  if (!only || strcmp(only, "fields") == 0) { scen_fields(); ran++; }
  if (!only || strcmp(only, "mtl") == 0)    { scen_mtl();    ran++; }
  if (!only || strcmp(only, "edges") == 0)  { scen_edges();  ran++; }

  /* A selector that matches nothing must FAIL, not report a green run having
   * tested nothing - the repo rule (see test/host/README.md). */
  if (ran == 0) {
    printf("wav_test: no scenario matches \"%s\"\n", only);
    return 2;
  }

  if (failures) {
    printf("wav_test: %d check(s) FAILED\n", failures);
    return 1;
  }
  printf("wav_test: all checks passed\n");
  return 0;
}
