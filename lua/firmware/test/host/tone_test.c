/**
 * @file tone_test.c
 * @brief Host-side tests for inc/tone_midi.h - nab.tone()'s resident asset (#331).
 *
 * The asset is generated (tools/tonegen.py) and committed, and it is the one
 * audio byte string the image carries: it is what `nab.tone()` hands to
 * `nab.play` at a bare REPL to answer "is the codec alive at all". A generated
 * blob nobody reads is exactly the thing that rots - the old MP3 sat in the
 * tree for a year at 2,160 B - so this file makes the header answer to two
 * independent sources rather than to itself:
 *
 *   * `smf`  rebuilds all 45 bytes here from the Standard MIDI File spec -
 *            MThd/MTrk chunk layout, variable-length delta times, the running
 *            event bytes - so a regenerated header that is no longer a valid
 *            Format-0 file fails even though tonegen.py produced it;
 *   * `lua`  pins the four constants the file shares with ../lib/audio/midi.lua
 *            (DIVISION, TEMPO, PROGRAM, VELOCITY) and the note/duration, which
 *            is the whole cross-seam claim the header's comment makes: the
 *            resident tone IS `audio.midi.note("A5", 250)`. The other half of
 *            that claim lives in ../../../lib/audio/test/test_midi.lua, which
 *            asserts the Lua builder still emits these same bytes;
 *   * `size` pins the budget the swap was made for. 2,160 B of MP3 was 28% of
 *            the image's free flash; a header that quietly grows back past a
 *            couple of hundred bytes has lost the point of #331.
 *
 * No hardware and no Lua: the subject is a byte array in a header, so the test
 * links nothing but itself. What it CANNOT check is that a VS1003B makes a
 * sound when fed these bytes - that is the rig gate #331 names, and it stays a
 * claim made by hand in firmware/README.md's hardware table.
 *
 * Scenarios (argv[1] selects one; all run by default):
 *
 *   smf   all 45 bytes, rebuilt from the SMF spec
 *   lua   the constants shared with lib/audio/midi.lua, read back out of them
 *   size  the flash budget the codec swap bought
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "tone_midi.h"

/* --- assert harness (same shape as wav_test.c) ---------------------------- */

static int failures;

static void eq_u32(uint32_t got, uint32_t want, const char *label)
{
  if (got != want) {
    printf("  FAIL: %s: got %lu, want %lu\n", label, (unsigned long)got,
           (unsigned long)want);
    failures++;
  }
}

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

/* The constants tonegen.py and lib/audio/midi.lua both carry. Spelled out
 * again here so that changing one of them on either side of the seam has to
 * be a decision taken in three places, not a silent drift in one. */
#define DIVISION 480      /* ticks per quarter note                          */
#define TEMPO    500000   /* microseconds per quarter note (= 120 bpm)       */
#define PROGRAM  9        /* GM 10, glockenspiel - the V1 jingle voice       */
#define VELOCITY 100
#define NOTE     81       /* A5 = 880 Hz, the pitch the MP3 predecessor sang */
#define TONE_MS  250

/* ms -> ticks at the tempo actually written into the file (midi.lua's ticks) */
#define TICKS(ms) ((ms) * DIVISION / (TEMPO / 1000))

/* --- smf: the whole file, rebuilt from the spec --------------------------- */
static void scen_smf(void)
{
  /* An SMF is <chunk id><32-bit big-endian length><body>, twice: a 6-byte
   * MThd (format, track count, division) and one MTrk of delta-timed events.
   * Delta times are variable-length quantities - 7 bits per byte, high bit
   * set on every byte but the last - so 240 ticks is 0x81 0x70, and that
   * encoding is the one thing a hand-written blob most easily gets wrong. */
  static const uint8_t want[] = {
      'M', 'T', 'h', 'd',
      0x00, 0x00, 0x00, 0x06,           /* MThd body is always 6 bytes       */
      0x00, 0x00,                       /* format 0: one multi-channel track */
      0x00, 0x01,                       /* one track                         */
      DIVISION >> 8, DIVISION & 0xFF,   /* 480 ticks per quarter note        */

      'M', 'T', 'r', 'k',
      0x00, 0x00, 0x00, 23,             /* 7 + 3 + 4 + 5 + 4 event bytes     */

      0x00, 0xFF, 0x51, 0x03,           /* delta 0, meta: set tempo, 3 bytes */
      (TEMPO >> 16) & 0xFF, (TEMPO >> 8) & 0xFF, TEMPO & 0xFF,

      0x00, 0xC0, PROGRAM,              /* delta 0, program change, channel 0 */

      0x00, 0x90, NOTE, VELOCITY,       /* delta 0, note on, channel 0        */

      0x81, 0x70,                       /* delta 240 ticks = 250 ms          */
      0x80, NOTE, 0x40,                 /* note off, release velocity 64     */

      0x00, 0xFF, 0x2F, 0x00,           /* delta 0, meta: end of track       */
  };

  /* If the rebuild is not the same length as the asset, comparing the common
   * prefix would pass on a truncated file. */
  eq_u32((uint32_t)sizeof nab_tone_midi, (uint32_t)sizeof want,
         "smf: the asset is as long as the file the spec describes");
  if (sizeof nab_tone_midi != sizeof want)
    return;

  eq_bytes(nab_tone_midi, want, sizeof want, "smf: the asset is that file");

  /* Read the two chunk lengths back out of the bytes, independently of the
   * table above: a length field that disagrees with the bytes that follow it
   * is the one corruption a byte-for-byte compare against a stale golden
   * would happily reproduce. */
  eq_u32(((uint32_t)nab_tone_midi[4] << 24) | ((uint32_t)nab_tone_midi[5] << 16)
         | ((uint32_t)nab_tone_midi[6] << 8) | nab_tone_midi[7], 6,
         "smf: MThd announces 6 body bytes");
  eq_u32(((uint32_t)nab_tone_midi[18] << 24) | ((uint32_t)nab_tone_midi[19] << 16)
         | ((uint32_t)nab_tone_midi[20] << 8) | nab_tone_midi[21],
         (uint32_t)(sizeof nab_tone_midi - 22),
         "smf: MTrk's length is the rest of the file");
}

/* --- lua: the constants shared with lib/audio/midi.lua -------------------- */
static void scen_lua(void)
{
  uint32_t ticks;

  /* Each of these is read out of the asset at its spec-defined offset and
   * compared to the constant, so the check is "the file says what midi.lua
   * would have said", not "the table above matches the table above". */
  eq_u32(((uint32_t)nab_tone_midi[12] << 8) | nab_tone_midi[13], DIVISION,
         "lua: division matches midi.DIVISION");
  eq_u32(((uint32_t)nab_tone_midi[26] << 16) | ((uint32_t)nab_tone_midi[27] << 8)
         | nab_tone_midi[28], TEMPO, "lua: tempo matches midi.TEMPO");
  eq_u32(nab_tone_midi[31], PROGRAM, "lua: program matches midi.PROGRAM");
  eq_u32(nab_tone_midi[35], VELOCITY, "lua: velocity matches midi.VELOCITY");
  eq_u32(nab_tone_midi[34], NOTE, "lua: the note is A5 (880 Hz)");
  eq_u32(nab_tone_midi[39], NOTE, "lua: the note off releases the same note");

  /* The note-off delta, decoded as a variable-length quantity, is the tone's
   * length - the one field that carries TONE_MS. 250 ms at 120 bpm with 480
   * ticks to the quarter note is 240 ticks; getting the VLQ or the tempo
   * arithmetic wrong makes the tone the wrong length, which on a 0.25 s smoke
   * test is the difference between a chime and a click. */
  ticks = ((uint32_t)(nab_tone_midi[36] & 0x7F) << 7) | nab_tone_midi[37];
  eq_u32((uint32_t)(nab_tone_midi[36] & 0x80), 0x80,
         "lua: the delta's first byte continues");
  eq_u32((uint32_t)(nab_tone_midi[37] & 0x80), 0,
         "lua: the delta's second byte ends it");
  eq_u32(ticks, TICKS(TONE_MS), "lua: the note lasts 250 ms worth of ticks");
}

/* --- size: the budget the swap was made for ------------------------------- */
static void scen_size(void)
{
  /* #331's definition of done: "the asset is <= ~200 B". The MP3 it replaced
   * was 2,160 B - 28% of the image's free flash at the time. This bound is
   * what stops the asset drifting back into being a sample library; a jingle
   * that needs more than this belongs in lib/audio/midi.lua, where it costs
   * no flash at all. */
  if (sizeof nab_tone_midi > 200U) {
    printf("  FAIL: size: the asset is %u B, over the 200 B #331 budget\n",
           (unsigned)sizeof nab_tone_midi);
    failures++;
  }
  /* ...and a lower bound, because "0 bytes" would satisfy every check above
   * that guards against growth. A Format-0 file with one note cannot be
   * shorter than its two chunk headers plus the five events. */
  eq_u32((uint32_t)sizeof nab_tone_midi, 45,
         "size: one note is 45 bytes (14 MThd + 8 MTrk + 23 events)");
}

int main(int argc, char **argv)
{
  const char *only = (argc > 1) ? argv[1] : NULL;
  int ran = 0;

  if (!only || strcmp(only, "smf") == 0)  { scen_smf();  ran++; }
  if (!only || strcmp(only, "lua") == 0)  { scen_lua();  ran++; }
  if (!only || strcmp(only, "size") == 0) { scen_size(); ran++; }

  /* A selector that matches nothing must FAIL, not report a green run having
   * tested nothing - the repo rule (see test/host/README.md). */
  if (ran == 0) {
    printf("tone_test: no scenario matches \"%s\"\n", only);
    return 2;
  }

  if (failures) {
    printf("tone_test: %d check(s) FAILED\n", failures);
    return 1;
  }
  printf("tone_test: all checks passed\n");
  return 0;
}
