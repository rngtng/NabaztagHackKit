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

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

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
  return 1;               /* one char per call; fgets reassembles the line */
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

/* Embedded proof that the interpreter runs even with no console input (sim). */
static const char DEMO[] =
    "print('firmwareV2 Lua ' .. _VERSION)\n"
    "print('1+1 =', 1 + 1)\n";

static void repl(lua_State *L)
{
  char line[256];
  sh_puts("> ");
  while (fgets(line, sizeof line, stdin) != NULL) {
    if (luaL_loadstring(L, line) == LUA_OK) {
      if (lua_pcall(L, 0, 0, 0) != LUA_OK)
        report(L);
    } else {
      report(L);
    }
    sh_puts("> ");
  }
}

int main(void)
{
  setvbuf(stdout, NULL, _IONBF, 0); /* semihosting console: no buffering */

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
