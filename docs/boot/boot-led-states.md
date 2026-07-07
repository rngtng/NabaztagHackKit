# Boot LED & motor states — `mtl/boot/boot.0.0.0.13.mtl`

On-device the only diagnostic signal is **LED colour + ear motion** (no
serial/UART on this unit — needs the `mod_serial` mod). This maps every boot LED
state back to the bytecode that drives it, so a colour on the device tells you
exactly which code path is live.

The bytecode (`.mtl`) is VLISP/Metal **source**, not binary — readable. It
calls C HAL syscalls (`setleds`/`led`, `motorset`/`motorget`, `button2`) wired
in `src/vm/vlog.c` → `src/hal/{led,motor}.c`.

> **Current state (2026-06-26, session 4): FULLY WORKING.**
> WDT enabled. `push_button_value` = real GPIO only (hold button at boot for config AP).
> RT2501 USB works. WiFi WPA2 connects to `g71 gast`. Device fully operational.
> See [`debug-strategy.md`](debug-strategy.md) for full history.

---

## "all LEDs purple + ears turn endlessly" (historical / earlier firmware)

**State: factory motor self-test, `tests==2`.** Not a crash — deliberate burn-in.
Ears spin forever by design; a button click is the only exit. Seen on *earlier*
firmware; the current fixed build boots to all-blue config mode instead.

| Symptom | Cause |
|---|---|
| All LEDs purple | `coltests[2] == 0xffff00` (yellow), shown magenta only **if** this unit had the Waitrony **green/blue swap** (`src/hal/led.c:83-84`) — but see note above, this unit shows blue as blue. |
| Ears turn endlessly | `tests==2` loop flips both motors every ~8.2 s with **no stop condition** (`boot.0.0.0.13.mtl:2835-2843`). |
| Root condition | `master != 0` → `button2` read **pressed at boot** → factory test sequence instead of normal boot / config AP. |

Reaching it: hold button2 at boot **>7 s** (`TESTDELAY`), release (→ `tests=1`),
then one click (→ `tests=2`).

---

## LED colour reference

`setleds col` = all 5 LEDs to `col` (`:147`). Colour is `0xRRGGBB`, standard RGB
(`set_led`, `src/hal/led.c:200`). **Caveat:** some Waitrony LED batches ship
with **green/blue swapped** (`led.c:83-84,191`). On a swapped unit:
`0xffff00` (yellow) → magenta/purple; `0xff00ff` (purple) → yellow.

| Colour | Hex | Where | Meaning |
|---|---|---|---|
| Purple/magenta | `0xff00ff` | `:2957` main, master==0 | normal-boot entry (overwritten next loop by `boot_leds`) |
| Purple/magenta | `0xff00ff` | `:2365` `wifiInit` | no saved wifi env → `initW` |
| Blue | `0xff` | `:2957` main, master!=0 | test/master mode entry |
| Blue | `0x0000ff` | `:2483` `gomasterW` | became wifi AP (master) |
| Green | `0x00ff00` | `:2391` `wifiInit` | saved env restored (station) |
| Amber | `0xff8000` | `:2419/:2463` | wifi scan / auth in progress |
| White | `0xffffff` | `:2791` | master mode, button held > `TESTDELAY` (7 s) |
| Amber/green animation | — | `boot_leds` `:2546-2559` | `!master` normal boot: network-progress blink (4 steps: connect→ip→dns→http) |

`coltests` self-test palette (`:2723`):
```
var coltests={0 0 0xffff00 0xff 0xff8000 0xffff 0xff00ff};;
//      idx:  0 1   2        3    4        5      6
```

---

## Boot state machine

`main` (`:2942`) runs once:
1. `set master=button2` (`:2945`) — single button read, decides everything.
2. `confInit; wifiInit 1; loopcb #loop; netstart`.
3. `set start=time_ms`.
4. `setleds if master then 0xff else 0xff00ff` (`:2957`) — **blue if button held, purple if not**.

`loop` (`:2779`) runs every tick:
- `!master` → `wifiRun; boot_leds; boot_loop` — **normal boot**: connect wifi,
  download bytecode (`boot_loop:2590`), LEDs animate amber/green. No ear motion.
- `master && start!=nil` → wait for button release. >7 s held + release → enter
  test mode (`tests=1`); <7 s release → `setleds 0xff`, fall to config AP.
- `master && !tests` → `wifiRun` → config/master AP (`startconfigserver`).
- `master && tests` → **factory self-test menu**, button advances `tests`:

| `tests` | LED (`coltests`) | Test |
|---|---|---|
| 1 | off | idle |
| 2 | `0xffff00` | **motors** — both ears spin, flip dir every 8.2 s, endless (`:2835`) |
| 3 | `0xff` flash → `0` | motor reference seek; blue flash on entry, then all off + per-LED encoder debug (`:2844`, see below) |
| 4 | `0xff8000` | RFID read (`:2894`) |
| 5 | `0xffff` | audio playback (`:2903`) |
| 6 | `0xff00ff` | mic record + playback (`:2917`) |

Endless ears appear in `tests==2` (no stop) and `tests==3` if the position
encoder never reports motion (`get_motor_position` reads PWM capture counter
`FTM0GR`/`FTM1GR`, `src/hal/motor.c:255`).

---

## `tests==3` per-LED encoder debug (`:2844-2891`)

The most useful debug signal in tree. Unlike other states (`setleds` = all 5),
`tests==3` opens with `setleds 0` (all off) then drives **LED 1 = motor 0** and
**LED 3 = motor 1** individually as each encoder reports a new `motorget`
position. Colour encodes the reference-seek state per ear:

| LED 1 / LED 3 colour | Hex | Condition | Meaning |
|---|---|---|---|
| Purple/magenta | `0xff00ff` | `count==nil` | first ref pulse not yet captured |
| Green | `0xff00` | `i-count == 17` | reference position hit (expected delta) |
| Red | `0xff0000` | `i-count != 17` | pulse at off-position |
| Off | `0` | `count!=nil && i-count>8` | moved away from captured ref |

So **purple on a single ear LED = motor encoder seeking, no reference yet**
(distinct from the all-5-purple status states above). Gated `600ms < d < 10s`
between pulses — slower/faster pulses update position silently without relighting.

**Correction to the "LED is the only diagnostic" claim:** this block also emits a
terminal stream — `Secho "----refpos0/1 "; Iecholn i-count` plus `time_ms` +
raw position per pulse (`:2864-2870`, `:2881-2887`). Routed via `Secho`/`Iecho`
→ `src/vm/vlog.c`. Present only in `tests==3`; normal boot stays LED-only.

---

## Debug hacks in `src/main.c` (session 4 / current state)

- `push_button_value()` `src/main.c:453-458` — **real GPIO only** (latch commented out):
  ```c
  return !((INT_SWITCH_READ&INT_SWITCH_BIT)==INT_SWITCH_BIT);
  ```
  Config AP entry = hold button at power-on, release < 7s → blue. No auto-latch.
  Factory test = hold > 7s → `tests=1` (off) → press → `tests=2` (yellow + motors) → ...
- WDT **enabled** (`wdt_start()` `main.c:224`, 2.09s). Kicked by main loop during normal ops.
- Button IRQ disabled (`main.c:394`).
- Codec timeout-continue + instant audio sink (commits `5be6754`, `0c82c35`) —
  dead VS1003 no longer hangs boot (DREQ wait in `audio.c`).

**Current behaviour:** device fully operational. WiFi WPA2 connects to `g71 gast`. LED states nominal.
