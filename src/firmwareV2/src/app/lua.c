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
#include <stddef.h>
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
 * format with one const char* arg. snprintf uses the string vfprintf path
 * (already linked for lua_number2str), not the FILE layer. */
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

static const luaL_Reg nab_funcs[] = {
    {"led", nab_led},
    {"button", nab_button},
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
  for (;;) {
  } /* bare metal: main never returns */
  return 0;
}
