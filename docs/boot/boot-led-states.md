# Boot LED & motor states

On a stock board the only diagnostic signal is **LED colour + ear motion** (no
serial/UART unless the `mod_serial` mod is fitted). This maps every boot LED state
back to the bytecode that drives it, so a colour on the device tells you exactly
which code path is live — useful when bringing up a freshly flashed rabbit.

The boot MTL is readable source (`src/boot/`). It calls C HAL syscalls
(`setleds`/`led`, `motorset`/`motorget`, `button2`) wired in
`src/firmware/src/vm/vlog.c` → `src/firmware/src/hal/{led,motor}.c`.

> **Line numbers** below (`:NNNN`) index Violet's original monolithic
> `boot.0.0.0.13.mtl`, from which `src/boot/*.mtl` is split. The self-test/LED logic
> lives in `src/boot/main.mtl` here; the colour→meaning semantics are unchanged.

---

## "all LEDs purple + ears turn endlessly"

**State: factory motor self-test, `tests==2`.** Not a crash — deliberate burn-in.
Ears spin forever by design; a button click is the only exit.

| Symptom | Cause |
|---|---|
| All LEDs purple | `coltests[2] == 0xffff00` (yellow), shown magenta only **if** this unit has the Waitrony **green/blue swap** (`src/firmware/src/hal/led.c`). |
| Ears turn endlessly | `tests==2` loop flips both motors every ~8.2 s with **no stop condition** (`:2835-2843`). |
| Root condition | `master != 0` → `button2` read **pressed at boot** → factory test sequence instead of normal boot / config AP. |

Reaching it: hold button2 at boot **>7 s** (`TESTDELAY`), release (→ `tests=1`),
then one click (→ `tests=2`).

---

## LED colour reference

`setleds col` = all 5 LEDs to `col` (`:147`). Colour is `0xRRGGBB`, standard RGB
(`set_led`, `src/firmware/src/hal/led.c`). **Caveat:** some Waitrony LED batches ship
with **green/blue swapped**. On a swapped unit: `0xffff00` (yellow) → magenta/purple;
`0xff00ff` (purple) → yellow. Trust the bytecode colour constant and confirm against
the physical unit.

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
`FTM0GR`/`FTM1GR`, `src/firmware/src/hal/motor.c`).

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

This block also emits a terminal stream — `Secho "----refpos0/1 "; Iecholn i-count`
plus `time_ms` + raw position per pulse (`:2864-2870`, `:2881-2887`), routed via
`Secho`/`Iecho` → `src/firmware/src/vm/vlog.c`. Present only in `tests==3`; normal
boot stays LED-only.
