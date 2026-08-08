/**
 * @file luaseam.c
 * @brief The shared Lua<->C seam helpers lifted out of main.c (#326).
 *
 * See utils/luaseam.h for what belongs in here and what deliberately does not.
 * The short version: the `nab` bindings are mostly marshalling, and this is
 * the part of them that is not - the bounds enforced at the seam, the UID
 * spelling two callers must agree on, and the error reporter.
 */
#include <string.h>

#include "lauxlib.h"
#include "libc/syscalls.h" /* _write: the console sink, shared with print() */
#include "utils/fmt.h"     /* fmt_hex8 */
#include "utils/luaseam.h"

void luaseam_report(lua_State *L)
{
  static const char nomsg[] = "(error with no message)";
  size_t len;
  const char *msg = lua_tolstring(L, -1, &len);

  /* lua_tolstring hands back the length, so the console write needs no strlen -
   * and the fallback's is a compile-time constant. */
  if (msg == NULL) {
    msg = nomsg;
    len = sizeof nomsg - 1;
  }
  _write(1, msg, (int)len);
  _write(1, "\n", 1);
  lua_pop(L, 1);
}

uint32_t luaseam_rgb(lua_State *L, lua_Integer max)
{
  uint32_t rgb = 0;

  for (int i = 2; i <= 4; i++) {
    lua_Integer v = luaL_checkinteger(L, i);
    luaL_argcheck(L, v >= 0 && v <= max, i, max == 127 ? "0..127" : "0..255");
    rgb = (rgb << 8) | (uint32_t)v;
  }
  return rgb;
}

const char *luaseam_bounded(lua_State *L, int arg, size_t max,
                            const char *what)
{
  size_t len;
  const char *s = luaL_checklstring(L, arg, &len);

  luaL_argcheck(L, len <= max, arg, what);
  return s;
}

const char *luaseam_optbounded(lua_State *L, int arg, const char *def,
                               size_t max, const char *what)
{
  size_t len;
  const char *s = luaL_optlstring(L, arg, def, &len);

  if (s != NULL)
    luaL_argcheck(L, len <= max, arg, what);
  return s;
}

void luaseam_field(lua_State *L, int idx, const char *k, char *dst, size_t cap)
{
  lua_getfield(L, idx, k);
  size_t len = 0;
  const char *s = "";
  if (!lua_isnil(L, -1))
    s = lua_tolstring(L, -1, &len);
  if (s == NULL || len >= cap)
    luaL_error(L, "config: bad %s", k);
  memcpy(dst, s, len);
  dst[len] = '\0';
  lua_pop(L, 1);
}

void luaseam_push_uid(lua_State *L, const uint8_t uid[8])
{
  char hex[17];

  fmt_hex8(hex, uid);
  lua_pushstring(L, hex);
}
