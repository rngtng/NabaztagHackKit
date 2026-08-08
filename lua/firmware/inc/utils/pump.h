/**
 * @file utils/pump.h
 * @brief The cooperative pump: draining the C event queue into Lua callbacks,
 *        and the reactor's tick slice (#329).
 *
 * `utils/event.c` owns the pollers and the queue; this sits directly on top of
 * it and is the only place C hands control to Lua. It lived in `main.c`, which
 * carries `main()` - and #329's point was that this is not an abstract cost:
 * the trap in rule 1 below went unnoticed until an architecture review read the
 * control flow by hand, *because* no test could reach the code.
 *
 * ## The four rules this file keeps
 *
 * 1. **A re-entrancy guard.** A callback that ends up back in
 *    `pump_dispatch()` - `nab.wait()` inside a `nab.on` handler, a spawned
 *    task, or a `sched` pump - must not dispatch recursively, or the stack
 *    gives out. Nested calls therefore deliver nothing.
 * 2. **The pollers run even when re-entered.** Deliberate and load-bearing:
 *    the guard exists to stop recursive *Lua* dispatch, and `event_pump()`
 *    touches no Lua state - the queue is precisely the buffer that decouples
 *    them. With the pump inside the guard, a `nab.wait()` from a callback
 *    stopped the debouncer and the scan cycle outright, so a press+release
 *    entirely inside that window was never even observed, let alone queued.
 * 3. **Drain, then tick.** The Lua reactor's slice runs after the C queue is
 *    empty, inside the guard.
 * 4. **`pcall` isolation.** A raising callback prints and dispatch continues
 *    (design principle 3); it never takes the runtime down.
 *
 * ## The consequence of rule 1, which callers must know
 *
 * Inside the guard nothing else is delivered. So `nab.wait(500)` called from a
 * `sched` pump, a spawned task or a `nab.on` callback is 500 ms in which no
 * other pump runs, no task resumes and no queued event arrives - the #283 hole
 * one level down. `nab.wait` is a TOP-LEVEL sleep; inside the reactor the
 * cooperative spellings are `sched.sleep(ms)` (from a task) or returning and
 * being called again (from a pump). `firmware/README.md`'s `nab` table and
 * `boot/test/run.lua`'s modelled seam both say so now, and the boot suite
 * fails if the guard stops being modelled.
 *
 * ## What stayed in main.c
 *
 * The `allow_rfid` decision. The REPL gates the ~5 ms coupler scan on the
 * console having been quiet (`CONSOLE_IDLE_MS`), because a scan mid-transfer
 * can overflow the 16-byte UART RX FIFO - an interlock between the pump and
 * the console, and the console side of it is the caller's. So the gate is
 * evaluated at the call site and passed in, exactly as before.
 */
#ifndef _PUMP_H_
#define _PUMP_H_

#include <stdint.h>

#include "lua.h"

/**
 * @brief Create the registry table `pump_on()` stores callbacks in.
 *
 * Call once, while opening the `nab` module. The registry key is private to
 * this file so nothing else can reach the table by name.
 */
void pump_init(lua_State *L);

/**
 * @brief Poll the hardware, deliver queued events, then run the reactor tick.
 *
 * @param allow_rfid pass the coupler scan through to `event_pump()`; see the
 *                   `CONSOLE_IDLE_MS` note above - the caller owns this.
 *
 * Re-entrant-safe but not re-entrant: a nested call polls the hardware and
 * returns without running any Lua (rules 1 and 2).
 */
void pump_dispatch(lua_State *L, uint8_t allow_rfid);

/** @brief The `nab.on(name, fn|nil)` binding. */
int pump_on(lua_State *L);

#endif /* _PUMP_H_ */
