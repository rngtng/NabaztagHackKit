/**
 * @file lcframe.c
 * @brief Fletcher-32 for the `#LC` frame's integrity check.
 *
 * Its own translation unit rather than a static in main.c, for the reason
 * split out fmt.c: main.c carries main(), so nothing else can link it and
 * nothing in it can be unit-tested. This is the one piece of new logic the frame
 * check adds, and test/host/lcframe_test.c pins it.
 *
 * The senders (tools/luac/replpipe.py, tools/luac/luash.py) implement the same
 * loop. Sender/receiver agreement is what the simulator round-trips prove -
 * lua:firmware:test, :test:inject and :test:sched all pipe real frames through
 * replpipe.py into a real image.
 */
#include "utils/lcframe.h"

uint32_t lcframe_checksum(const uint8_t *data, size_t n)
{
  uint32_t a = 0xFFFF, b = 0xFFFF;

  while (n) {
    /* 359 is the longest run that cannot overflow the 32-bit accumulators
     * before the fold below (the standard Fletcher-32 blocking constant). */
    size_t blk = (n > 359) ? 359 : n;

    n -= blk;
    do {
      a += *data++;
      b += a;
    } while (--blk);
    a = (a & 0xFFFF) + (a >> 16);
    b = (b & 0xFFFF) + (b >> 16);
  }
  a = (a & 0xFFFF) + (a >> 16);
  b = (b & 0xFFFF) + (b >> 16);
  return (b << 16) | a;
}

/* One hex digit -> 0..15, or -1. No newlib: ctype/strtol pull in locale tables. */
static int hexval(char c)
{
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/* Header parsing lives here rather than in main.c for the reason the checksum
 * does: main.c carries main(), so nothing can link it and nothing in it can be
 * tested. What has to be got right is not the happy path but what `*len` says
 * when the parse FAILS - see the header comment, and lcframe_test.c's `header`
 * scenario, which is the regression guard for the console desync. */
lcframe_status lcframe_parse_header(const char *line, long max,
                                    long *len, uint32_t *sum)
{
  const char *p = line + 4;   /* past "#LC:" */
  long n = 0;
  int i;

  *len = 0;
  *sum = 0;

  if (*p < '0' || *p > '9')
    return LCFRAME_ERR_LEN;
  for (; *p >= '0' && *p <= '9'; p++) {
    n = n * 10 + (*p - '0');
    if (n > max)
      return LCFRAME_ERR_TOOLONG;   /* *len stays 0: do not drain megabytes */
  }

  /* From here the length is credible, so every failure below reports it: the
   * sender has queued 2*n hex chars behind this header whether we like the
   * rest of it or not. */
  *len = n;

  if (*p++ != ':')
    return LCFRAME_ERR_NOSUM;
  for (i = 0; i < 8; i++) {
    int v = hexval(*p++);
    if (v < 0)
      return LCFRAME_ERR_BADSUM;
    *sum = (*sum << 4) | (uint32_t)v;
  }
  return LCFRAME_OK;
}

const char *lcframe_strerror(lcframe_status st)
{
  switch (st) {
    case LCFRAME_ERR_LEN:     return "malformed #LC frame header";
    case LCFRAME_ERR_TOOLONG: return "#LC frame too large";
    case LCFRAME_ERR_NOSUM:   return "#LC frame header carries no checksum";
    case LCFRAME_ERR_BADSUM:  return "malformed #LC frame checksum";
    default:                  return "ok";
  }
}
