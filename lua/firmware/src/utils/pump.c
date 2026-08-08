/**
 * @file pump.c
 * @brief Draining the C event queue into Lua callbacks (#329).
 *
 * Design principle 2 lands here: utils/event.c polls the hardware from the
 * cooperative pump (never an ISR) and queues edge events; this file delivers
 * them to Lua callbacks under lua_pcall. Dispatch runs while the REPL prompt
 * sits idle and inside nab.wait() - Lua code never runs behind the script's
 * back.
 *
 * Split out of main.c because it is the most rule-bearing code that was in
 * there: four rules, none of which any test could reach, and one of them with
 * a consequence (nab.wait inside the reactor) that a review found by reading
 * the control flow rather than by running anything. utils/pump.h states all
 * four; keep them and their reasons together.
 */
#include "lauxlib.h"

#include "event.h"
#include "utils/luaseam.h"
#include "utils/pump.h"

#define EVENTS_TABLE "nab.events"  /* registry key: {button=fn, rfid=fn} */

static const char *const event_names[] = {"button", "rfid", "tick", NULL};

void pump_init(lua_State *L)
{
  lua_newtable(L);
  lua_setfield(L, LUA_REGISTRYINDEX, EVENTS_TABLE);
}

void pump_dispatch(lua_State *L, uint8_t allow_rfid)
{
  static uint8_t busy;
  /* Sample the hardware ALWAYS, even when re-entered (rule 2). The guard
   * exists to stop recursive *Lua dispatch* (principle 2), and event_pump
   * touches no Lua state - the queue is precisely the buffer that decouples
   * the two. With the pump inside the guard, a nab.wait() called from a
   * callback stopped the debouncer and the scan cycle outright, so a
   * press+release entirely inside that window was never even observed, let
   * alone queued for later. */
  event_pump(allow_rfid);
  /* Rule 1: nested calls deliver nothing. Kept a function-local static rather
   * than a header extern on purpose - a guard callers can clear is not one. */
  if (busy)
    return;
  busy = 1;
  event_t e;
  /* The callback table is fetched once, not per event: it is the same table
   * throughout, and the registry lookup hashes EVENTS_TABLE every time. */
  lua_getfield(L, LUA_REGISTRYINDEX, EVENTS_TABLE);
  int cbs = lua_gettop(L);
  while (event_next(&e)) {
    lua_getfield(L, cbs,
                 (e.type == EV_RFID_TAG || e.type == EV_RFID_GONE) ? "rfid"
                                                                   : "button");
    if (!lua_isfunction(L, -1)) {
      lua_pop(L, 1); /* callback cleared after the event was queued */
      continue;
    }
    switch (e.type) {
      case EV_BUTTON_DOWN: lua_pushboolean(L, 1); break;
      case EV_BUTTON_UP:   lua_pushboolean(L, 0); break;
      case EV_RFID_TAG:    luaseam_push_uid(L, e.uid); break;
      default:             lua_pushnil(L); break; /* EV_RFID_GONE */
    }
    /* Rule 4: a raising callback prints and dispatch continues. */
    if (lua_pcall(L, 1, 0, 0) != LUA_OK)
      luaseam_report(L);
  }
  /* Rule 3 - the cooperative tick: once the C event queue is drained, hand the
   * Lua reactor a slice. This is the seam that lets behaviour which must keep
   * running during a blocking call - an ear stopping on its target, a net
   * connection being pumped - actually run, without the C layer knowing what
   * any of it is. Registered with nab.on("tick", fn); under lua_pcall like
   * every other callback (principle 3), and inside the busy guard, so a
   * nab.wait() from within the tick cannot recurse into dispatch. Reuses the
   * callback table already on the stack rather than hashing EVENTS_TABLE again. */
  lua_getfield(L, cbs, "tick");
  if (lua_isfunction(L, -1)) {
    if (lua_pcall(L, 0, 0, 0) != LUA_OK)
      luaseam_report(L);
  } else {
    lua_pop(L, 1);
  }
  lua_settop(L, cbs - 1); /* drop the callback table */
  busy = 0;
}

/* nab.on(name, fn|nil): register (or clear) the callback for an event source.
 * name "button": fn(pressed) on debounced press/release edges. name "rfid":
 * fn(uid) when a new tag lands on the coupler, fn(nil) when it leaves;
 * registering starts the background ~750 ms scan cycle, clearing stops it.
 * name "tick": fn() on every pump iteration, after the event queue is drained -
 * the seam the Lua reactor (sched) hangs off. It is not an event source:
 * nothing is queued for it and it carries no argument.
 * Callbacks fire from the cooperative pump - while the REPL prompt is idle or
 * inside nab.wait()/nab.delay() - never from an interrupt (principle 2).
 * ONE callback per name, replaced silently: there is no chaining and no
 * diagnostic, and boot/test/run.lua models that deliberately. */
int pump_on(lua_State *L)
{
  int which = luaL_checkoption(L, 1, NULL, event_names);
  int has_fn = !lua_isnoneornil(L, 2);
  if (has_fn)
    luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_settop(L, 2); /* materialize an absent arg 2 as nil */
  lua_getfield(L, LUA_REGISTRYINDEX, EVENTS_TABLE);
  lua_pushvalue(L, 2);
  lua_setfield(L, -2, event_names[which]);
  if (which == 1) /* "rfid" */
    event_rfid_enable((uint8_t)has_fn);
  return 0;
}
