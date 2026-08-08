/**
 * @file lcread.c
 * @brief Pulling an `#LC` bytecode frame off the console (#328).
 *
 * The receiving half of the frame; `lcframe.c` is the parsing and checking
 * half. Split out of main.c for the reason fmt.c and lcframe.c were: main.c
 * carries main(), so nothing can link it and nothing in it can be unit-tested -
 * and here that was not an abstract cost. The one path this file gets wrong
 * most expensively (what a REFUSED frame leaves on the console) had a known
 * open corner that its own comment said was "written down rather than
 * half-done, because it is a loop over attacker-paced input in main.c, where
 * nothing can link it to test it". Moving the code is what closed it; the fix
 * is in lcread_frame() below and test/host/lcread_test.c pins it.
 *
 * The console seam is `_read` from libc/syscalls.h, which is what makes this
 * host-testable: test/host/lcread_test.c substitutes a fake one, exactly as
 * fmt_test.c does for `_write`.
 *
 * No Lua here on purpose - see the header. luaL_loadbuffer stays in main.c's
 * thin caller, so this TU (and its test) links neither the Lua core nor a
 * lua_State.
 */
#include <stdlib.h>

#include "libc/syscalls.h"  /* _read: the console seam, substitutable in tests */
#include "utils/lcframe.h"
#include "utils/lcread.h"

/* Read the next hex digit off the console, skipping the whitespace the sender
 * uses to wrap the payload at 64 columns. Returns 0..15, or -1 on EOF / a
 * non-hex byte - which it has CONSUMED by the time it says so: the console has
 * no pushback, so proving a byte is not payload costs that byte. */
static int read_hex_nibble(void)
{
  char c;
  for (;;) {
    if (_read(0, &c, 1) != 1)
      return -1; /* EOF */
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
      continue; /* framing whitespace */
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1; /* not hex -> malformed frame */
  }
}

/* Consume the console up to and including the next newline. After a frame's
 * 2*len hex chars there is still the payload's line terminator sitting in the
 * stream; dropping it here keeps the following line read from seeing that '\n'
 * as a spurious empty REPL line (which would double every prompt). */
static void skip_to_eol(void)
{
  char c;
  while (_read(0, &c, 1) == 1) {
    if (c == '\n')
      break;
  }
}

/* Throw away a frame's payload: 2*len hex chars plus its trailing newline.
 * Used when the length is credible but the frame is not.
 *
 * len > 0 is the caller's job to check, and it is not a formality: a sender
 * emits ceil(2*len/64) payload lines, so a frame of 0 bytes has NO payload
 * line, and calling this with 0 would skip_to_eol() straight through whatever
 * the console holds next - the user's following line. */
static void drop_payload(long len)
{
  for (long i = 0; i < 2 * len; i++) {
    if (read_hex_nibble() < 0)
      return; /* EOF or non-hex: the frame was short, nothing left to drop */
  }
  skip_to_eol();
}

/* Throw away a payload whose declared length must NOT be trusted, by draining
 * hex and framing whitespace until the first byte that is neither.
 *
 * This is the LCFRAME_ERR_TOOLONG path, and the reason it drains by content:
 * the length is over the cap by definition, so looping 2*len times would mean
 * reading megabytes on a header's say-so. Draining by content reads only what
 * the sender actually put on the wire - bytes that arrive either way, and that
 * cost one echoed error per wrapped line if they are left for the REPL. No
 * skip_to_eol() after: the terminating byte is typically the first character of
 * the NEXT line, and eating the remainder of that line would throw away a
 * perfectly good following frame. */
static void drain_hex(void)
{
  while (read_hex_nibble() >= 0)
    ;
}

lcread_status lcread_frame(const char *line, char **buf, long *len,
                           const char **msg)
{
  long n;
  uint32_t want;
  char *b;
  lcframe_status st;

  *buf = NULL;
  *len = 0;
  *msg = "ok";

  st = lcframe_parse_header(line, LCREAD_MAX, &n, &want);
  if (st != LCFRAME_OK) {
    /* n is what the sender queued behind this header; 0 means either nothing
     * to drop (a hand-typed header) or a length too big to believe, and only
     * the second of those still has a payload to get rid of. Guarding on n > 0
     * is what keeps a hand-typed "#LC:oops" from eating the line after it. */
    if (st == LCFRAME_ERR_TOOLONG)
      drain_hex();
    else if (n > 0)
      drop_payload(n);
    *msg = lcframe_strerror(st);
    return LCREAD_ERR_HEADER;
  }

  b = (n > 0) ? malloc((size_t)n) : NULL;
  if (n > 0 && b == NULL) {
    /* Out of ExtRAM heap. The payload is on the wire regardless, so it is
     * dropped here too - refusing a frame without consuming it is the #308
     * desync, and running out of memory is no exception to that rule. */
    drop_payload(n);
    *msg = "out of memory reading #LC frame";
    return LCREAD_ERR_MEM;
  }

  for (long i = 0; i < n; i++) {
    int hi = read_hex_nibble();
    /* Short-circuit: reading `lo` after `hi` already failed would consume a
     * SECOND byte of whatever follows the payload, doubling the one-byte
     * resync cost this console cannot avoid (see the header's table). */
    int lo = (hi < 0) ? -1 : read_hex_nibble();
    if (hi < 0 || lo < 0) {
      free(b);
      /* Nothing more to drop: the payload ended early, which is how we got
       * here, and read_hex_nibble already consumed the byte that proved it. */
      *msg = "truncated or non-hex #LC frame payload";
      return LCREAD_ERR_PAYLOAD;
    }
    b[i] = (char)((hi << 4) | lo);
  }
  if (n > 0)
    skip_to_eol(); /* the payload's trailing newline; a 0-byte frame has none */

  /* Verify before the loader ever sees it. */
  if (lcframe_checksum((const uint8_t *)b, (size_t)n) != want) {
    free(b);
    *msg = "#LC frame checksum mismatch - frame corrupted in transit";
    return LCREAD_ERR_CHECKSUM;
  }

  *buf = b;
  *len = n;
  return LCREAD_OK;
}
