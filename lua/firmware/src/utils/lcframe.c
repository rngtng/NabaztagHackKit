/**
 * @file lcframe.c
 * @brief Fletcher-32 for the `#LC` frame's integrity check (#298).
 *
 * Its own translation unit rather than a static in main.c, for the reason #245
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
