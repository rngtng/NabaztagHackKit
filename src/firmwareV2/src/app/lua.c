/**
 * @file lua.c
 * @brief M4 bring-up app (issue #92): boot PUC-Rio Lua 5.4 and run a REPL.
 *
 * First binary that runs a real language runtime on the ML67Q4051. It brings up
 * the vendored Lua 5.4 core (see ../../lua/, PROVENANCE.md), opens a trimmed set
 * of standard libraries, runs an embedded demo chunk, then drops into a REPL.
 *
 * Console + heap, the two things a hosted Lua assumes but bare metal does not:
 *
 *   - Console: ARM semihosting (SWI 0x123456 / Thumb `svc 0xAB`) is the M3 (#91)
 *     no-UART console. The newlib syscalls _read/_write below route stdin/stdout
 *     through SYS_READC/SYS_WRITEC, so Lua's print() and the REPL prompt reach the
 *     GDB/OpenOCD console on hardware and the Unicorn simulator (#96) off hardware.
 *     On real hardware this needs M2's OpenOCD `arm semihosting enable`; until then
 *     the simulator is the console (it implements these same SWIs, see sim/).
 *
 *   - Heap: 16 KB of internal RAM is far too small for a Lua state, so _sbrk hands
 *     out the 1 MB external RAM window (0xD0000000) that init.s' EMC setup brings
 *     up. The linker's IntRAM heap (__heap_start__) is left unused here.
 *
 * DoD (#92): REPL reachable over the console; `print(1+1)` works. In the simulator
 * the embedded demo proves it; SYS_READC returns 0 (no input) so the REPL then
 * sees EOF and exits. On hardware the REPL reads real keystrokes over the console.
 */
#include <errno.h>
#include <stdarg.h>   /* vsnprintf override (M7.5, #114) */
#include <stddef.h>
#include <stdint.h>   /* uintptr_t in the %p path (M7.5, #114) */
#include <stdio.h>
#include <string.h>   /* strcmp - LED-name lookup in the nab binding */

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

/* M5 (#93) hardware bindings: LED driver + head button. */
#include "ml674061.h"
#include "common.h"
#include "hal/spi.h"
#include "hal/led.h"
#include "hal/button.h"
#include "hal/audio.h"

/* ---- ARM semihosting (the M3 #91 no-UART console) ------------------------ */
/* Thumb semihosting call: r0 = operation, r1 = parameter, result in r0. The
 * simulator (and OpenOCD/GDB on hardware) traps `svc 0xAB` and services it. */
#define SYS_WRITEC 0x03   /* write the single char at *r1                     */
#define SYS_READC  0x07   /* read one char, returned in r0 (0 = no input yet) */

static inline int semihost(int op, void *arg)
{
  register int r0 asm("r0") = op;
  register void *r1 asm("r1") = arg;
  asm volatile("svc #0xAB" : "+r"(r0) : "r"(r1) : "memory");
  return r0;
}

/* Raw console write, independent of stdio buffering - used for prompts/errors. */
static void sh_puts(const char *s)
{
  while (*s) {
    char c = *s++;
    semihost(SYS_WRITEC, &c);
  }
}

/* ---- newlib syscalls: stdout/stdin over semihosting, heap in ExtRAM ------- */
/* Our own definitions win over libnosys' stubs (object beats archive member). */

int _write(int fd, const char *ptr, int len)
{
  (void)fd;
  for (int i = 0; i < len; i++) {
    char c = ptr[i];
    semihost(SYS_WRITEC, &c);
  }
  return len;
}

int _read(int fd, char *ptr, int len)
{
  (void)fd;
  if (len <= 0)
    return 0;
  int c = semihost(SYS_READC, NULL);
  if (c <= 0)
    return 0;             /* no input (simulator) -> EOF, ends the REPL */
  ptr[0] = (char)c;
  return 1;               /* one char per call; sh_gets reassembles the line */
}

/* Read one line into buf (keeps the trailing '\n', NUL-terminates), built on
 * the single-char _read syscall. Replaces fgets() so the REPL needs no newlib
 * stdio FILE layer (M7.1, #107). Returns NULL on immediate EOF, like fgets. */
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

/* ---- Lua console output (M7.1, #107) ------------------------------------- */
/* luaconf.h routes lua_writestring/writeline/writestringerror here so print()
 * and the error/panic paths write straight to the semihosting _write syscall,
 * never linking newlib's buffered-FILE layer (~6 KB). */
void luai_writestring(const char *s, size_t l)
{
  _write(1, s, (int)l);
}

/* Every lua_writestringerror call site (lauxlib panic/warn) uses a "%s"-style
 * format with one const char* arg. snprintf is our own (M7.5, below). */
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

/* ---- compact vsnprintf/snprintf, overriding newlib (M7.5, #114) ---------- */
/* Overriding these strong symbols is the last flash-reclaim lever: Lua's number
 * formatting (lua_number2str/lua_integer2str/lua_pointer2str) and string.format
 * route through snprintf, and newlib's snprintf (_svfprintf_r) drags in the
 * buffered-FILE layer (~12 KB) via __sinit's CHECK_INIT. This implementation
 * needs no FILE machinery.
 *
 * Supports the conversions Lua emits: d i u o x X c s p % - with flags
 * (-+space 0 #), width and precision (both incl '*'), and length modifiers
 * (h/hh/l/ll/L/z/t/j, parsed; values are fetched as `long` since LUA_32BITS
 * makes lua_Integer and pointers 32-bit, so no 64-bit divide helpers are
 * pulled). Float conversions (f e g a, any case) stay approximate - integer
 * part + ".0" - float printing is still not properly supported (was already
 * stubbed pre-M7; a real dtoa is future work). */

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
        double dv = va_arg(ap, double);
        long iv = (long)dv;
        if (iv < 0 || (dv < 0 && iv == 0)) sign = '-';
        else sign = (flags & PF_PLUS) ? '+' : (flags & PF_SPACE) ? ' ' : 0;
        unsigned long uv = (iv < 0) ? (unsigned long)(-(iv + 1)) + 1 : (unsigned long)iv;
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

/* ---- decimal string -> Lua number (M7.2, #108) --------------------------- */
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
 * looser than strtof - acceptable here (integer-first target; float printing is
 * already stubbed, #92). */
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

/* ---- float ^ and % without libm (M7.3, #109) ----------------------------- */
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
  LUA_NUMBER n = (LUA_NUMBER)(long long)q;   /* truncate toward zero (C fmod) */
  LUA_NUMBER m = a - n * b;                  /* remainder, sign of a */
  if (m != 0 && ((m < 0) != (b < 0)))        /* sign differs from b -> floor */
    m += b;
  return m;
}

/* ---- abort: halt, don't raise(SIGABRT) (M7.4, #110) ---------------------- */
/* newlib's abort() calls raise() + _exit(), pulling the signal machinery
 * (raise/signal/_kill_r/_getpid). Bare metal has no OS to signal, so halt in
 * place. The other M7.4 targets (strerror/strstr via luaL_fileresult/gsub) were
 * already removed by --gc-sections once the io/os libs were excluded. */
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

/* ---- M5 hardware bindings (#93): the `nab` module ------------------------ */
/* Exposes the LEDs and head button to Lua. Ears (motors) need a timer/PWM/
 * encoder subsystem not yet in firmwareV2 - deferred. */

/* nab.led(name, r, g, b): light an RGB LED. name is one of
 * nose|belly|left|right|bottom (physical map verified on hardware, LLC2_4c #93 -
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

static const luaL_Reg nab_funcs[] = {
    {"led", nab_led},
    {"button", nab_button},
    {"volume", nab_volume},
    {"beep", nab_beep},
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
    {"nab", luaopen_nab},   /* M5 (#93): LEDs + button */
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

/* Embedded proof that the interpreter runs even with no console input (sim).
 * Also exercises the M5 nab bindings: lights the nose green and reads the
 * button - visible/reportable on hardware, harmless in the simulator. */
static const char DEMO[] =
    "print('firmwareV2 Lua ' .. _VERSION)\n"
    "print('1+1 =', 1 + 1)\n"
    "nab.led('nose', 0, 127, 0)\n"
    "print('button:', nab.button())\n";

#define REPL_LINE 256

/* Compile a REPL line, trying expression form ("return <line>") first - so a
 * bare expression like `2+3` evaluates and echoes - then falling back to
 * statement form (`x = 5`, `for ...`). Mirrors the stock `lua` prompt. Leaves the
 * compiled chunk on the stack on success, or an error message on failure. */
static int load_line(lua_State *L, const char *line)
{
  char expr[REPL_LINE + 8];
  snprintf(expr, sizeof expr, "return %s", line);
  if (luaL_loadstring(L, expr) == LUA_OK)
    return LUA_OK;
  lua_pop(L, 1); /* drop the "return ..." compile error, try as a statement */
  return luaL_loadstring(L, line);
}

static void repl(lua_State *L)
{
  char line[REPL_LINE];
  sh_puts("> ");
  while (sh_gets(line, sizeof line) != NULL) {
    if (load_line(L, line) != LUA_OK) {
      report(L); /* syntax error */
    } else {
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
  init_button();
  init_vlsi();   /* M8 (#116): VS1003 audio codec on SPI0, for nab.beep/volume */
}

int main(void)
{
  init_hw();                        /* LEDs + button, for the nab bindings */

  lua_State *L = luaL_newstate();
  if (L == NULL) {
    sh_puts("lua: cannot create state (out of memory)\n");
    for (;;) {
    }
  }
  open_trimmed_libs(L);

  if (luaL_dostring(L, DEMO) != LUA_OK)
    report(L);

  repl(L);

  lua_close(L);
  sh_puts("<<FV_DONE>>\n");   /* tell flash.py the run is done (early-exit) */
  for (;;) {
  } /* bare metal: main never returns */
  return 0;
}
