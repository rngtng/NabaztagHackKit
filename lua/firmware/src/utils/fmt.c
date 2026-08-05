/**
 * @file fmt.c
 * @brief Formatting + number conversion shims that keep newlib's stdio out of
 *        the flash budget. Split out of main.c (#245).
 *
 * These are the strong symbols that override newlib's, plus the luai_* hooks
 * luaconf.h routes Lua's number formatting through. They were written inline in
 * main.c next to main(), which made ~250 lines of hand-rolled printf the least
 * testable code on the track - nothing could link them without pulling in a
 * second main(). Here they are a plain TU, unit-tested by test/host/fmt_test.c.
 *
 * No behaviour change in the move itself; the one fix is documented at
 * luai_writestringerror.
 *
 * The console sink (_write) stays in main.c: it is a syscall over the UART and
 * belongs with the console, and keeping it out of here is what lets the host
 * tests capture output with their own _write.
 *
 * NO FLOAT MAY CROSS A VARARGS BOUNDARY on the device. C default argument
 * promotion turns a float into a double at the call, which links libgcc's
 * double soft-float and costs kilobytes of a 124 KB budget. Print floats
 * through the non-variadic luai_num2str instead. This is why the number paths
 * below take their argument by a concrete type rather than through `...`;
 * float printing stays approximate pending a real dtoa.
 */
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include "lua.h"

#include "utils/fmt.h"

/* newlib syscall, defined in main.c (host tests supply their own). */
int _write(int fd, const char *ptr, int len);

/* ---- Lua console output -------------------------------------------------- */
/* luaconf.h routes lua_writestring/writeline/writestringerror here so print()
 * and the error/panic paths write straight to the UART _write syscall, never
 * linking newlib's buffered-FILE layer (~6 KB). */
void luai_writestring(const char *s, size_t l)
{
  _write(1, s, (int)l);
}

/* Every lua_writestringerror call site (lauxlib panic/warn) uses a "%s"-style
 * format with one const char* arg. snprintf is our own (below).
 *
 * #245: the clamp is `sizeof b - 1`, not `sizeof b`. snprintf returns the
 * length the output WOULD have had and writes at most sizeof b - 1 chars plus
 * the NUL, so clamping to sizeof b emitted 127 message bytes plus the
 * terminator - a stray NUL into the console stream that flash.py/uart_repl.py
 * then have to read past. */
void luai_writestringerror(const char *fmt, const char *arg)
{
  char b[128];
  int n = snprintf(b, sizeof b, fmt, arg);
  if (n <= 0)
    return;
  if (n > (int)sizeof b - 1)
    n = (int)sizeof b - 1;
  _write(2, b, n);
}

/* ---- compact vsnprintf/snprintf, overriding newlib ----------------------- */
/* Overriding these strong symbols keeps ~12 KB out of flash: Lua's number
 * formatting (lua_number2str/lua_integer2str/lua_pointer2str) and string.format
 * route through snprintf, and newlib's snprintf (_svfprintf_r) drags in the
 * buffered-FILE layer via __sinit's CHECK_INIT. This needs no FILE machinery.
 *
 * Supports the conversions Lua emits: d i u o x X c s p % - with flags
 * (-+space 0 #), width and precision (both incl '*'), and length modifiers
 * (h/hh/l/ll/L/z/t/j, parsed; values are fetched as `long` since LUA_32BITS
 * makes lua_Integer and pointers 32-bit, so no 64-bit divide helpers are
 * pulled). Float conversions (f e g a, any case) stay approximate - integer
 * part + ".0" - a real dtoa is still future work; they are rendered by
 * luai_num2str at the bottom of this file, which is the only float-to-digits
 * code here (#306). */

/* Widest digit string either float path can produce: FLT_MAX is 39 digits. */
#define NUM2STR_DIGITS 44

#define PF_LEFT  1
#define PF_PLUS  2
#define PF_SPACE 4
#define PF_ZERO  8
#define PF_ALT   16

/* unsigned -> digits (forward order) in out[]; returns the digit count. */
static int pf_utoa(char *out, unsigned long v, int base, int upper)
{
  const char *dig = upper ? "0123456789ABCDEF" : "0123456789abcdef";
  char rev[32];
  int i = 0;
  do {
    rev[i++] = dig[v % (unsigned)base];
    v /= (unsigned)base;
  } while (v);
  for (int j = 0; j < i; j++)
    out[j] = rev[i - 1 - j];
  return i;
}

static void pf_emit(char **d, char *end, size_t *n, char c)
{
  if (*d < end)
    *(*d)++ = c;
  (*n)++;
}

static void pf_pad(char **d, char *end, size_t *n, char c, int count)
{
  while (count-- > 0)
    pf_emit(d, end, n, c);
}

/* IEEE-754 double bits -> the nearest float, by integer surgery alone (#306).
 * A plain (float)dv cast would link libgcc's double soft-float (~2.4 KB, #213),
 * which is the whole reason the %f branch takes the bits apart at all. This is
 * the exact inverse of the promotion that put the value in the varargs: on this
 * target LUA_32BITS makes lua_Number a float, so every float that reaches a
 * "..." arrived as one. Rounds to nearest with ties to even and saturates to
 * inf on overflow, as the cast would, so "%f" and print() of the same value
 * render the same digits. It differs from a true cast in one place that cannot
 * reach the output: anything below the float's NORMAL range flushes to zero
 * instead of becoming a subnormal, and every such value is < 1, which this
 * shim prints as "0.0" either way. */
static float pf_d2f(uint64_t u)
{
  union { float f; uint32_t u; } fv;
  uint32_t sign = (uint32_t)(u >> 63) << 31;
  int e = (int)((u >> 52) & 0x7FF);
  uint64_t frac = u & 0xFFFFFFFFFFFFFULL;
  uint32_t m = (uint32_t)(frac >> 29);              /* top 23 fraction bits */

  if (e == 0x7FF)                                   /* inf / NaN stay themselves */
    fv.u = sign | 0x7F800000u | (frac ? (m ? m : 1u) : 0u);
  else if (e == 0)                                  /* zero, double subnormal */
    fv.u = sign;
  else {
    int fe = e - 1023 + 127;                        /* rebias */
    uint32_t rest = (uint32_t)(frac & 0x1FFFFFFFu); /* the 29 bits dropped above */

    if (rest > 0x10000000u || (rest == 0x10000000u && (m & 1u))) {
      if (++m == 0x800000u) {                       /* the carry left the field */
        m = 0;
        fe++;
      }
    }
    if (fe >= 0xFF)
      fv.u = sign | 0x7F800000u;                    /* overflows the float range */
    else if (fe <= 0)
      fv.u = sign;                                  /* underflows it; |x| < 1 anyway */
    else
      fv.u = sign | ((uint32_t)fe << 23) | m;
  }
  return fv.f;
}

int vsnprintf(char *buf, size_t size, const char *fmt, va_list ap)
{
  char *d = buf;
  char *end = (size > 0) ? buf + size - 1 : buf;   /* reserve room for NUL */
  size_t n = 0;

  for (; *fmt; fmt++) {
    if (*fmt != '%') {
      pf_emit(&d, end, &n, *fmt);
      continue;
    }
    fmt++;                                          /* skip '%' */

    int flags = 0;
    for (;; fmt++) {
      if (*fmt == '-')      flags |= PF_LEFT;
      else if (*fmt == '+') flags |= PF_PLUS;
      else if (*fmt == ' ') flags |= PF_SPACE;
      else if (*fmt == '0') flags |= PF_ZERO;
      else if (*fmt == '#') flags |= PF_ALT;
      else break;
    }

    int width = 0;
    if (*fmt == '*') {
      width = va_arg(ap, int);
      if (width < 0) { flags |= PF_LEFT; width = -width; }
      fmt++;
    } else {
      while (*fmt >= '0' && *fmt <= '9')
        width = width * 10 + (*fmt++ - '0');
    }

    int prec = -1;
    if (*fmt == '.') {
      fmt++;
      if (*fmt == '*') { prec = va_arg(ap, int); fmt++; }
      else { prec = 0; while (*fmt >= '0' && *fmt <= '9') prec = prec * 10 + (*fmt++ - '0'); }
      if (prec < 0) prec = -1;
    }

    int islong = 0;                                 /* fetch value as long? */
    for (;;) {
      if (*fmt == 'l') { islong = 1; fmt++; }
      else if (*fmt == 'h' || *fmt == 'L' || *fmt == 'j' || *fmt == 'z' || *fmt == 't') fmt++;
      else break;
    }

    char conv = *fmt;
    if (conv == '\0') break;

    char tmp[NUM2STR_DIGITS + 4];      /* sized for the widest float body */
    const char *body = tmp;
    int blen = 0;
    char sign = 0;
    const char *prefix = "";
    int is_num = 0;

    switch (conv) {
      case 'd': case 'i': {
        long v = islong ? va_arg(ap, long) : (long)va_arg(ap, int);
        unsigned long uv;
        if (v < 0) { sign = '-'; uv = (unsigned long)(-(v + 1)) + 1; }
        else { uv = (unsigned long)v; sign = (flags & PF_PLUS) ? '+' : (flags & PF_SPACE) ? ' ' : 0; }
        blen = pf_utoa(tmp, uv, 10, 0);
        is_num = 1;
        break;
      }
      case 'u': case 'o': case 'x': case 'X': {
        unsigned long uv = islong ? va_arg(ap, unsigned long) : (unsigned long)va_arg(ap, unsigned int);
        int base = (conv == 'o') ? 8 : (conv == 'u') ? 10 : 16;
        blen = pf_utoa(tmp, uv, base, conv == 'X');
        if ((flags & PF_ALT) && uv != 0)
          prefix = (conv == 'o') ? "0" : (conv == 'X') ? "0X" : "0x";
        is_num = 1;
        break;
      }
      case 'p': {
        unsigned long uv = (unsigned long)(uintptr_t)va_arg(ap, void *);
        prefix = "0x";
        blen = pf_utoa(tmp, uv, 16, 0);
        is_num = 1;
        prec = -1;
        break;
      }
      case 'c':
        tmp[0] = (char)va_arg(ap, int);
        blen = 1;
        break;
      case 's': {
        const char *s = va_arg(ap, const char *);
        if (s == NULL) s = "(null)";
        body = s;
        while (s[blen] && (prec < 0 || blen < prec)) blen++;
        break;
      }
      case '%':
        tmp[0] = '%';
        blen = 1;
        break;
      case 'f': case 'F': case 'e': case 'E':
      case 'g': case 'G': case 'a': case 'A': {
        /* Varargs promoted the float to a double (C default argument
         * promotion, unavoidable through '...'), so narrow it back by bit
         * surgery and render it through luai_num2str - the one float-to-digits
         * path in this file (#306).
         *
         * This branch used to shift the mantissa into an `unsigned long` of its
         * own, which is the defect #301 removed next door: the width of the
         * output was the width of the target's `unsigned long`, so %f of 1e10
         * printed "1410065408.0" on the rabbit - 1e10 mod 2^32, not an
         * approximation of anything - and an infinity took the `>= 64` arm and
         * printed "0.0". Going through luai_num2str deletes that second
         * implementation instead of repairing it: its digits come from
         * dec_shifted(), which shifts in DECIMAL and so is exact on both
         * widths, and inf/nan come along with it. */
        union { double d; uint64_t u; } fv;
        fv.d = va_arg(ap, double);
        blen = luai_num2str(tmp, sizeof tmp, pf_d2f(fv.u));
        if (tmp[0] == '-') {                       /* let the padding own the sign */
          sign = '-';
          body = tmp + 1;
          blen--;
        } else {
          sign = (flags & PF_PLUS) ? '+' : (flags & PF_SPACE) ? ' ' : 0;
        }
        is_num = (body[0] >= '0' && body[0] <= '9');   /* "inf"/"nan": no 0-pad */
        prec = -1;
        break;
      }
      default:                                       /* unknown: emit literally */
        pf_emit(&d, end, &n, '%');
        pf_emit(&d, end, &n, conv);
        continue;
    }

    int zeros = 0;
    if (is_num && prec >= 0) {                        /* precision = min digits */
      if (blen < prec) zeros = prec - blen;
      flags &= ~PF_ZERO;                              /* precision disables '0' */
    }

    int preflen = 0;
    while (prefix[preflen]) preflen++;
    int total = (sign ? 1 : 0) + preflen + zeros + blen;
    int pad = (width > total) ? width - total : 0;

    if (!(flags & PF_LEFT) && !(flags & PF_ZERO))
      pf_pad(&d, end, &n, ' ', pad);
    if (sign)
      pf_emit(&d, end, &n, sign);
    for (int i = 0; i < preflen; i++)
      pf_emit(&d, end, &n, prefix[i]);
    if (!(flags & PF_LEFT) && (flags & PF_ZERO))
      pf_pad(&d, end, &n, '0', pad);
    pf_pad(&d, end, &n, '0', zeros);
    for (int i = 0; i < blen; i++)
      pf_emit(&d, end, &n, body[i]);
    if (flags & PF_LEFT)
      pf_pad(&d, end, &n, ' ', pad);
  }

  if (size > 0)
    *d = '\0';
  return (int)n;
}

int snprintf(char *buf, size_t size, const char *fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  int r = vsnprintf(buf, size, fmt, ap);
  va_end(ap);
  return r;
}

/* ---- 8-byte UID -> lowercase hex (#254) ---------------------------------- */
/* Was eight snprintf("%02x") calls in main.c's push_uid_hex - eight walks of a
 * format string, eight flag/width parses and eight pf_utoa divide loops to
 * produce sixteen characters, on the per-RFID-event path. A nibble table does
 * it with no division at all. Writes exactly 16 chars + NUL. */
void fmt_hex8(char out[17], const uint8_t uid[8])
{
  static const char hexdig[] = "0123456789abcdef";
  for (int i = 0; i < 8; i++) {
    out[i * 2]     = hexdig[uid[i] >> 4];
    out[i * 2 + 1] = hexdig[uid[i] & 0x0F];
  }
  out[16] = '\0';
}

/* ---- float -> string for Lua's number printing (#213) --------------------- */
/* luaconf.h routes lua_number2str/lua_number2strx (lobject.c's tostringbuff,
 * string.format's %a and %q-on-floats) here. Non-variadic on purpose: a float
 * passed through '...' is promoted to double by the caller (C default argument
 * promotion), which links libgcc's double soft-float (~2.4 KB). Same
 * integer-part + ".0" output as the vsnprintf float stub (a real dtoa is still
 * future work), with the integer part taken from the float's own bits so no
 * double ever exists. snprintf contract: truncate to sz, NUL-terminate,
 * return the untruncated length. */
/* Decimal digits of (v << shift), most significant first, into out[]. Returns
 * the digit count.
 *
 * The shift is done in DECIMAL. `v` is at most 24 bits, so pf_utoa converts it
 * with an ordinary 32-bit divide, and the value is then doubled `shift` times
 * by adding it to itself digit by digit - the /10 and %10 in that loop are by a
 * constant, which the compiler turns into a multiply, not a library call.
 *
 * The point is exactness on BOTH widths (#301). The old
 * `(unsigned long)mant << (fexp - 23)` inherited the width of `unsigned long`:
 * it wrapped at 2^32 on the rabbit, so print(1e10) came out as 1410065408.0 -
 * 1e10 mod 2^32, not an approximation of anything - and on a 64-bit host it
 * produced up to 17 digits and ran off the caller's buffer. Accumulating in
 * uint64_t is the other way to fix it and is not free here: pf_utoa's /= and %=
 * would link libgcc's __aeabi_uldivmod. (NUM2STR_DIGITS is defined at the top
 * of the file: vsnprintf's %f branch sizes its own body buffer by it, since
 * that branch renders through here too - #306.) */

static int dec_shifted(char *out, uint32_t v, int shift)
{
  char d[NUM2STR_DIGITS];   /* least-significant digit first */
  int n, i, j;

  n = pf_utoa(out, v, 10, 0);          /* MSB-first, at most 8 digits */
  for (i = 0; i < n; i++)              /* flip to LSB-first for the carries */
    d[i] = (char)(out[n - 1 - i] - '0');

  while (shift-- > 0) {
    int carry = 0;

    for (i = 0; i < n; i++) {
      int t = d[i] * 2 + carry;

      d[i] = (char)(t % 10);
      carry = t / 10;
    }
    while (carry && n < NUM2STR_DIGITS) {
      d[n++] = (char)(carry % 10);
      carry /= 10;
    }
  }

  for (i = 0, j = n - 1; j >= 0; i++, j--)
    out[i] = (char)('0' + d[j]);
  return n;
}

int luai_num2str(char *s, size_t sz, float n)
{
  union { float f; uint32_t u; } fv;
  fv.f = n;
  int fexp = (int)((fv.u >> 23) & 0xFF) - 127;
  uint32_t mant = (fv.u & 0x7FFFFF) | (1UL << 23);
  char body[NUM2STR_DIGITS + 4];       /* sign + digits + ".0" */
  int blen = 0;

  if (fv.u >> 31)
    body[blen++] = '-';

  if (fexp == 128) {
    /* Exponent all-ones: infinity or NaN. Printing the mantissa as a number
     * would claim a magnitude that does not exist - the old code printed "0.0"
     * for an infinity, which is worse than saying so. */
    const char *w = (fv.u & 0x7FFFFF) ? "nan" : "inf";

    if (w[0] == 'n')
      blen = 0;                        /* NaN has no sign worth printing */
    while (*w)
      body[blen++] = *w++;
  } else if (fexp < 0) {
    body[blen++] = '0';                /* |x| < 1, subnormals, zero */
    body[blen++] = '.';
    body[blen++] = '0';
  } else {
    if (fexp <= 23)
      blen += pf_utoa(body + blen, mant >> (23 - fexp), 10, 0);
    else
      blen += dec_shifted(body + blen, mant, fexp - 23);
    body[blen++] = '.';
    body[blen++] = '0';
  }

  size_t copy = (sz > 0) ? (size_t)blen : 0;
  if (copy > 0 && copy > sz - 1)
    copy = sz - 1;
  for (size_t i = 0; i < copy; i++)
    s[i] = body[i];
  if (sz > 0)
    s[copy] = '\0';
  return blen;
}

/* ---- decimal string -> Lua number ---------------------------------------- */
/* Replaces strtof as Lua's lua_str2number (see luaconf.h). strtof drags in
 * newlib's double strtod + gdtoa multi-precision machinery (~14 KB). Lua only
 * reaches here for *decimal float* numerals - luaO_str2num tries the integer
 * path (l_str2int) first, and hex-floats use Lua's own lua_strx2number - so
 * this handles [ws][sign]digits[.digits][(e|E)[sign]digits] and nothing else.
 *
 * Contract matches strtof as used by l_str2dloc(): set *endptr to the first
 * unconsumed char, leave it at 's' (return 0) when no digit is seen. Mantissa
 * is accumulated in a float and scaled by 10^exp via binary exponentiation, so
 * only single-float mul/div are used (no libm, no strtod). Last-ulp rounding is
 * looser than strtof - acceptable here (integer-first target). */
#define LUAI_ISDIGIT(c) ((c) >= '0' && (c) <= '9')

LUA_NUMBER luai_str2number(const char *s, char **endptr)
{
  const char *p = s;
  while (*p == ' ' || (*p >= '\t' && *p <= '\r'))  /* skip leading whitespace */
    p++;

  int neg = 0;
  if (*p == '+' || *p == '-') {
    neg = (*p == '-');
    p++;
  }

  lua_Number val = 0;
  int anydig = 0;
  int fracdigits = 0;
  while (LUAI_ISDIGIT(*p)) {
    val = val * 10 + (*p - '0');
    p++;
    anydig = 1;
  }
  if (*p == '.') {
    p++;
    while (LUAI_ISDIGIT(*p)) {
      val = val * 10 + (*p - '0');
      fracdigits++;
      p++;
      anydig = 1;
    }
  }
  if (!anydig) {              /* no mantissa digits: nothing valid */
    *endptr = (char *)s;
    return 0;
  }

  int exp = -fracdigits;
  if (*p == 'e' || *p == 'E') {   /* optional exponent */
    const char *ep = p + 1;
    int eneg = 0, edig = 0, eval = 0;
    if (*ep == '+' || *ep == '-') {
      eneg = (*ep == '-');
      ep++;
    }
    while (LUAI_ISDIGIT(*ep)) {
      eval = eval * 10 + (*ep - '0');
      ep++;
      edig = 1;
    }
    if (edig) {                   /* only consume 'e...' if it has digits */
      exp += eneg ? -eval : eval;
      p = ep;
    }
  }

  lua_Number scale = 1, base = 10;   /* scale = 10^|exp| by binary exponentiation */
  for (int e = (exp < 0 ? -exp : exp); e; e >>= 1) {
    if (e & 1)
      scale *= base;
    base *= base;
  }
  val = (exp < 0) ? val / scale : val * scale;
  if (neg)
    val = -val;

  *endptr = (char *)p;
  return val;
}

/* ---- float ^ and % without libm ------------------------------------------ */
/* luaconf.h routes luai_numpow/luai_nummod here so Lua's `^` and float `%` do
 * not pull libm's powf/fmodf (~4 KB: __ieee754_powf/fmodf, scalbnf, wf_pow).
 * `^` yields a float in Lua: integer exponents are exact (binary
 * exponentiation), fractional exponents return NaN (no libm here; math.sqrt et
 * al. are unavailable anyway). `%` is Lua floor-mod, computed by truncation. */
#define LUAI_NAN (__builtin_nanf(""))

LUA_NUMBER luai_pow(LUA_NUMBER a, LUA_NUMBER b)
{
  long n = (long)b;
  if ((LUA_NUMBER)n != b)          /* non-integer exponent: unsupported */
    return LUAI_NAN;
  int neg = n < 0;
  unsigned long e = neg ? (unsigned long)(-n) : (unsigned long)n;
  LUA_NUMBER r = 1, base = a;
  while (e) {
    if (e & 1)
      r *= base;
    base *= base;
    e >>= 1;
  }
  return neg ? (LUA_NUMBER)1 / r : r;
}

LUA_NUMBER luai_fmod(LUA_NUMBER a, LUA_NUMBER b)
{
  if (b == 0)
    return LUAI_NAN;
  LUA_NUMBER q = a / b;
  /* Truncate toward zero (C fmod). At |q| >= 2^24 every float is already
   * integral, so only the small range needs the int round-trip - and (long)
   * stays within single-float helpers. The former (long long) cast pulled
   * __aeabi_f2lz, whose libgcc __fixunssfdi converts VIA DOUBLE, dragging in
   * the double soft-float (~740 B: muldf3 + fixunsdfsi, #213). */
  LUA_NUMBER n = (q >= 16777216.0f || q <= -16777216.0f)
                     ? q : (LUA_NUMBER)(long)q;
  LUA_NUMBER m = a - n * b;                  /* remainder, sign of a */
  if (m != 0 && ((m < 0) != (b < 0)))        /* sign differs from b -> floor */
    m += b;
  return m;
}
