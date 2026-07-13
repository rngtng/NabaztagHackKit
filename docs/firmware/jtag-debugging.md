# Observing & driving the Nabaztag:tag V2 over JTAG

Once the firmware is flashed (see
[`mtl/tools/openocd/README.md`](../../mtl/tools/openocd/README.md) for the
debrick/flash procedure), JTAG is also the only way to *inspect* a running board
— read C variables, capture the boot console, back up/restore the config sector.
This unit has **no serial console** (UART pads unpopulated; a serial line needs
the `mod_serial` mod), so JTAG carries everything.

The recipes below assume OpenOCD is already running against the board on
`localhost:3333` (GDB) / `4444` (telnet). Adjust the ELF path
(`mtl/build/firmware/Nab.elf`) and host as needed.

---

## First rule: don't flash-debug static questions

Most "what does the device do here" questions are answerable by *reading* source,
not by halting hardware. Reserve JTAG for C-runtime and hardware questions.

| Question | Best tool | Not |
|---|---|---|
| "What does the boot/app bytecode do?" (config gate, master mode, LED meaning) | read the `.mtl` in `mtl/boot/` / `mtl/apps/*/` | flash + breakpoint to infer it |
| "What's a valid config / SSID format?" | `mtl/tools/mtl_linux/config.txt`, the `conf.bin` sample | guess |
| "Does bytecode logic Z work?" | `task mtl:boot:simulate` / `<app>:simulate` (host, HW stubbed) | on-device |
| "How far did boot get / what's it printing?" | console-over-JTAG (`putst_uart`, below) | LED-guessing |
| "Is a C value X?" (`rt2501_connected`, button, config bytes) | `print` / `x` over JTAG | `finish` |
| "Does C path Y run in real time?" (USB enum, WiFi connect) | **single** breakpoint, run free, timeout | multi-breakpoint or console capture (both distort timing) |
| Hardware present/alive? (codec, WiFi module) | physical check + one targeted breakpoint | assume it's software |

---

## One-shot register / variable reads (no script file)

Chain `-ex` directly. `maint set target-async off` is required or `continue`
returns immediately and later commands fail with "target running".

```bash
gdb-multiarch -q -nx mtl/build/firmware/Nab.elf \
  -ex 'maint set target-async off' \
  -ex 'target extended-remote localhost:3333' \
  -ex 'monitor reset halt' \
  -ex 'x/1xb 0xB7A03004' \
  -ex 'monitor reset run' \
  -ex 'detach' -ex 'quit'
```

- `0xB7A03004` — button GPIO byte. Bit `0x02`: **SET = released (`0x0f`), CLEAR = pressed (`0x09`)**.
- RAM symbols (`rt2501_connected`, `master`, …) are only valid **mid-run**, not at reset-halt — see below.

## Reading mid-run variables (breakpoint required)

`monitor halt` / Ctrl-C **time out** on this bit-bang ARM7 — only `reset halt`
and breakpoint-stop work. Anchor on a symbol that runs every main-loop iteration
and is **not** inlined at `-O1` (`interpGo` is reliable; tiny functions like
`usbhost_events`/`hcd_rh_events` are inlined and their breakpoints never fire).

```bash
gdb-multiarch -q -nx mtl/build/firmware/Nab.elf \
  -ex 'maint set target-async off' \
  -ex 'target extended-remote localhost:3333' \
  -ex 'hbreak interpGo' \
  -ex 'commands' \
  -ex 'printf "master=%d rt2501=%d\n", master, rt2501_connected' \
  -ex 'continue' \
  -ex 'end' \
  -ex 'monitor reset run'
```

`silent` inside a `commands` block is rejected by gdb 7.x in batch mode — omit
it, the `printf` still runs.

## Console-over-JTAG

`consolestr(...)` is a macro; the real function is **`putst_uart`**
(`mtl/firmware/src/vm/vlog.c`), taking the string in `r0`. Break on it to read
the boot narrative without a UART line:

```gdb
maint set target-async off
target extended-remote localhost:3333
monitor reset halt
hbreak putst_uart
commands
  printf "LOG> %s\n", $r0
  continue
end
continue
```

**Caveat:** halting per string starves real-time USB — enumeration won't
complete under this capture. Use it for the boot narrative, not for
timing-sensitive USB/WiFi.

## OHCI USB host state

The OHCI controller runs autonomously, so these registers reflect real
electrical state even at a CPU halt. Base `0xF0000000` (`USB_REG_BASE_ADDR`);
offsets from `mtl/firmware/sys/inc/ml60842.h`:

- `0xF0000104` HcControl — operational if `&0xC0 == 0x80`
- `0xF0000150` HcRhStatus — bit0 LPS
- `0xF0000154` HcRhPortStatus — bit0 CCS (device present), bit1 PES, bit8 PPS (port power)
- `0xF0000148` HcRhDescriptorA

Split "USB port not powered" vs "dongle dead": if **PPS stays 0** the port never
got powered (clock/VBUS/port-power init — firmware or board fault); if **PPS=1
but CCS=0** the port is powered with no device (dongle hardware — reseat, check
VBUS, verify a supported VID:PID `0x148f:2573` / `0x0df6:9712` / `0x0db0:6877`,
or replace).

## Config backup / restore (flash sector `0x0801F000`)

The device config (WiFi SSID/key, server URL, login/pwd — ~208 bytes, magic byte
`0x47` at offset 207) lives in the **last 4 KB flash sector** `0x0801F000`
(`mtl/firmware/src/utils/mem.c` + linker script). The **MAC is not here** — it
comes from the dongle EEPROM (`netMac` → `rt2501_mac`).

```gdb
# Back up the sector
monitor dump_image cfg.bin 0x0801F000 0x1000

# Restore it (auto-erase touches only this sector; firmware untouched)
target extended-remote localhost:3333
monitor reset halt
monitor flash write_image erase cfg.bin 0x0801F000 bin
monitor verify_image cfg.bin 0x0801F000 bin
monitor reset run
```

---

## JTAG / gdb gotchas (read before debugging)

- **`continue` is async by default** → returns immediately, later commands fail
  with "target running". Fix: `maint set target-async off` (required for batch).
- **Only 2 hardware breakpoint units; software breakpoints don't work in flash.**
  Max 2 `hbreak`. `finish` and inferior calls (`print fn()`) **fail** ("Remote
  failure reply: 0E") — they need a 3rd unit. Read state with `print`/`x`, not
  `finish`.
- **Stale HW breakpoints persist across gdb sessions** and eat the 2 units. A
  bare `mon reset` does **not** clear the EmbeddedICE breakpoint regs, so after a
  killed session the device can halt at a ghost breakpoint. Restart OpenOCD to
  clear them — kill with `sudo pkill -x openocd` (**not** `pkill -f openocd`,
  which matches your own ssh command line and kills the session → exit 255).
- **Bit-bang is slow: each `continue`/halt cycle ≈ 10–15 s.** Budget ~3–4
  `continue`s before a 45–60 s `timeout` fires (exit 124). Read what you need in
  the fewest halts.
- **Tiny functions are inlined at `-O1`** → `hbreak <symbol>` never fires. Use a
  known-not-inlined, once-per-loop symbol like `interpGo` as the loop anchor.
- **`monitor reset halt` resets the core** → you **cannot back-trace an existing
  freeze**; re-run and catch it going forward (breakpoint or ignore-count).
- **Async halt (Ctrl-C / `monitor halt`) times out** on this ARM7 bit-bang —
  only breakpoint-stop and `reset halt` work.
- **`connect_assert_srst`**: every gdb attach asserts reset (ears stop, LEDs
  off). Normal, but a running device can't be inspected without resetting first.
- **`compare-sections`**: `.text`/`.rodata` mismatch = bad flash. A `.data`
  mismatch *after the program ran* is benign (RW globals mutated) — compare right
  after `load`, before run. "matched" only proves flash == the ELF gdb loaded,
  not that it's your latest build — confirm the ELF you copied over is current (a
  stale transfer that silently skipped will "match" the old firmware).
- **Watchpoints don't work** on this JTAG setup.
- **`exit 0` from a piped gdb is the *pipe's* exit, not gdb's.** Capture
  separately: `gdb ... > out; echo $?` (124 = timeout = breakpoint never fired).
- **LED colour can't be read over JTAG** — a human must look. Some Waitrony LED
  batches ship green/blue swapped (`mtl/firmware/src/hal/led.c`), which would map
  blue→green / yellow→purple / amber→magenta; trust the bytecode colour constant
  and confirm against the physical unit. See
  [`../boot/boot-led-states.md`](../boot/boot-led-states.md).
