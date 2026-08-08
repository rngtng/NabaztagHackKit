/**
 * @file utils/luaseam.h
 * @brief The small shared pieces of the Lua<->C seam: bounded argument checks,
 *        the two pushes with a rule in them, and the error reporter (#326).
 *
 * Most of what `main.c` does at this seam is marshalling with no decision in
 * it - `luaL_checkinteger` into a HAL call - and #326's finding was that
 * extracting *that* would test `luaL_checkinteger`. What is in here is the
 * remainder: the handful of helpers that each carry a rule, are shared by more
 * than one binding, and were sitting in the one TU nothing can link.
 *
 * - **The bounds checks** (`luaseam_rgb`, `luaseam_bounded`,
 *   `luaseam_optbounded`, `luaseam_field`) are principle 5 in code: every
 *   `nab.*` argument that reaches a fixed-size C buffer is bounded AT THE
 *   SEAM, so no length or address is ever taken from Lua on trust. #296 found
 *   bugs in exactly this band.
 * - **`luaseam_push_uid`** is the one helper that genuinely crosses a file
 *   boundary: `nab.rfid` (main.c) and the event pump (utils/pump.c) must
 *   spell a UID the same way or the two paths report the same tag
 *   differently. It is one of the functions the compiler used to inline into
 *   both call sites; out of line and shared, it is cheaper, not dearer.
 * - **`luaseam_report`** is how a Lua error reaches the console. The pump
 *   needs it for the `lua_pcall` isolation rule, `main()` for a boot chunk
 *   that will not load, and the REPL for a chunk that raised.
 *
 * This TU deliberately holds NO device dependency: it is the Lua C API plus
 * `_write` (the console seam, `libc/syscalls.h`), so the day a host test links
 * the Lua core - #329 weighs that as its option (a), and defers it - these
 * become testable with no further moves. They are not testable before that
 * day, which is the honest reason they are still described here rather than
 * asserted anywhere.
 */
#ifndef _LUASEAM_H_
#define _LUASEAM_H_

#include <stddef.h>
#include <stdint.h>

#include "lua.h"

/**
 * @brief Print and pop the Lua error message on top of the stack.
 *
 * Writes through `_write` - the same console sink Lua's own `print()` uses -
 * so an error and the output around it stay in order on the UART.
 */
void luaseam_report(lua_State *L);

/**
 * @brief Args 2, 3, 4 as one 0xRRGGBB word, each channel bounded by `max`.
 *
 * @param max 127 for the raw TLC5922 range, 255 for the gamma bindings.
 * Raises a Lua error naming the offending argument if a channel is out of
 * range - it never returns a clamped value, because a silently clamped colour
 * is a bug that looks like a dim LED.
 */
uint32_t luaseam_rgb(lua_State *L, lua_Integer max);

/**
 * @brief A string argument that has to fit a fixed-size C buffer.
 *
 * The 802.11 SSID and the WPA passphrase both land in one - `rt2501_scan`'s
 * probe frame, the PBKDF2 - so the cap belongs at the seam. The HAL re-checks;
 * neither layer is the only guard.
 *
 * @param what the message shown when it does not fit, e.g. "at most 32 bytes".
 */
const char *luaseam_bounded(lua_State *L, int arg, size_t max,
                            const char *what);

/** @brief luaseam_bounded for an optional argument; `def` may be NULL. */
const char *luaseam_optbounded(lua_State *L, int arg, const char *def,
                               size_t max, const char *what);

/**
 * @brief Copy string field `k` of the table at `idx` into `dst` (max `cap`).
 *
 * Missing or nil becomes "". Raises a Lua error on a non-string value or one
 * that would overflow the field: `nab.config` owns the flash sector layout, so
 * its bounds are enforced here rather than trusted from Lua.
 */
void luaseam_field(lua_State *L, int idx, const char *k, char *dst, size_t cap);

/** @brief Push an 8-byte RFID UID as "a1b2c3d4e5f60708" (lowercase hex). */
void luaseam_push_uid(lua_State *L, const uint8_t uid[8]);

#endif /* _LUASEAM_H_ */
