# Debug strategy & capabilities — Nabaztag:tag V2 (ML67Q4051)

Companion to [`boot/boot-led-states.md`](boot/boot-led-states.md) (LED/motor → bytecode map).
That doc decodes *what the device shows*; this one captures *how we observe and
drive it*, the gotchas, and an efficient plan. Goal of the whole effort: get the
bunny onto the user's **WPA2** network (the PMK fixes `308f711`/`817b938` are in
but untested on-device).

---

## TL;DR — current state (2026-06-26, session 4) — **FULLY WORKING**

### FIXED session 1
- **Boot hang** — boot bytecode signed (`"amber"` wrapper). Fixed: `main.c` skip-header guard + unsigned `dumpbc`.
- **Config erased** — restored from `~/nabgcc/cfg.bin` to `0x0801F000`.

### FIXED session 2 — **RT2501 USB works. `rt2501_connected=1` confirmed.**
- **Root cause:** `CFLAGS += -fdata-sections` generates per-variable `.bss.varname` sections.
  Linker script `sys/ml67q4051.ld` only had `*(.bss)` — `.bss.usbhost_init_status` / `.bss.hcd_info`
  landed as orphans outside `[__bss_start__,__bss_end__]`, never zeroed by startup `LoopZI`.
  On every reset `usbhost_init_status` kept value `1` → `usbhost_init()` short-circuited →
  OHCI never initialized → RT2501 never enumerated → WiFi blocked → WDT crashloop.
- **Fix:** `*(.bss.*)` added to `sys/ml67q4051.ld` (commit `423e43f`). `rt2501_connected=1` confirmed.
- RT2501 is hardwired on PCB. WDT active (`wdt_start()` in `main.c:224`, 2.09s timeout).

### FIXED session 3 (debug detour) — button / latch investigation
- Discovered `push_button_value()` latch (`counter_timer_s<3? 1 : 0`) was commented out.
  Temporarily re-enabled with real-GPIO fallback to unblock config-AP testing.
- Confirmed via JTAG: button GPIO `0xb7a03004` = `0x0f` released / `0x09` pressed. Hardware fine.
- RT2501 `rt2501_connect` callback fires on cold boot.
- **Reverted before session 4** — latch back to commented-out (real GPIO only). WDT restored.

### FIXED session 4 — **WiFi WPA2 connects. Device fully operational.**
- WDT re-enabled + latch reverted → device boots normally → WiFi connects to `g71 gast` (WPA2).
- The original WDT crashloop was entirely the USB/BSS bug (session 2). Once USB fixed, WDT causes
  no issues — the main loop kicks it continuously during WiFi operations.
- PMK fixes (`308f711`/`817b938`) confirmed working on-device.
- **Device is now fully functional.** Config AP, WiFi connect, LED states all nominal.

### Current build state
- WDT: **enabled** (`wdt_start()` at `main.c:224`, 2.09s)
- `push_button_value()`: **real GPIO only** (latch commented out) — hold button at boot to enter config AP
- Config AP entry: hold button at power-on, release < 7s → blue → config AP
- Factory test: hold > 7s → `tests=1` (off), press → `tests=2` (yellow + motors), etc.
- USB / WiFi: working. WPA2 to `g71 gast` connects.

---

## Remote debugging (Claude via SSH)

Claude can drive JTAG directly via `ssh tobi@jtag.local`. Hard-won gotchas for efficiency:

### Start OpenOCD

**Must run from `~/nabgcc/openocd/` dir** — `source [find target/ml67q4051.cfg]` resolves relative to CWD:

```bash
ssh tobi@jtag.local "cd ~/nabgcc/openocd && sudo /usr/local/bin/openocd -f nabaztag-pi.cfg > /tmp/openocd.log 2>&1 &"
sleep 6
ssh tobi@jtag.local "cat /tmp/openocd.log"
# Expect: "JTAG tap: ml67q4051.cpu tap/device found: 0x3f0f0f0f"
```

Do NOT use `-f openocd/target/ml67q4051.cfg` on the command line — that path is wrong for the old OpenOCD 0.8.0 `[find]` resolver.

### One-shot variable/register reads (no script file needed)

Use `-ex` chains directly; avoid `-x scriptfile` for simple reads since the old gdb rejects `silent` inside `commands` blocks in batch mode:

```bash
ssh tobi@jtag.local "timeout 25 gdb-multiarch -q -nx ~/nabgcc/Nab.elf \
  -ex 'maint set target-async off' \
  -ex 'target extended-remote localhost:3333' \
  -ex 'monitor reset halt' \
  -ex 'x/1xb 0xB7A03004' \
  -ex 'print master' \
  -ex 'monitor reset run' \
  -ex 'detach' -ex 'quit' 2>&1"
```

Key addresses/symbols readable at `reset halt` (before program runs):
- `0xB7A03004` — button GPIO byte. Bit `0x02`: **SET=released (`0x0f`), CLEAR=pressed (`0x09`)**
- `master`, `rt2501_connected`, `start` — RAM symbols (valid only mid-run, not at reset halt)

### Read mid-run variables (breakpoint required — `monitor halt` does NOT work)

`async halt` times out on this bit-bang ARM7. Only `reset halt` and breakpoint-stop work. Use `hbreak interpGo` as the loop anchor (confirmed not inlined at `-O1`):

```bash
# Read master + rt2501_connected on each main-loop iteration (few iterations before timeout)
ssh tobi@jtag.local "timeout 35 gdb-multiarch -q -nx ~/nabgcc/Nab.elf \
  -ex 'maint set target-async off' \
  -ex 'target extended-remote localhost:3333' \
  -ex 'hbreak interpGo' \
  -ex 'commands' \
  -ex 'printf \"master=%d rt2501=%d\\n\", master, rt2501_connected' \
  -ex 'continue' \
  -ex 'end' \
  -ex 'monitor reset run' 2>&1"
```

Note: `silent` inside `commands` is rejected by gdb 7.x in batch — omit it, the printf still runs.

### Flash

```bash
# Build on dev host
cd /Users/tobias.bielohlawek/projects/nabaztag/nabgcc && task compile
# Sync (always --checksum; rsync -a silently skips unchanged mtime)
rsync --checksum -avz bin/Nab.elf gdb_load tobi@jtag.local:~/nabgcc/
# Verify md5 match before flashing
ssh tobi@jtag.local "md5sum ~/nabgcc/Nab.elf"
md5 -q bin/Nab.elf
# Flash
ssh tobi@jtag.local "cd ~/nabgcc && timeout 60 gdb-multiarch -q -nx Nab.elf -x gdb_load 2>&1"
```

### Kill stale OpenOCD

```bash
ssh tobi@jtag.local "sudo pkill -x openocd"
# NOT: pkill -f openocd  (matches your own ssh command string → kills the session)
```

---

## What we can observe & drive (access established this session)

| Capability | How | Notes |
|---|---|---|
| **Build** | `task compile` (Docker toolchain on the dev host) | clean rebuild needs `task clean` first — changing Makefile `OPTIONS` does **not** invalidate `.o` (flag changes silently don't recompile). |
| **Flash + run** | `ssh tobi@jtag.local`, `gdb-multiarch bin/Nab.elf -x gdb_load` | Pi (Model A+) is a bit-bang JTAG adapter. Passwordless sudo. OpenOCD 0.8.0 + OKI patch. |
| **Push files** | `rsync -avz --relative bin/Nab.elf gdb_load gdb_debug openocd/... tobi@jtag.local:~/nabgcc/` | dev host → Pi. |
| **Interactive debug** | `gdb-multiarch bin/Nab.elf -x gdb_debug` | flash, halt at reset, stay attached. |
| **Read console over JTAG** | breakpoint `putst_uart`, dump `$r0` each hit (see below) | **Contradicts the old "LED is the only signal" claim** — we *can* read the firmware console without a UART line. |
| **Read any state** | `print rt2501_connected`, `x/1xb 0xB7A03004` (button), `x` config sector `0x0801F000` | symbols from the matching ELF. |
| **Read OHCI USB host** | `x/1xw 0xF0000104` HcControl (oper if `&0xC0==0x80`), `0xF0000150` HcRhStatus (bit0 LPS), `0xF0000154` HcRhPortStatus (bit0 CCS=device, bit1 PES, bit8 PPS=port-power), `0xF0000148` HcRhDescriptorA | base `0xF0000000` (`USB_REG_BASE_ADDR`), offsets from `sys/inc/ml60842.h`. OHCI runs autonomously, so these reflect electrical state even at a CPU halt. |
| **Diagnostic gdb scripts** (repo + Pi `~/nabgcc/`) | `gdb_boot` (loaderInit→interpGo), `gdb_console` (boot log), `gdb_ohci` (USB/OHCI health), `gdb_diag` (button+rt2501_connect) | `timeout N gdb-multiarch -q -nx bin/Nab.elf -x <script>`. Run only ONE hbreak at a time (2-unit limit). **Watchpoints don't work on this JTAG setup.** |
| **Host bytecode run** | `task vm` (native `testVM`, HW stubbed) | fast bytecode iteration, no flash. |
| **Bytecode SOURCE** | `mtl/boot/boot.0.0.0.13.mtl` (VLISP, readable) + `mtl/mtl_linux/{conf.bin,config.txt}` | every "what does the VM do" question is static — **read, don't flash**. |

### Console-over-JTAG recipe
`consolestr(...)` is a macro → real fn is **`putst_uart`** (arg = string in `r0`).
```gdb
maint set target-async off
target extended-remote localhost:3333
monitor reset halt
hbreak putst_uart
commands
  silent
  printf "LOG> %s\n", $r0
  continue
end
continue
```
Caveat: **halting per string starves real-time USB** — enumeration won't complete
under this capture. Use it for the boot narrative, not for timing-sensitive USB/WiFi.

### Config backup / restore recipe (flash sector `0x0801F000`)
The 208-byte device config lives in the **last 4 KB flash sector** `0x0801F000`
(`src/utils/mem.c`, linker script). Layout in `boot.0.0.0.13.mtl:202-232`; magic
byte `0x47` at offset 207. A full-sector backup is on the Pi: `~/nabgcc/cfg.bin`
(4096 B = current network: server, SSID `g71 gast`, WPA2, PMK). To restore over
JTAG (OpenOCD running):
```gdb
gdb-multiarch -q -nx -batch \
  -ex "target extended-remote localhost:3333" \
  -ex "monitor reset halt" \
  -ex "monitor flash write_image erase /home/tobi/nabgcc/cfg.bin 0x0801F000 bin" \
  -ex "monitor verify_image /home/tobi/nabgcc/cfg.bin 0x0801F000 bin" \
  -ex "monitor reset run" -ex "detach"
```
`auto erase` handles the sector; only `0x0801F000` is touched (firmware untouched).
To re-dump a backup: `monitor dump_image cfg.bin 0x0801F000 0x1000`. **Note:** the
MAC is NOT here — it comes from the dongle EEPROM (`netMac`→`rt2501_mac`).

---

## JTAG / gdb gotchas (hard-won — read before debugging)

- **`continue` is async by default** → it returns immediately and later commands
  fail with "target running". Fix: **`maint set target-async off`** makes
  `continue` block until a breakpoint (required for batch scripts).
- **Only 2 hardware breakpoint units; software breakpoints don't work in flash.**
  So max 2 `hbreak`. `finish` / inferior calls (`print fn()`) **fail** ("Remote
  failure reply: 0E") because they need a 3rd unit. Read state via `print`/`x`,
  not `finish`.
- **Stale HW breakpoints persist** across gdb sessions and eat the 2 units.
  Restart OpenOCD between runs to clear them (a bare `mon reset` does **not**
  clear the EmbeddedICE breakpoint regs → after a killed session the device can
  halt at a ghost breakpoint instead of running). Kill with `sudo pkill -x
  openocd` — **not** `-f openocd` (that matches your own ssh command line
  containing "openocd" and kills the session → ssh exit 255).
- **Bit-bang is SLOW: each `continue`/halt cycle ≈ 10–15 s.** Budget ~3–4
  `continue`s before a 45–60 s `timeout` fires (124). Read what you need in the
  fewest halts; don't script 8 continues.
- **Tiny functions are inlined at `-O1`** (`usbhost_events`, `hcd_rh_events`, …) →
  `hbreak <symbol>` **never fires** even though the code runs inline. Use a
  known-not-inlined repeating symbol like `interpGo` (called once per main-loop
  iteration) as your loop anchor instead.
- **ELF on the Pi MUST match what's flashed**, and **`rsync -a` can silently skip
  the transfer** (seen this session — stale ELF stayed, so the *old* firmware got
  re-flashed and "matched", wasting a full cycle). Force with `rsync --checksum`
  and confirm `md5sum` local==remote before `gdb_load`. After `load`,
  `compare-sections` "matched" only proves flash==the-ELF-gdb-loaded, not that
  it's your new build.
- **`monitor reset halt` resets the core** → you **cannot back-trace an existing
  freeze**; you re-run and catch it going forward (breakpoint or ignore-count).
- **Async halt (Ctrl-C / `monitor halt`) times out** on this ARM7 bit-bang — only
  breakpoint-stop and `reset halt` work.
- **`connect_assert_srst`**: every gdb attach asserts reset (ears stop, LEDs off).
  That's normal, but it means a running device can't be inspected without resetting.
- **`compare-sections`**: `.text`/`.rodata` mismatch = bad flash; **`.data`
  mismatch after the program ran is benign** (RW globals mutated) — verify by
  comparing right after `load`, before run.
- **`exit 0` from a piped gdb is the *grep's* exit, not gdb's.** Capture
  `gdb ... > out; echo $?` separately (124 = timeout = breakpoint never fired).
- **Eyeball-only signals**: LED colour can't be read remotely here — the user must
  look. A **Waitrony green/blue swap** (`led.c:83-84`) exists on some units and
  would map blue→green, yellow→purple, amber→magenta. **But this unit shows the
  master-mode `setleds 0xff` (blue) as actual BLUE** → the swap likely does *not*
  apply here. (So the earlier "purple" sightings were a *different* state/firmware,
  not swapped-blue — see discrepancies.) Trust the bytecode colour constant.

---

## Tool → question decision table (the efficiency answer)

Stop flash-debugging things that are static or readable elsewhere.

| Question | Best tool | Don't |
|---|---|---|
| "What does the VM do here?" (config gate, master, connect flow, LED meaning) | **Read `boot.0.0.0.13.mtl`** | …flash + breakpoint to infer it |
| "What's a valid config / SSID format?" | `mtl/mtl_linux/config.txt`, `conf.bin` | …guess |
| "How far did boot get / what's it printing?" | **console-over-JTAG** (`putst_uart`) | …LED-guess |
| "Is a C-level value X?" (`rt2501_connected`, button, config bytes) | `print` / `x` over JTAG | …`finish` |
| "Does C path Y run in real time?" (USB enum, WiFi connect) | **single** breakpoint, run free, timeout | …multi-breakpoint or console capture (both distort timing) |
| "Does bytecode logic Z work?" | **`task vm`** on host | …on-device |
| Hardware present/alive? (codec, WiFi module) | physical check + targeted breakpoint | …assume software |

---

## Resolved this session / remaining gaps

1. **RESOLVED — the boot hang was the bytecode blob FORMAT.** `dumpbc` was
   compiled **signed** → `"amber"` wrapper → `loaderInit` hung. Fixed (skip the
   13-byte header in `main.c`; or regenerate `dumpbc` unsigned). The inner
   bytecode *does* match `boot.0.0.0.13.mtl`. VM now boots.
2. **RESOLVED — button routing.** `push_button_value` (`main.c`) now returns
   "pressed" on the **first** VM read (`set master=button2`) then the real GPIO →
   `master=1`, brief release → **config/master mode** (all blue). The old `3 s
   latch` / `return 1` notes are superseded.
3. **RESOLVED — "purple + ears endless" was `tests==2`** (factory motor test),
   reached on *earlier* firmware via a button hold>7s + click (or an earlier flash)
   — not the current state. Current fixed firmware cold-boots to **all blue =
   master/config mode**, ears still. The WiFi module is **present** (the
   "disconnected" claim was wrong).
4. **RESOLVED — config erased, restored** from `~/nabgcc/cfg.bin` to `0x0801F000`
   (verified). Was not the boot blocker (still loops after restore). The MAC is
   the dongle's (`netMac`→`rt2501_mac`), never in the config sector.
5. **RESOLVED (session 2) — RT2501/USB now works.** Root cause was a linker
   script bug (`*(.bss)` missing `*(.bss.*)`) that left BSS variables outside the
   startup zeroing range, causing `usbhost_init_status` to persist across resets
   and skip `hcd_init()`. Fix: one line added to `sys/ml67q4051.ld` (commit `46985f1`).
   `rt2501_connected=1` confirmed over JTAG. WiFi connect attempt now in progress.
6. **OPEN — WiFi connection to `g71 gast` (WPA2)** — the original goal. Device is
   running with RT2501 alive; PMK is in config. Verify LED progresses past amber to
   green/amber-animation. Debug WPA2 if needed (see next steps above).

---

## Proper debug strategy (ordered)

**Phase 0 — split "USB port not powered" vs "dongle dead" (the only remaining blocker).**
Boot/bytecode/config are fixed; the device runs but the RT2501 never enumerates,
so every WiFi call blocks → 2 s WDT crashloop.
1. **Reflash a WDT-disabled build** (comment `wdt_start();` in `main.c`, ~`:218`) →
   the reset-loop becomes a stable freeze at the stuck point, so JTAG can inspect.
2. **Read the OHCI port in steady state** (free-run, then a single late breakpoint;
   `usbhost_events`/`hcd_rh_events` are inlined at `-O1`, so anchor on `interpGo`):
   `HcControl` (`0xF0000104`, operational if `&0xC0==0x80`), `HcRhStatus`
   (`0xF0000150`), `HcRhPortStatus` (`0xF0000154`: bit8 PPS power, bit0 CCS device).
   `hcd_init` (`hcd.c:90-100`) sets `HcControl=PLE|USB_OPER` + `HcRhStatus=LPSC`, so
   it *should* be operational + powered.
   - **`PPS` stays 0** → port never powered = USB clock / VBUS / port-power init
     (`usbctrl_init` `RstClkCtl` + VBUS, `hcd_init` LPSC) — firmware, or a board
     clock/VBUS fault.
   - **`PPS=1` but `CCS=0`** → port powered, no device = **dongle hardware**: reseat,
     check VBUS at the connector, verify supported VID:PID (`0x148f:2573`,
     `0x0df6:9712`, `0x0db0:6877`), or replace.
3. Fix accordingly → then the config-AP / WPA2 work resumes (Phase 3).

**Phase 1 — observability:**
4. **No serial on this unit.** A real UART would be ideal (115200 8N1, UART0, no
   USB-timing distortion), but the factory UART header is unpopulated and needs
   the `mod_serial` hardware mod (https://wk.redox.ws/dev/nab/v2/mod_serial),
   which we don't have. So **JTAG is the only console/observability path**: a
   *single* free-run breakpoint + static register reads for timing-sensitive
   questions; the `putst_uart` capture (below) only for the non-timing boot
   narrative, since it starves USB.

**Phase 2 — clean up the bytecode blob (optional, not the blocker):**
5. Regenerate `dumpbc` **UNSIGNED** (`task vm-compile`, `SIGN=false`) → bare
   bytecode (`0xf5 0x47`), so `loaderInit(&dumpbc)` works without the `main.c`
   skip-guard. Keep the guard anyway as a re-brick safety net (see
   [`architecture.md`](architecture.md)).

**Phase 3 — the original goal (WPA2):**
Config is already restored (server `192.168.178.33:59292`, SSID `g71 gast`, WPA2,
PMK — see restore recipe). Once the dongle enumerates: exercise the connect path,
read the WPA2 flow in the `.mtl` + `src/net/{ieee80211,eapol}.c` (PMK fixes
`308f711`/`817b938` are in), enable `DEBUG_WIFI` (alone — both debug sets overflow
the 128 KB ROM by ~1.8 KB) and capture MIC OK/NOK over the JTAG console.

**Cross-cutting:** for every new question, hit the decision table first. Reserve
flash+JTAG for C-runtime/hardware questions only.

---

## Build state & debug toggles

**Currently flashed (`debug2` branch, commit `43bd7ac`+):** WDT **enabled**,
bare unsigned `dumpbc` (no `"amber"` skip-guard needed), linker script fix
(`*(.bss.*)`) applied, RT2501 working. WiFi connect in progress.

**Diagnostic gdb scripts** committed to repo (`gdb_load`, `gdb_debug`, `gdb_boot`,
`gdb_console`, `gdb_ohci`, `gdb_diag`) — also on Pi `~/nabgcc/`. Run with
`timeout N gdb-multiarch -q -nx bin/Nab.elf -x <script>`.

**Toggles available when needed:** comment `wdt_start()` → freeze instead of
crashloop (useful for JTAG inspection); the `"amber"` skip-guard in `main.c`
(keep as re-brick safety if a signed `dumpbc` ever sneaks back); `Makefile OPTIONS`
`-DDEBUG_WIFI` **alone** for wifi capture (both DEBUG sets overflow 128 KB ROM by
~1.8 KB).
