/**
 * @file lcframe_test.c
 * @brief Host-side tests for the `#LC` frame checksum (#298).
 *
 * `lcframe_checksum` is the one piece of new logic the frame's integrity check
 * adds, and it lives in its own TU precisely so it can be linked here - the
 * reason #245 split fmt.c out of main.c. What this file pins:
 *
 *   * the exact value for fixed inputs, so the C receiver and the two Python
 *     senders (tools/luac/replpipe.py, luash.py) cannot drift apart silently;
 *   * that a single flipped NIBBLE changes it - the actual failure mode on this
 *     console, where a hex payload rides a UART with no flow control;
 *   * that transposed bytes change it, which a plain sum would miss.
 *
 * Sender/receiver agreement itself is proved end to end by the simulator
 * round-trips: lua:firmware:test, :test:inject and :test:sched all pipe real
 * frames from replpipe.py into a real image, which only loads them if the
 * checksums match.
 *
 * Scenarios (argv[1] selects one; all run by default):
 *
 *   vectors   fixed inputs keep their values
 *   damage    every single-nibble flip in a chunk-sized buffer is detected
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "utils/lcframe.h"

static int failures;

static void eq_u32(uint32_t got, uint32_t want, const char *label)
{
  if (got != want) {
    printf("  FAIL: %s: got 0x%08x, want 0x%08x\n", label,
           (unsigned)got, (unsigned)want);
    failures++;
  }
}

#define CHECK(cond, msg)                                                      \
  do {                                                                        \
    if (!(cond)) {                                                            \
      printf("  FAIL: %s\n", (msg));                                          \
      failures++;                                                             \
    }                                                                         \
  } while (0)

/* Values produced by this implementation and cross-checked against the Python
 * senders' fletcher32(). Pinned so a change to either side has to be
 * deliberate - they are a wire format now, shared by three implementations. */
static void scen_vectors(void)
{
  static const uint8_t abcde[] = {'a', 'b', 'c', 'd', 'e'};
  static const uint8_t sig[] = {0x1B, 'L', 'u', 'a'};   /* LUA_SIGNATURE */

  printf("scenario vectors: fixed inputs keep their checksums\n");

  eq_u32(lcframe_checksum(NULL, 0), 0xFFFFFFFFu, "empty input");
  eq_u32(lcframe_checksum(abcde, sizeof abcde), 0x05C301EFu, "\"abcde\"");
  eq_u32(lcframe_checksum(sig, sizeof sig), 0x029B013Du, "a bytecode signature");
}

/* The failure this exists for: a hex payload crossing a UART with a 16-byte RX
 * FIFO and no flow control. One wrong nibble is one wrong byte in the decoded
 * chunk, and 13% of those kill the loader (lua:firmware:test:bytecode). */
static void scen_damage(void)
{
  uint8_t buf[174];   /* the size the bytecode-robustness subject chunk is */
  uint32_t good;
  size_t i;
  int nibble;
  int missed = 0, tried = 0;

  printf("scenario damage: every single-nibble flip must change the checksum\n");

  for (i = 0; i < sizeof buf; i++)
    buf[i] = (uint8_t)(i * 7 + 3);
  good = lcframe_checksum(buf, sizeof buf);

  for (i = 0; i < sizeof buf; i++) {
    uint8_t orig = buf[i];

    for (nibble = 0; nibble < 32; nibble++) {
      uint8_t alt = (uint8_t)((nibble < 16)
                              ? ((orig & 0x0F) | (uint8_t)(nibble << 4))
                              : ((orig & 0xF0) | (uint8_t)(nibble - 16)));
      if (alt == orig)
        continue;
      tried++;
      buf[i] = alt;
      if (lcframe_checksum(buf, sizeof buf) == good)
        missed++;
      buf[i] = orig;
    }
  }

  CHECK(tried > 5000, "the sweep really covered every nibble of every byte");
  if (missed != 0)
    printf("  %d of %d single-nibble flips went undetected\n", missed, tried);
  CHECK(missed == 0, "no single-nibble flip may collide with the good checksum");

  /* A plain byte sum would miss this; Fletcher's positional term does not. */
  {
    uint8_t a[4] = {1, 2, 3, 4};
    uint8_t b[4] = {1, 3, 2, 4};

    CHECK(lcframe_checksum(a, 4) != lcframe_checksum(b, 4),
          "transposed bytes must change the checksum");
  }
}

int main(int argc, char **argv)
{
  const char *only = (argc > 1) ? argv[1] : NULL;

  if (!only || strcmp(only, "vectors") == 0) scen_vectors();
  if (!only || strcmp(only, "damage") == 0)  scen_damage();

  if (failures) {
    printf("lcframe_test: %d check(s) FAILED\n", failures);
    return 1;
  }
  printf("lcframe_test: all checks passed\n");
  return 0;
}
