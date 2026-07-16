/**
 * @file lua.c
 * @brief Boot PUC-Rio Lua 5.4 and run a REPL - the first real language runtime
 *        on the ML67Q4051. Opens a trimmed stdlib, runs an embedded demo chunk,
 *        then drops into a REPL. Console = UART0 (#207); heap = 1 MB ExtRAM.
 *
 * Bare metal supplies neither, so this file also overrides the newlib syscalls
 * (_read/_write over the polled UART, _sbrk into ExtRAM) and Lua's number/printf
 * helpers to keep the buffered-FILE + libm layers out of the flash budget.
 */
#include <errno.h>
#include <stdarg.h>   /* vsnprintf override */
#include <stddef.h>
#include <stdint.h>   /* uintptr_t in the %p path */
#include <stdio.h>
#include <stdlib.h>   /* malloc/free - #LC bytecode frame buffer */
#include <string.h>   /* strcmp - LED-name lookup in the nab binding */

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

/* Hardware bindings: LED driver + head button. */
#include "ml674061.h"
#include "common.h"
#include "hal/spi.h"
#include "hal/led.h"
#include "hal/button.h"
#include "hal/audio.h"
#include "hal/adc.h"
#include "hal/i2c.h"
#include "hal/rfid.h"
#include "hal/motor.h"   /* ear motors + encoders */
#include "hal/uart.h"    /* console: polled UART0 TX/RX (#207) */
#include "hal/wifi.h"    /* USB RT2501 802.11 join - nab.wifi() */
#include "irq.h"         /* init_irq: interrupt controller + tick (wifi needs it) */
#include "utils/delay.h" /* init_tick + counter_timer (the wifi stack's clock) */

#include "tone_mp3.h"   /* nab_tone_mp3[]: built-in MP3 tone for nab.tone() */

/* ---- UART console (#207) ------------------------------------------------- */
/* The REPL console is UART0 (hal/uart.c): polled TX + polled RX, 38400 8N1.
 * init_uart() runs at boot (main); read/drive it on the Pi's /dev/serial0.
 *
 * EOF: getch_uart() is non-blocking (-1 = RX FIFO empty) and a raw UART has no
 * native end-of-stream, so _read() blocks until a byte arrives and treats EOT
 * (0x04, what Ctrl-D sends) as EOF - EOF is what ends the REPL loop and fires
 * <<FV_DONE>>. The host feeder (replpipe/flash.py/simulator) appends EOT after
 * the input it sends; hex #LC frames and source lines never contain 0x04. */
#define CONSOLE_EOF 0x04   /* EOT / Ctrl-D: end of console input */

/* Raw console write, independent of stdio buffering - used for prompts/errors. */
static void sh_puts(const char *s)
{
  while (*s)
    putch_uart((uint8_t)*s++);
}

/* ---- newlib syscalls: stdout/stdin over UART, heap in ExtRAM ------------- */
/* Our own definitions win over libnosys' stubs (object beats archive member). */

int _write(int fd, const char *ptr, int len)
{
  (void)fd;
  for (int i = 0; i < len; i++)
    putch_uart((uint8_t)ptr[i]);
  return len;
}

int _read(int fd, char *ptr, int len)
{
  (void)fd;
  if (len <= 0)
    return 0;
  int c;
  while ((c = getch_uart()) < 0)
    ;                      /* block: no byte yet (UART has no native EOF) */
  if (c == CONSOLE_EOF)
    return 0;              /* EOT -> EOF, ends the REPL */
  ptr[0] = (char)c;
  return 1;               /* one char per call; sh_gets reassembles the line */
}

/* Read one line into buf (keeps the trailing '\n', NUL-terminates), built on
 * the single-char _read syscall. Replaces fgets() so the REPL needs no newlib
 * stdio FILE layer. Returns NULL on immediate EOF, like fgets. */
static char *sh_gets(char *buf, int size)
{
  int i = 0;
  for (;;) {
    char c;
    if (_read(0, &c, 1) != 1) {   /* EOF / no input (simulator) */
      if (i == 0)
        return NULL;
      break;
    }
    if (i < size - 1)
      buf[i++] = c;
    if (c == '\n')
      break;
  }
  buf[i] = '\0';
  return buf;
}

/* ---- Lua console output -------------------------------------------------- */
/* luaconf.h routes lua_writestring/writeline/writestringerror here so print()
 * and the error/panic paths write straight to the UART _write syscall, never
 * linking newlib's buffered-FILE layer (~6 KB). */
void luai_writestring(const char *s, size_t l)
{
  _write(1, s, (int)l);
}

/* Every lua_writestringerror call site (lauxlib panic/warn) uses a "%s"-style
 * format with one const char* arg. snprintf is our own (below). */
void luai_writestringerror(const char *fmt, const char *arg)
{
  char b[128];
  int n = snprintf(b, sizeof b, fmt, arg);
  if (n <= 0)
    return;
  if (n > (int)sizeof b)
    n = (int)sizeof b;
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
 * part + ".0" - a real dtoa is future work. */

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

    char tmp[32];
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
         * promotion, unavoidable through '...'), so take the IEEE-754 bits
         * apart instead of doing double arithmetic - a single (long)dv or
         * dv < 0 would link libgcc's double soft-float (~2.4 KB, #213).
         * Output is the same integer part + ".0" as before. */
        union { double d; uint64_t u; } fv;
        fv.d = va_arg(ap, double);
        int fexp = (int)((fv.u >> 52) & 0x7FF) - 1023;
        uint64_t mant = (fv.u & 0xFFFFFFFFFFFFFULL) | (1ULL << 52);
        unsigned long uv;
        if (fexp < 0)
          uv = 0;                                  /* |x| < 1, subnormals, 0 */
        else if (fexp <= 52)
          uv = (unsigned long)(mant >> (52 - fexp));
        else                                       /* huge/inf/nan: low bits */
          uv = (fexp - 52 < 64) ? (unsigned long)(mant << (fexp - 52)) : 0;
        if (fv.u >> 63) sign = '-';
        else sign = (flags & PF_PLUS) ? '+' : (flags & PF_SPACE) ? ' ' : 0;
        blen = pf_utoa(tmp, uv, 10, 0);
        tmp[blen++] = '.';
        tmp[blen++] = '0';
        is_num = 1;
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

/* ---- float -> string for Lua's number printing (#213) --------------------- */
/* luaconf.h routes lua_number2str/lua_number2strx (lobject.c's tostringbuff,
 * string.format's %a and %q-on-floats) here. Non-variadic on purpose: a float
 * passed through '...' is promoted to double by the caller (C default argument
 * promotion), which links libgcc's double soft-float (~2.4 KB). Same
 * integer-part + ".0" output as the vsnprintf float stub (a real dtoa is still
 * future work), with the integer part taken from the float's own bits so no
 * double ever exists. snprintf contract: truncate to sz, NUL-terminate,
 * return the untruncated length. */
int luai_num2str(char *s, size_t sz, float n)
{
  union { float f; uint32_t u; } fv;
  fv.f = n;
  int fexp = (int)((fv.u >> 23) & 0xFF) - 127;
  uint32_t mant = (fv.u & 0x7FFFFF) | (1UL << 23);
  unsigned long uv;
  if (fexp < 0)
    uv = 0;                                      /* |x| < 1, subnormals, 0 */
  else if (fexp <= 23)
    uv = mant >> (23 - fexp);
  else                                           /* huge/inf/nan: low bits */
    uv = (fexp - 23 < 32) ? (unsigned long)mant << (fexp - 23) : 0;

  char body[16];                                 /* -,10 digits,.,0 = 13 max */
  int blen = 0;
  if (fv.u >> 31)
    body[blen++] = '-';
  blen += pf_utoa(body + blen, uv, 10, 0);
  body[blen++] = '.';
  body[blen++] = '0';

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

/* ---- abort: halt, don't raise(SIGABRT) ----------------------------------- */
/* newlib's abort() calls raise() + _exit(), pulling the signal machinery
 * (raise/signal/_kill_r/_getpid). Bare metal has no OS to signal, so halt in
 * place. */
void abort(void)
{
  for (;;) {
  }
}

/* Heap = the 1 MB external RAM window; IntRAM is too small for a Lua state. */
extern char __extram_start__, __extram_end__;

void *_sbrk(ptrdiff_t incr)
{
  static char *cur;
  if (cur == NULL)
    cur = &__extram_start__;
  if (cur + incr > &__extram_end__) {
    errno = ENOMEM;
    return (void *)-1;
  }
  char *prev = cur;
  cur += incr;
  return prev;
}

/* ---- hardware bindings: the `nab` module --------------------------------- */
/* Exposes the LEDs, head button, audio, RFID coupler, and ear motors to Lua. */

/* nab.led(name, r, g, b): light an RGB LED. name is one of
 * nose|belly|left|right|bottom (physical map verified on hardware, LLC2_4c -
 * see inc/hal/led.h). r/g/b are 7-bit intensities (0..127), the TLC5922 range. */
static int nab_led(lua_State *L)
{
  const char *name = luaL_checkstring(L, 1);
  lua_Integer r = luaL_checkinteger(L, 2);
  lua_Integer g = luaL_checkinteger(L, 3);
  lua_Integer b = luaL_checkinteger(L, 4);
  luaL_argcheck(L, r >= 0 && r <= 127, 2, "0..127");
  luaL_argcheck(L, g >= 0 && g <= 127, 3, "0..127");
  luaL_argcheck(L, b >= 0 && b <= 127, 4, "0..127");

  uint32_t ch;
  if      (strcmp(name, "belly")  == 0) ch = LED_RGB_1;
  else if (strcmp(name, "bottom") == 0) ch = LED_RGB_2;
  else if (strcmp(name, "left")   == 0) ch = LED_RGB_3;
  else if (strcmp(name, "right")  == 0) ch = LED_RGB_4;
  else if (strcmp(name, "nose")   == 0) ch = LED_RGB_5;
  else return luaL_error(L, "bad LED '%s'", name);  /* nose|belly|left|right|bottom */

  set_led_rgb(ch | ((uint32_t)r << 16) | ((uint32_t)g << 8) | (uint32_t)b);
  return 0;
}

/* nab.button() -> boolean: true while the head button is held (polled). */
static int nab_button(lua_State *L)
{
  lua_pushboolean(L, button_pressed());
  return 1;
}

/* nab.volume(v): output volume, 0 = loudest .. 254 = quietest (VS1003). */
static int nab_volume(lua_State *L)
{
  lua_Integer v = luaL_checkinteger(L, 1);
  luaL_argcheck(L, v >= 0 && v <= 254, 1, "0..254");
  set_vlsi_volume((uint8_t)v);
  return 0;
}

/* nab.beep([freq [, ms]]): play the VS1003 built-in sine test. freq is the
 * VS10xx sine-skip byte (pitch, 0..255, default 0x44); ms is an approximate
 * duration - a CPU busy-loop, since firmwareV2 has no timer yet, so it is
 * rough (default 300). The tone plays on the codec while the CPU spins. */
static int nab_beep(lua_State *L)
{
  lua_Integer freq = luaL_optinteger(L, 1, 0x44);
  lua_Integer ms = luaL_optinteger(L, 2, 300);
  luaL_argcheck(L, freq >= 0 && freq <= 255, 1, "0..255");
  luaL_argcheck(L, ms >= 0 && ms <= 10000, 2, "0..10000");

  vlsi_ampli(1);                   /* plays at the current nab.volume setting */
  vlsi_sine((uint8_t)freq, 1);
  for (volatile unsigned long i = 0; i < (unsigned long)ms * 3000UL; i++)
    CLR_WDT;                       /* ~ms; no timer, so approximate */
  vlsi_sine((uint8_t)freq, 0);
  vlsi_ampli(0);
  return 0;
}

/* nab.play(data): stream a byte buffer (e.g. an MP3 file's bytes) to the
 * VS1003 decoder over SDI. Unlike nab.beep this is real decoded audio and
 * nab.volume actually attenuates it. Blocking: returns once the buffer is fed
 * and flushed. */
static int nab_play(lua_State *L)
{
  size_t len;
  const char *data = luaL_checklstring(L, 1, &len);
  vlsi_play((const uint8_t *)data, (uint32_t)len);
  return 0;
}

/* nab.tone(): a small built-in MP3 tone (nab_tone_mp3, see tone_mp3.h) so
 * nab.play + nab.volume are demoable without shipping a file. It is MP3, not
 * raw PCM WAV: the VS1003B on this board decodes MP3 but NOT PCM WAV
 * (hardware-verified). Feed to nab.play. */
static int nab_tone(lua_State *L)
{
  lua_pushlstring(L, (const char *)nab_tone_mp3, sizeof nab_tone_mp3);
  return 1;
}

/* nab.wheel(): 8-bit ADC ch.2 reading (0..255). The back wheel is almost
 * certainly an analog pot on this channel (ADCON1_CH2, same register sequence
 * as src/firmware's get_adc_value) - not yet hardware-confirmed. To map it to
 * volume: `nab.volume(nab.wheel())` in a polling loop. */
static int nab_wheel(lua_State *L)
{
  lua_pushinteger(L, adc_read_ch2());
  return 1;
}

/* nab.rfid() -> UID as a lowercase hex string (e.g. "a1b2c3d4e5f60708"), or
 * nil if no tag is on the coupler. Scans the CRX14 (I2C 0xA0) each call - no
 * caching, so placing/removing a tag is reflected on the next poll. */
static int nab_rfid(lua_State *L)
{
  uint8_t uid[8];
  int8_t found = rfid_read_uid(uid);
  if (found <= 0) {
    lua_pushnil(L);
    return 1;
  }
  char hex[17];
  for (int i = 0; i < 8; i++)
    snprintf(hex + i * 2, 3, "%02x", uid[i]);
  lua_pushstring(L, hex);
  return 1;
}

/* nab.ear_move(n, speed, dir): drive ear motor n (1 or 2) at speed (0..255)
 * in dir ("forward"|"reverse"). Runs until nab.ear_stop() or another
 * nab.ear_move() call - there is no closed-loop position control here (see
 * hal/motor.h: the encoder is a raw hole counter, not a homed position). */
static int nab_ear_move(lua_State *L)
{
  lua_Integer n = luaL_checkinteger(L, 1);
  lua_Integer speed = luaL_checkinteger(L, 2);
  const char *dir = luaL_checkstring(L, 3);
  luaL_argcheck(L, n == 1 || n == 2, 1, "1 or 2");
  luaL_argcheck(L, speed >= 0 && speed <= 255, 2, "0..255");

  uint8_t rotation;
  if      (strcmp(dir, "forward") == 0) rotation = FORWARD;
  else if (strcmp(dir, "reverse") == 0) rotation = REVERSE;
  else return luaL_error(L, "bad direction '%s' (forward|reverse)", dir);

  run_motor((uint8_t)n, (uint8_t)speed, rotation);
  return 0;
}

/* nab.ear_stop(n): stop ear motor n (1 or 2). */
static int nab_ear_stop(lua_State *L)
{
  lua_Integer n = luaL_checkinteger(L, 1);
  luaL_argcheck(L, n == 1 || n == 2, 1, "1 or 2");
  stop_motor((uint8_t)n);
  return 0;
}

/* nab.ear_pos(n) -> integer: raw 16-bit encoder pulse count for motor n (1 or
 * 2). Free-running hardware counter - watch it change while ear_move runs,
 * it is not a homed/absolute angle (see hal/motor.h). */
static int nab_ear_pos(lua_State *L)
{
  lua_Integer n = luaL_checkinteger(L, 1);
  luaL_argcheck(L, n == 1 || n == 2, 1, "1 or 2");
  lua_pushinteger(L, get_motor_position((uint8_t)n));
  return 1;
}

/* nab.sci(reg): read a VS1003 SCI register (diagnostic). e.g. HDAT1=0x09 shows
 * the decoder's detected stream format (0 = nothing decoding), SS_VER lives in
 * STATUS=0x01. Used to confirm nab.play's stream reaches the decoder. */
static int nab_sci(lua_State *L)
{
  uint8_t reg = (uint8_t)luaL_checkinteger(L, 1);
  lua_pushinteger(L, vlsi_read_sci(reg));
  return 1;
}

/* nab.sciw(reg, val): write a VS1003 SCI register (diagnostic). Pairs with
 * nab.sci to bring up / probe the codec from the REPL. */
static int nab_sciw(lua_State *L)
{
  uint8_t reg = (uint8_t)luaL_checkinteger(L, 1);
  uint16_t val = (uint16_t)luaL_checkinteger(L, 2);
  vlsi_write_sci(reg, val);
  return 0;
}

/* nab.wifi(ssid [, psk]) -> true on connect, or (nil, message). Blocking:
 * brings up the USB RT2501 dongle (cold-boot + firmware upload + radio settle),
 * scans for ssid, then runs the WPA/WPA2 join (auth + assoc + 4-way handshake),
 * pumping until connected or ~30 s. psk is required for an encrypted AP; omit
 * (or "") for an open one. The whole USB + 802.11 stack is pulled into the
 * image only because this binding references it (see hal/wifi.c). */
static int nab_wifi(lua_State *L)
{
  const char *ssid = luaL_checkstring(L, 1);
  const char *psk = luaL_optstring(L, 2, "");
  if (wifi_connect(ssid, psk, 30000) != 0) {
    lua_pushnil(L);
    lua_pushfstring(L, "wifi: could not connect to '%s'", ssid);
    return 2;
  }
  lua_pushboolean(L, 1);
  return 1;
}

static const luaL_Reg nab_funcs[] = {
    {"led", nab_led},
    {"wifi", nab_wifi},
    {"button", nab_button},
    {"volume", nab_volume},
    {"beep", nab_beep},
    {"play", nab_play},
    {"tone", nab_tone},
    {"wheel", nab_wheel},
    {"rfid", nab_rfid},
    {"ear_move", nab_ear_move},
    {"ear_stop", nab_ear_stop},
    {"ear_pos", nab_ear_pos},
    {"sci", nab_sci},
    {"sciw", nab_sciw},
    {NULL, NULL},
};

static int luaopen_nab(lua_State *L)
{
  luaL_newlib(L, nab_funcs);
  return 1;
}

/* ---- Lua runtime --------------------------------------------------------- */
/* Trimmed stdlib for the 124 KB flash budget (see the Makefile's LUA_LIB note):
 * base + string + table only. Dropped: math (pulls ~16 KB of libm trig),
 * io/os/package/debug/loadlib (no filesystem, OS, or dynamic loading on this
 * target), coroutine, and utf8. base's dofile/loadfile are removed in
 * lua/lbaselib.c. This is the largest set that fits; drop string or table to
 * make room for another (e.g. coroutine or forced float printf). */
static const luaL_Reg loadedlibs[] = {
    {LUA_GNAME, luaopen_base},
    {LUA_TABLIBNAME, luaopen_table},
    {LUA_STRLIBNAME, luaopen_string},
    {"nab", luaopen_nab},   /* LEDs + button + audio + RFID + ears */
    {NULL, NULL},
};

static void open_trimmed_libs(lua_State *L)
{
  for (const luaL_Reg *lib = loadedlibs; lib->func != NULL; lib++) {
    luaL_requiref(L, lib->name, lib->func, 1);
    lua_pop(L, 1); /* remove the library table left on the stack */
  }
}

/* Print and clear a Lua error message sitting on the top of the stack. */
static void report(lua_State *L)
{
  const char *msg = lua_tostring(L, -1);
  sh_puts(msg ? msg : "(error with no message)");
  sh_puts("\n");
  lua_pop(L, 1);
}

/* Resident boot chunk: the M5 nab-binding demo helpers + an idle LED state,
 * defined at startup so the interpreter proves out with no console input (sim)
 * and the demo is one `run()` away on hardware. It does NOT auto-call run()
 * (a while-true RFID loop that only returns on a head-button press - calling it
 * at boot would strand the REPL behind a physical button, #207).
 *
 * This image has no on-device parser (#128), so the chunk cannot be compiled
 * from source at boot. The build compiles src/app/boot.lua off-device
 * (tools/luac/embed.py) into gen/boot_lc.h - a `boot_lc[]` bytecode blob loaded
 * below via luaL_loadbuffer (sizeof boot_lc = chunk length). Edit boot.lua, not
 * this header. */
#include "boot_lc.h"

#define REPL_LINE 256

/* ---- off-device luac bytecode frames ------------------------------------- */
/* This image drops lparser/llex/lcode (~18.9 KB, #128), so it can ONLY load
 * bytecode - it cannot compile source on-device. Host-side luac (tools/luac)
 * compiles every REPL line off-device and ships the chunk here as a framed hex
 * payload:
 *
 *     #LC:<len>\n            header line (len = chunk size in bytes, decimal)
 *     <2*len hex chars>      the chunk, whitespace/newlines ignored (wrapped 64c)
 *
 * Raw bytecode can't ride this line-oriented console directly (chunks contain
 * '\n'/NUL and sh_gets is line-based), hence the hex framing. #LC frames are the
 * only executable input the REPL accepts; anything else is rejected (see repl). */
#define LC_MAX 65536   /* sanity cap on a single bytecode chunk */

/* Read the next hex digit off the console, skipping the whitespace the sender
 * uses to wrap the payload. Returns 0..15, or -1 on EOF / a non-hex byte. */
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
 * stream; dropping it here keeps the following sh_gets from reading that '\n'
 * as a spurious empty REPL line (which would double every prompt). */
static void skip_to_eol(void)
{
  char c;
  while (_read(0, &c, 1) == 1) {
    if (c == '\n')
      break;
  }
}

/* Parse a "#LC:<len>" header (already read into `line`), stream the following
 * 2*len hex chars into a fresh buffer, and load it as a Lua chunk. Leaves the
 * compiled chunk on the stack on success (like load_line), else pushes an error
 * message and returns non-LUA_OK. No strtol - a manual digit loop keeps newlib
 * out. The buffer comes from the external-RAM heap (_sbrk). */
static int load_lc_frame(lua_State *L, const char *line)
{
  const char *p = line + 4; /* past "#LC:" */
  if (*p < '0' || *p > '9') {
    lua_pushliteral(L, "malformed #LC frame header");
    return LUA_ERRSYNTAX;
  }
  long len = 0;
  for (; *p >= '0' && *p <= '9'; p++) {
    len = len * 10 + (*p - '0');
    if (len > LC_MAX) {
      lua_pushliteral(L, "#LC frame too large");
      return LUA_ERRSYNTAX;
    }
  }

  char *buf = (len > 0) ? malloc((size_t)len) : NULL;
  if (len > 0 && buf == NULL) {
    lua_pushliteral(L, "out of memory reading #LC frame");
    return LUA_ERRMEM;
  }
  for (long i = 0; i < len; i++) {
    int hi = read_hex_nibble();
    int lo = read_hex_nibble();
    if (hi < 0 || lo < 0) {
      free(buf);
      lua_pushliteral(L, "truncated or non-hex #LC frame payload");
      return LUA_ERRSYNTAX;
    }
    buf[i] = (char)((hi << 4) | lo);
  }
  skip_to_eol(); /* drop the payload's trailing newline (see skip_to_eol) */

  /* "=stdin" chunkname matches the host pipe's luaL_loadbuffer name. The chunk
   * starts with LUA_SIGNATURE, so lua_load takes the lundump (bytecode) branch;
   * a non-bytecode payload would hit the guarded f_parser text branch and error
   * (there is no parser in this image, #128). */
  int status = luaL_loadbuffer(L, buf, (size_t)len, "=stdin");
  free(buf);
  return status;
}

/* Run a compiled chunk sitting on top of the stack - from either a source line
 * or a #LC frame - by pcall + echoing any returned values through print(),
 * exactly as the stock `lua` prompt does. Shared by both input paths so their
 * transcripts stay byte-identical (the point of the round-trip test). */
static void run_chunk(lua_State *L)
{
  int base = lua_gettop(L) - 1; /* stack height below the chunk */
  if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
    report(L);
  } else {
    int nres = lua_gettop(L) - base; /* values the chunk returned */
    if (nres > 0) {                  /* echo them via print() */
      lua_getglobal(L, "print");
      lua_insert(L, base + 1);
      if (lua_pcall(L, nres, 0, 0) != LUA_OK)
        report(L);
    }
  }
}

static void repl(lua_State *L)
{
  char line[REPL_LINE];
  sh_puts("> ");
  while (sh_gets(line, sizeof line) != NULL) {
    if (line[0] == '\n' || line[0] == '\0') {
      /* blank line: no-op, just re-prompt (matches the stock lua prompt) */
    } else if (line[0] == '#' && line[1] == 'L' && line[2] == 'C' && line[3] == ':') {
      if (load_lc_frame(L, line) != LUA_OK) /* off-device luac bytecode */
        report(L);                          /* frame error */
      else
        run_chunk(L);
    } else {
      /* Bytecode-only build (#128): no on-device parser. Source is compiled
       * off-device (tools/luac) and sent as an #LC frame - see luash.py. */
      sh_puts("bytecode-only build: send #LC frames (see tools/luac)\n");
    }
    sh_puts("> ");
  }
}

/* Bring up the LED bus + button for the nab bindings. LED init mirrors blink.c
 * (the LLC2_4c LED-only subset of the firmware's init_io). */
static void init_hw(void)
{
  CS_LED_AS_OUTPUT;
  MODE_LED_AS_OUTPUT;
  CS_LED_SET;
  MODE_LED_CLEAR;
  init_spi();
  init_led_rgb_driver();
  init_irq();    /* interrupt controller + the 1 ms tick (init_tick): */
  init_tick();   /* counter_timer, the clock the wifi stack runs on */
  init_button();
  init_vlsi();   /* VS1003 audio codec on SPI0, for nab.beep/volume */
  init_adc();    /* ADC ch.2 (PD2), for nab.wheel() */
  init_i2c();    /* I2C bus, for the CRX14 RFID coupler / nab.rfid() */
  init_ears();   /* FTM PWM + encoder timers, for nab.ear_* */
}

int main(void)
{
  init_uart();                      /* console up first (#207): _read/_write, sh_puts */
  init_hw();                        /* LEDs + button, for the nab bindings */

  lua_State *L = luaL_newstate();
  if (L == NULL) {
    sh_puts("lua: cannot create state (out of memory)\n");
    for (;;) {
    }
  }
  open_trimmed_libs(L);

  /* Load + run the resident boot chunk (precompiled bytecode; see boot_lc.h). */
  if (luaL_loadbuffer(L, (const char *)boot_lc, sizeof boot_lc, "=boot") != LUA_OK
      || lua_pcall(L, 0, 0, 0) != LUA_OK)
    report(L);

  repl(L);

  lua_close(L);
  sh_puts("<<FV_DONE>>\n");   /* tell flash.py the run is done (early-exit) */
  for (;;) {
  } /* bare metal: main never returns */
  return 0;
}
