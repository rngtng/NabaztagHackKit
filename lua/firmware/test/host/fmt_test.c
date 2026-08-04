/**
 * @file fmt_test.c
 * @brief Host-side unit tests for src/utils/fmt.c (#245, #254).
 *
 * fmt.c is ~250 lines of hand-rolled printf and number conversion that every
 * print(), string.format() and error message on the device goes through, and
 * until #245 split it out of main.c nothing could link it. These are its first
 * tests.
 *
 * The console sink is ours here: fmt.c calls _write(), which lives in main.c
 * on the device and is captured into a buffer below, so luai_writestring /
 * luai_writestringerror can be asserted byte for byte.
 *
 * Scenarios (argv[1] selects one; all run by default):
 *
 *   writestringerror  #245 - the over-long message must not emit a stray NUL
 *   printf            the conversions Lua actually emits
 *   num               float <-> string, pow and fmod
 *   num-large         a big float must stay inside luai_num2str's digit buffer
 *   parity            #306 - both float paths, swept against glibc's printf
 *   hex8              #254 - UID formatting, especially zero nibbles
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "lua.h"

#include "utils/fmt.h"

/* --- captured console ----------------------------------------------------- */

static char   cap[4096];
static size_t cap_len;

int _write(int fd, const char *ptr, int len)
{
  (void)fd;
  for (int i = 0; i < len && cap_len < sizeof cap; i++)
    cap[cap_len++] = ptr[i];
  return len;
}

static void cap_reset(void)
{
  cap_len = 0;
  memset(cap, 0, sizeof cap);
}

/* --- assert harness ------------------------------------------------------- */

static int failures;

#define CHECK(cond, msg)                                                      \
  do {                                                                        \
    if (!(cond)) {                                                            \
      printf("  FAIL: %s\n", (msg));                                          \
      failures++;                                                             \
    }                                                                         \
  } while (0)

/* Compare a produced string against the expected one, reporting both. */
static void eq_str(const char *got, const char *want, const char *label)
{
  if (strcmp(got, want) != 0) {
    printf("  FAIL: %s: got \"%s\", want \"%s\"\n", label, got, want);
    failures++;
  }
}

static void eq_int(long got, long want, const char *label)
{
  if (got != want) {
    printf("  FAIL: %s: got %ld, want %ld\n", label, got, want);
    failures++;
  }
}

/* ---------------------------------------------------------------------------
 * #245: luai_writestringerror clamped the write length to sizeof b instead of
 * sizeof b - 1. snprintf returns the length the output WOULD have had and
 * writes at most 127 chars + NUL into char b[128], so an over-long message
 * emitted 127 characters PLUS the terminator - a NUL byte into the console
 * stream that flash.py / uart_repl.py then read as content.
 * ------------------------------------------------------------------------- */
static void scen_writestringerror(void)
{
  char longmsg[200];

  printf("scenario writestringerror: an over-long message must not emit a NUL\n");

  memset(longmsg, 'A', sizeof longmsg - 1);
  longmsg[sizeof longmsg - 1] = '\0';

  cap_reset();
  luai_writestringerror("%s", longmsg);

  eq_int((long)cap_len, 127, "over-long message writes exactly 127 bytes");
  CHECK(memchr(cap, '\0', cap_len) == NULL,
        "over-long message must not put a NUL on the console");
  CHECK(cap_len > 0 && cap[cap_len - 1] == 'A',
        "the last byte written should be message content");

  /* A message that fits is unaffected by the clamp. */
  cap_reset();
  luai_writestringerror("bad thing: %s\n", "oops");
  cap[cap_len] = '\0';
  eq_str(cap, "bad thing: oops\n", "short message passes through verbatim");

  /* Exactly-at-the-boundary: 127 chars of content fits, 128 truncates by one. */
  cap_reset();
  longmsg[127] = '\0';
  luai_writestringerror("%s", longmsg);
  eq_int((long)cap_len, 127, "a 127-char message is written whole");

  cap_reset();
  longmsg[128] = '\0';
  luai_writestringerror("%s", longmsg);
  eq_int((long)cap_len, 127, "a 128-char message is truncated to 127");
  CHECK(memchr(cap, '\0', cap_len) == NULL, "128-char case emits no NUL");

  /* luai_writestring is the plain print() path: exact bytes, embedded NUL and
   * all (Lua strings may contain them). */
  cap_reset();
  luai_writestring("ab\0cd", 5);
  eq_int((long)cap_len, 5, "luai_writestring writes the given length");
  CHECK(memcmp(cap, "ab\0cd", 5) == 0, "luai_writestring is byte-exact");
}

/* ---------------------------------------------------------------------------
 * The conversions Lua actually emits through our snprintf. No bug known here -
 * this is the coverage that was impossible before the #245 split, and the
 * safety net for anyone touching the formatter.
 * ------------------------------------------------------------------------- */
static void scen_printf(void)
{
  char b[64];

  printf("scenario printf: the conversions Lua emits\n");

  snprintf(b, sizeof b, "%d", 0);            eq_str(b, "0", "%d zero");
  snprintf(b, sizeof b, "%d", -1);           eq_str(b, "-1", "%d negative");
  snprintf(b, sizeof b, "%d", 2147483647);   eq_str(b, "2147483647", "%d int max");
  /* INT_MIN: negating it is UB, hence the -(v+1)+1 dance in pf_utoa's caller */
  snprintf(b, sizeof b, "%d", -2147483647 - 1);
  eq_str(b, "-2147483648", "%d int min");
  snprintf(b, sizeof b, "%5d", 42);          eq_str(b, "   42", "%d width");
  snprintf(b, sizeof b, "%-5d|", 42);        eq_str(b, "42   |", "%d left align");
  snprintf(b, sizeof b, "%05d", 42);         eq_str(b, "00042", "%d zero pad");
  snprintf(b, sizeof b, "%+d", 42);          eq_str(b, "+42", "%d plus flag");
  snprintf(b, sizeof b, "%.5d", 42);         eq_str(b, "00042", "%d precision");
  snprintf(b, sizeof b, "%x", 0xdeadbeef);   eq_str(b, "deadbeef", "%x");
  snprintf(b, sizeof b, "%X", 0xdeadbeef);   eq_str(b, "DEADBEEF", "%X");
  snprintf(b, sizeof b, "%#x", 0xff);        eq_str(b, "0xff", "%#x prefix");
  snprintf(b, sizeof b, "%#x", 0);           eq_str(b, "0", "%#x no prefix on 0");
  snprintf(b, sizeof b, "%o", 8);            eq_str(b, "10", "%o");
  snprintf(b, sizeof b, "%u", 4294967295u);  eq_str(b, "4294967295", "%u max");
  snprintf(b, sizeof b, "%c", 'z');          eq_str(b, "z", "%c");
  snprintf(b, sizeof b, "%s", "hi");         eq_str(b, "hi", "%s");
  snprintf(b, sizeof b, "%.2s", "hello");    eq_str(b, "he", "%s precision");
  snprintf(b, sizeof b, "%6s|", "hi");       eq_str(b, "    hi|", "%s width");
  snprintf(b, sizeof b, "%s", (char *)NULL); eq_str(b, "(null)", "%s NULL");
  snprintf(b, sizeof b, "%*d", 4, 7);        eq_str(b, "   7", "%d star width");
  snprintf(b, sizeof b, "100%%");            eq_str(b, "100%", "%% literal");
  snprintf(b, sizeof b, "%ld", 123456789L);  eq_str(b, "123456789", "%ld");

  /* snprintf contract: truncate to size, always NUL-terminate, return the
   * length the output WOULD have had. This is what #245's clamp got wrong. */
  eq_int(snprintf(b, 4, "abcdef"), 6, "snprintf returns untruncated length");
  eq_str(b, "abc", "snprintf truncates to size-1 and terminates");
  eq_int(snprintf(NULL, 0, "abcdef"), 6, "snprintf(NULL,0) measures");

  /* An unknown conversion is emitted literally rather than eating the arg. */
  snprintf(b, sizeof b, "a%qb");             eq_str(b, "a%qb", "unknown conv literal");
}

/* ---------------------------------------------------------------------------
 * Number conversion: the float shims Lua's tostring / tonumber route through.
 * Float printing is deliberately approximate (integer part + ".0", a real dtoa
 * is future work, #213) - these pin the CURRENT contract so a future dtoa is a
 * deliberate change rather than an accident.
 * ------------------------------------------------------------------------- */
static void scen_num(void)
{
  char b[32];
  char *endp;

  printf("scenario num: float<->string, pow, fmod\n");

  luai_num2str(b, sizeof b, 0.0f);     eq_str(b, "0.0", "num2str zero");
  luai_num2str(b, sizeof b, 42.0f);    eq_str(b, "42.0", "num2str integral");
  luai_num2str(b, sizeof b, -42.0f);   eq_str(b, "-42.0", "num2str negative");
  luai_num2str(b, sizeof b, 0.5f);     eq_str(b, "0.0", "num2str |x|<1 truncates");
  luai_num2str(b, sizeof b, -0.5f);    eq_str(b, "-0.0", "num2str keeps the sign");
  eq_int(luai_num2str(b, sizeof b, 7.0f), 3, "num2str returns the length");

  eq_int((long)luai_str2number("42", &endp), 42, "str2number integral");
  eq_int((long)luai_str2number("-42", &endp), -42, "str2number negative");
  eq_int((long)luai_str2number("  3e2", &endp), 300, "str2number exponent");
  eq_int((long)(luai_str2number("0.5", &endp) * 10), 5, "str2number fraction");
  /* No digits: endptr must be left at the start so Lua rejects the numeral. */
  luai_str2number("abc", &endp);
  CHECK(endp != NULL && *endp == 'a', "str2number rejects a non-number");
  /* 'e' with no digits is not part of the numeral. */
  luai_str2number("5e", &endp);
  CHECK(endp != NULL && *endp == 'e', "str2number leaves a bare exponent");

  eq_int((long)luai_pow(2.0f, 10.0f), 1024, "pow integral exponent");
  eq_int((long)(luai_pow(2.0f, -1.0f) * 4), 2, "pow negative exponent");
  eq_int((long)luai_pow(5.0f, 0.0f), 1, "pow zero exponent");
  CHECK(luai_pow(2.0f, 0.5f) != luai_pow(2.0f, 0.5f), "pow fractional is NaN");

  /* Lua's % is floor-mod: the result takes the sign of the divisor. */
  eq_int((long)luai_fmod(7.0f, 3.0f), 1, "fmod positive");
  eq_int((long)luai_fmod(-7.0f, 3.0f), 2, "fmod negative dividend floors");
  eq_int((long)luai_fmod(7.0f, -3.0f), -2, "fmod negative divisor floors");
  CHECK(luai_fmod(1.0f, 0.0f) != luai_fmod(1.0f, 0.0f), "fmod by zero is NaN");
}

/* ---------------------------------------------------------------------------
 * Large floats: luai_num2str writes its digits into a fixed char body[16] whose
 * size assumes the widest thing pf_utoa can produce is a 32-bit `unsigned long`
 * (10 digits + sign + ".0" = 13). Nothing in the function enforces that, and
 * `uv` is a plain `unsigned long`:
 *
 *     uv = (fexp - 23 < 32) ? (unsigned long)mant << (fexp - 23) : 0;
 *
 * so the width of the output is the width of the target's `unsigned long`. Two
 * manifestations of the one defect:
 *
 *   * On any 64-bit host - i.e. right here, where this file is the only thing
 *     that ever executes fmt.c natively - the shift produces up to 2^55, which
 *     is 17 digits, and the writes at fmt.c's `body[blen++] = '.'` run past the
 *     end of body[]. ASan reports a stack-buffer-overflow.
 *   * On the device (`unsigned long` is 32 bits) the shift wraps instead, so
 *     the buffer holds but the number is silently wrong: print(1e10) renders
 *     "1410065408.0" - 1e10 mod 2^32 - not an approximation of 1e10. "Float
 *     printing is approximate" (#213) is the documented contract for the
 *     FRACTION; dropping the high bits of the integer part is not that.
 *
 * The assertions below pin the values that are exactly representable and fit,
 * then require the output to stay inside the buffer for one that does not. A
 * fix has to bound the digit count and the buffer together (and, if the
 * integer part cannot be represented at all, say so rather than print a
 * wrapped one) - note that widening pf_utoa to 64 bits is not free on ARM7:
 * it links libgcc's __aeabi_uldivmod for the /= and %= in the digit loop.
 * ------------------------------------------------------------------------- */
static void scen_num_large(void)
{
  char b[64];
  int n;

  printf("scenario num-large: a big float must stay inside the digit buffer\n");

  /* 2^24 - the first float above the contiguous-integer range, and the first
   * value to take the `fexp > 23` shift branch at all. */
  luai_num2str(b, sizeof b, 16777216.0f);
  eq_str(b, "16777216.0", "num2str 2^24");

  /* The largest float below 2^32: the widest integer part the device's
   * `unsigned long` can still hold, so this must be exact on both widths. */
  luai_num2str(b, sizeof b, 4294967040.0f);
  eq_str(b, "4294967040.0", "num2str largest value that fits unsigned long");

  /* Just over that: exactly representable as a float (10^10 = 2^10 * 5^10, and
   * 5^10 < 2^24), so the correct rendering is exact digits. This is the value
   * that comes out as "1410065408.0" on the 32-bit target. */
  luai_num2str(b, sizeof b, 1.0e10f);
  eq_str(b, "10000000000.0", "num2str 1e10 keeps its high digits");

  /* Wide enough that the digits cannot fit body[16] on a 64-bit host. Whatever
   * the shim decides to print, it must print it inside its own buffer and
   * report the length it wrote. */
  memset(b, 0, sizeof b);
  n = luai_num2str(b, sizeof b, 1.0e15f);
  eq_int(n, (long)strlen(b), "num2str returns the length it actually wrote");
  CHECK(n > 0 && n < (int)sizeof b, "num2str output stays bounded for a huge float");

  /* The widest thing this shim can be asked to print: FLT_MAX is 39 digits,
   * which is what NUM2STR_DIGITS is sized for. */
  luai_num2str(b, sizeof b, 3.40282347e38f);
  eq_str(b, "340282346638528859811704183484516925440.0", "num2str FLT_MAX");

  /* --- the same values through vsnprintf's %f branch (#306) ---------------
   *
   * This is where num-large used to stop, at
   *
   *     snprintf(b, sizeof b, "%f", 1.0e15);
   *     CHECK(strlen(b) > 0 && strlen(b) < sizeof b, ...);
   *
   * - the branch's BOUNDS and never its VALUE. That blind spot was structural,
   * not an oversight of a line: the defect is that %f shifted the mantissa into
   * an `unsigned long`, and `unsigned long` is 64-bit here, so the 2^32 wrap
   * that bites on the rabbit cannot reproduce on this host at all. Asserting
   * the digits of 1e10 would still have passed.
   *
   * So the fix to the test is both halves: assert the digits, and choose
   * magnitudes that overflow SIXTY-FOUR bits, where a width-dependent shift is
   * wrong on the host too. The shim itself no longer has a width to inherit -
   * it narrows the double by bit surgery and renders it through luai_num2str,
   * the one float path in the file - so host digits and device digits are now
   * the same digits by construction.
   *
   * %f renders at float precision (LUA_32BITS makes lua_Number a float, so a
   * float is all a %f on this target can be carrying), hence the expected
   * strings are the exact values of the NEAREST FLOAT, not of the decimal
   * literal: 1e10 and 1e20 are shown below as themselves and as
   * 100000002004087734272, respectively, because the first is exactly a float
   * and the second is not. */
  snprintf(b, sizeof b, "%f", 1.0e10);
  eq_str(b, "10000000000.0", "%f keeps the digits of a value above 2^32");
  snprintf(b, sizeof b, "%f", 1.0e20);
  eq_str(b, "100000002004087734272.0", "%f keeps the digits of a value above 2^64");
  snprintf(b, sizeof b, "%f", -1.0e20);
  eq_str(b, "-100000002004087734272.0", "%f keeps the sign of a huge negative");
  snprintf(b, sizeof b, "%f", 1.0e15);
  eq_str(b, "999999986991104.0", "%f renders a double at float precision");
  snprintf(b, sizeof b, "%f", 0.25);
  eq_str(b, "0.0", "%f truncates a fraction to 0.0");

  /* An infinity used to take the `fexp - 52 >= 64` arm and print "0.0" - a
   * magnitude that does not exist. Both float paths say so now. */
  snprintf(b, sizeof b, "%f", (double)__builtin_inf());
  eq_str(b, "inf", "%f of an infinity says so");
  snprintf(b, sizeof b, "%f", -(double)__builtin_inf());
  eq_str(b, "-inf", "%f keeps the sign of a negative infinity");
  snprintf(b, sizeof b, "%f", (double)__builtin_nan(""));
  eq_str(b, "nan", "%f of a NaN says so");

  /* A double past the float range has no float to be narrowed to; saturating
   * to inf is what the narrowing conversion does, and it is at least not a
   * wrapped number. (The old branch printed "0.0" for this too.) */
  snprintf(b, sizeof b, "%f", 1.0e300);
  eq_str(b, "inf", "%f of a double beyond the float range saturates to inf");

  /* Width and sign still come from the padding machinery around the body. */
  snprintf(b, sizeof b, "%12f|", 42.0);
  eq_str(b, "        42.0|", "%f pads to width");
  snprintf(b, sizeof b, "%+f", 42.0);
  eq_str(b, "+42.0", "%f honours the plus flag");
}

/* ---------------------------------------------------------------------------
 * #254: fmt_hex8 replaced eight snprintf("%02x") calls. This is an
 * optimisation, not a bug fix, so the test's job is to pin the exact output -
 * especially the zero-padding, which is the behaviour a hand-rolled version
 * loses while still looking right on a random UID.
 * ------------------------------------------------------------------------- */
static void scen_hex8(void)
{
  static const uint8_t real[8]  = {0xd0, 0x02, 0x1a, 0x35, 0x06, 0x19, 0x8b, 0x86};
  static const uint8_t zeros[8] = {0x00, 0x0a, 0x00, 0x0f, 0x00, 0x00, 0x01, 0x00};
  static const uint8_t ones[8]  = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
  char b[17];

  printf("scenario hex8: UID formatting, zero nibbles included\n");

  /* The UID from boot.lua's GREEN_UID - the exact string scripts compare. */
  fmt_hex8(b, real);
  eq_str(b, "d0021a3506198b86", "fmt_hex8 real UID");

  /* High nibbles of zero: "%02x" pads, a naive rewrite drops them and shifts
   * every following character. */
  fmt_hex8(b, zeros);
  eq_str(b, "000a000f00000100", "fmt_hex8 zero-padding preserved");

  fmt_hex8(b, ones);
  eq_str(b, "ffffffffffffffff", "fmt_hex8 all ones");

  /* Exactly 16 chars + NUL, no overrun (ASan would catch a 17th write). */
  memset(b, 'Z', sizeof b);
  fmt_hex8(b, real);
  eq_int((long)strlen(b), 16, "fmt_hex8 writes exactly 16 characters");
  eq_int(b[16], 0, "fmt_hex8 NUL-terminates");
}

/* ---------------------------------------------------------------------------
 * parity - the two float paths, swept against glibc (#306).
 *
 * num-large pins named values. This pins the CONTRACT over the whole dynamic
 * range of the type, which is what #306 was really about: there must be one
 * float renderer, and its output must not depend on the width of any C type.
 *
 * The oracle is glibc's own printf. fmt.c overrides snprintf and vsnprintf but
 * NOT sprintf, so sprintf here is genuinely libc's and can referee. The
 * contract it is held to: the integer part of the NEAREST FLOAT, truncated
 * toward zero, plus ".0"; "inf"/"nan" as words; a double outside the float
 * range saturates to inf rather than wrapping.
 *
 * Every value is checked twice - through vsnprintf's %f and through
 * luai_num2str - and the two are required to agree with each other as well as
 * with glibc. Before #306 they did not: %f of 1e20 printed 1e20 mod 2^64 here
 * and 1e20 mod 2^32 on the device.
 * ------------------------------------------------------------------------- */
static int par_checks;

/* What %f must print for x, per glibc. */
static void par_reference(char *out, double x)
{
  float f = (float)x;

  if (isnan(f)) { strcpy(out, "nan"); return; }
  if (isinf(f)) { strcpy(out, signbit(f) ? "-inf" : "inf"); return; }
  sprintf(out, "%.0f.0", trunc((double)f));
}

static void par_check(double x, const char *what)
{
  char got[64], want[64];

  snprintf(got, sizeof got, "%f", x);
  par_reference(want, x);
  par_checks++;
  if (strcmp(got, want) != 0) {
    printf("  FAIL: %%f %s: x=%.17g got \"%s\", want \"%s\"\n", what, x, got, want);
    failures++;
  }
}

/* luai_num2str must render the same float the same way. */
static void par_check_num2str(float f, const char *what)
{
  char a[64], b[64];

  luai_num2str(a, sizeof a, f);
  snprintf(b, sizeof b, "%f", (double)f);
  par_checks++;
  if (strcmp(a, b) != 0) {
    printf("  FAIL: parity %s: f=%.9g num2str \"%s\", %%f \"%s\"\n", what, (double)f, a, b);
    failures++;
  }
}

static void scen_parity(void)
{
  union { float f; uint32_t u; } fv;
  union { double d; uint64_t u; } dv;
  unsigned i;

  printf("scenario parity: both float paths swept against glibc\n");

  /* The oracle itself, on a value whose digits are stated in this file, so a
   * broken reference cannot quietly agree with a broken shim. */
  {
    char ref[64];

    par_reference(ref, 1.0e10);
    eq_str(ref, "10000000000.0", "the glibc reference renders 1e10 as expected");
  }

  /* Binary boundaries where a width-dependent shift breaks: 2^24 is where
   * floats stop being contiguous integers, 2^32 is where the device's
   * `unsigned long` wrapped, 2^64 is where this host's did. */
  {
    static const double bounds[] = {
      0.0, 0.5, 1.0, 0.9999999,
      16777215.0, 16777216.0, 16777217.0,           /* 2^24 -1/+0/+1 */
      2147483648.0, 4294967040.0, 4294967296.0,     /* 2^31, largest < 2^32, 2^32 */
      9007199254740992.0,                           /* 2^53 */
      18446744073709551616.0,                       /* 2^64 */
      3.4028234663852886e38,                        /* FLT_MAX */
      1.1754943508222875e-38,                       /* FLT_MIN */
      1e-45, 1e-320,                                /* float, then double subnormal */
      1e39, 1e300,                                  /* past the float range: inf */
    };

    for (i = 0; i < sizeof bounds / sizeof *bounds; i++) {
      par_check(bounds[i], "boundary");
      par_check(-bounds[i], "boundary (negative)");
    }
  }

  par_check(__builtin_inf(), "inf");
  par_check(-__builtin_inf(), "-inf");
  par_check(__builtin_nan(""), "nan");

  /* Every float exponent crossed with a spread of mantissas - the whole
   * dynamic range of the type, including subnormals and the all-ones
   * exponent. */
  for (uint32_t e = 0; e < 256; e++) {
    for (uint32_t m = 0; m < 0x800000u; m += 0x800000u / 16) {
      fv.u = (e << 23) | m;
      par_check((double)fv.f, "float bits");
      par_check(-(double)fv.f, "float bits (negative)");
      par_check_num2str(fv.f, "float bits");
    }
  }

  /* Random DOUBLE bit patterns: mantissa bits below what a float can hold, so
   * these are the values where the narrowing has to round rather than copy. */
  srand(1);
  for (i = 0; i < 20000; i++) {
    dv.u = ((uint64_t)rand() << 40) ^ ((uint64_t)rand() << 20) ^ (uint64_t)rand();
    par_check(dv.d, "double bits");
  }

  /* A selector that matched nothing, or a sweep that stopped early, must not
   * report a pass (the vacuous-pass rule). */
  CHECK(par_checks > 30000, "the sweep actually compared its whole range");
}

int main(int argc, char **argv)
{
  const char *only = (argc > 1) ? argv[1] : NULL;

  if (!only || strcmp(only, "writestringerror") == 0) scen_writestringerror();
  if (!only || strcmp(only, "printf") == 0)           scen_printf();
  if (!only || strcmp(only, "num") == 0)              scen_num();
  if (!only || strcmp(only, "num-large") == 0)        scen_num_large();
  if (!only || strcmp(only, "hex8") == 0)             scen_hex8();
  if (!only || strcmp(only, "parity") == 0)           scen_parity();

  if (failures) {
    printf("fmt_test: %d check(s) FAILED\n", failures);
    return 1;
  }
  printf("fmt_test: all checks passed\n");
  return 0;
}
