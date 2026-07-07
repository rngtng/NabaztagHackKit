# Debricking a Nabaztag:tag (V2) with a Raspberry Pi (bit-bang JTAG)

How to recover a dead **Nabaztag:tag (V2)** (OKI **ML67Q4051**, ARM7TDMI) by
reflashing its firmware over JTAG — using a **Raspberry Pi as the JTAG adapter**,
no Bus Pirate or Bus Blaster required.

Companion config: [`nabaztag-pi.cfg`](nabaztag-pi.cfg).

## Background / insights

- The chip is an OKI **ML67Q4051** (ARM7TDMI). Flash is a single 128 KB bank
  based at `0x08000000` (see [`target/ml67q4051.cfg`](target/ml67q4051.cfg)).
- **Mainline OpenOCD cannot program this flash.** The OKI flash routine lives in
  a custom `ml67q40xx` NOR driver written by **RedoX**. A stock `apt install
  openocd` fails with `Error: flash driver 'ml67q40xx' not found`. You must
  build OpenOCD **0.8.0 + RedoX's OKI patch**.
- This repo ships a prebuilt patched OpenOCD at [`openocd`](openocd), **but it is
  an x86-64 binary built FTDI-only** — it neither runs on the ARM Pi nor contains
  the `bcm2835gpio` (Pi GPIO) driver. So we rebuild 0.8.0 on the Pi ourselves,
  enabling `bcm2835gpio`.
- The [journaldulapin debrick guide](https://www.journaldulapin.com/2017/09/10/debriquer-nabaztag/)
  is the **same chip + same RedoX patch**, but it drives JTAG with a **Bus
  Pirate** (the Pi is only the build host). Here the Pi *is* the adapter, so we
  add `--enable-bcm2835gpio` (confirmed present in the 0.8.0 source) and skip the
  Bus Pirate. We also skip its "dump a backup from a working rabbit" step — we
  build `bin/Nab.elf` from source instead.
- The Nabaztag JTAG header exposes **RESETN only (no TRST)**, so the OpenOCD
  reset mode is `srst_only` (overridden in `nabaztag-pi.cfg`).
- Both sides are **3.3 V logic** (Pi GPIO and ML67), so no level shifter needed.

## Hardware required

- Raspberry Pi with a GPIO header. This doc is tuned for a **Model A+ / Pi 1 /
  Zero (BCM2835, single core)**. Pi 4 needs the right `peripheral_base`; Pi 5
  must use the `linuxgpiod` driver — neither matches the 0.8.0 build here.
- ~6 jumper wires.
- Triangular ("tamper") screwdriver to open the Nabaztag base (4 screws).
- The Nabaztag's **own power supply** (do not power it from the Pi).

## Wiring: Pi GPIO → Nabaztag 8-pin JTAG

Connector is at the **top-left when facing the rabbit**. Match signal names exactly.

| JTAG signal | Nabaztag pin | Pi BCM GPIO | Pi physical pin |
|---|---|---|---|
| TCK    | 6 | GPIO11 | 23 |
| TMS    | 5 | GPIO25 | 22 |
| TDI    | 4 | GPIO10 | 19 |
| TDO    | 7 | GPIO9  | 21 |
| RESETN | 8 | GPIO24 | 18 |
| GND    | 2 | —      | 25 (any GND) |
| 3.3 V (Vref) | 1 | **leave unconnected** | — |
| (not connected) | 3 | — | — |

> ⚠️ **Verify pin 1 first.** Find the square pad / silkscreen "1" on the board;
> don't assume orientation. A miswired 3.3 V pin can damage the device.
> Wire with **everything powered off**. The Pi shares only **GND** with the rabbit.

## Steps

### 1. Build patched OpenOCD 0.8.0 on the Pi (one-time)

This is the part that makes `ml67q40xx` flashing work. On the Pi:

```sh
sudo apt-get update
# tcl provides tclsh, so jimtcl's bundled configure skips its broken
# bootstrap-compile (otherwise fails "No working C compiler found").
sudo apt-get install -y build-essential libtool autoconf automake pkg-config libusb-1.0-0-dev tcl
which autoreconf      # sanity check: must print /usr/bin/autoreconf before continuing

cd ~
wget http://wk.redox.ws/_media/dev/nab/v2/jtag/openocd-0.8.0.tar.gz
wget http://wk.redox.ws/_media/dev/nab/v2/jtag/openocd_0.8.0_oki.patch.gz
gzip -d openocd_0.8.0_oki.patch.gz
tar xzf openocd-0.8.0.tar.gz
cd openocd-0.8.0
patch -p1 < ../openocd_0.8.0_oki.patch        # adds src/flash/nor/ml67q40xx.c
autoreconf -fi

# bcm2835gpio = Pi as adapter. --disable-werror turns off OpenOCD's own -Werror.
# The CFLAGS downgrade gcc 14's new DEFAULT errors (Debian/RPi OS Trixie) so this
# 2014 code still builds; harmless on older gcc (12, Bookworm).
./configure --enable-bcm2835gpio --disable-werror \
  CFLAGS="-Wno-error=implicit-function-declaration -Wno-error=incompatible-pointer-types -Wno-error=int-conversion -Wno-error=implicit-int"

make                                          # Pi A+ is single-core; no -j. Slow (~20-40 min)
sudo make install                             # installs to /usr/local/bin/openocd

/usr/local/bin/openocd --version              # must say 0.8.0
```

> The apt `openocd` (0.12, no driver) may still be first on `$PATH`. Always call
> the freshly built one by full path: `/usr/local/bin/openocd`.

### 2. Build firmware (on the Mac / dev host)

```sh
task compile          # -> bin/Nab.elf  (also .bin / .hex)
```

### 3. Copy the repo to the Pi

Needs at least `openocd/` (configs), `bin/Nab.elf`, and `gdb_load`:

```sh
scp -r . pi@<pi-ip>:~/nabgcc
```

### 4. Start the JTAG bridge — Pi shell 1

```sh
cd ~/nabgcc/openocd
sudo /usr/local/bin/openocd -f nabaztag-pi.cfg     # sudo: bcm2835gpio needs /dev/mem
```

**This is the make-or-break check.** Look for:

```
JTAG tap: ml67q4051.cpu tap/device found: 0x3f0f0f0f ...
```

- **IDCODE `0x3f0f0f0f` appears** → CPU is alive, the brick was just a bad flash → reflashable. Continue.
- **"JTAG scan chain interrogation failed" / all-ones / all-zeroes** → recheck
  wiring (especially pin 1 and the table); if wiring is confirmed good it's a
  deeper hardware fault JTAG can't fix.

### 5. Flash — Pi shell 2

```sh
cd ~/nabgcc
gdb-multiarch bin/Nab.elf -x gdb_load
```

`gdb_load` runs: `target extended-remote localhost:3333` → `load` (programs flash
at `0x08000000` via the `ml67q40xx` driver) → `mon reset run` → `quit`.

### 6. Verify

The rabbit should boot (LEDs / ears move). Done.

## Troubleshooting

- **`flash driver 'ml67q40xx' not found`** → you're running the apt OpenOCD, not
  the patched 0.8.0. Use the full path `/usr/local/bin/openocd` and confirm
  `--version` says 0.8.0.
- **`DEPRECATED! use 'adapter gpio ...'` then a config error** → same cause: that
  warning comes from the modern apt OpenOCD. `nabaztag-pi.cfg` uses 0.8.0 syntax
  (`interface bcm2835gpio`, `bcm2835gpio_jtag_nums`, `bcm2835gpio_srst_num`).
- **Build fails on a warning** → ensure `--disable-werror` was passed.
- **jimtcl: `No working C compiler found` during configure** → gcc 14 rejects
  jimtcl's bootstrap `jimsh0.c` (undeclared `isatty`). Fix: `sudo apt-get install
  -y tcl` (lets its configure use system `tclsh` instead of compiling jimsh0).
- **`make` errors with `implicit-function-declaration` / `incompatible-pointer-types`
  / `int-conversion`** → gcc 14 promotes these to errors by default. Add the
  `CFLAGS="-Wno-error=..."` shown in the configure step above and re-`./configure`.
- **IDCODE shows but rabbit still won't boot after flashing** → the firmware
  build itself may be broken (e.g. mid-feature work on the current branch). Flash
  a **known-good earlier commit**: checkout the commit, `task compile`, repeat
  step 5.
- **Flashes & verifies fine (sections "matched") but the rabbit is dead — blank/
  stuck LEDs, no button, no WiFi** → boot is hanging **inside `loaderInit`, before
  the VM starts**, because the embedded boot bytecode `dumpbc` (`src/bc.c`) is in
  the **signed `"amber"` wrapper** instead of bare. `loaderInit` reads `"ambe"` as
  a size → runaway copy → hang. **Fix:** regenerate `dumpbc` **unsigned**
  (`task vm-compile` / `SIGN=false`) so it starts `0xf5 0x47`, or rely on the
  `main.c` guard that skips the 13-byte `"amber"` header. Confirm over JTAG with
  `hbreak interpGo` — if it never fires, the loader is hung. See
  [`architecture.md`](architecture.md) bytecode warning.
- **Re-flashed but behaviour didn't change** → the ELF on the Pi may be stale
  (`rsync -a` can silently skip). Force `rsync --checksum` and confirm `md5sum`
  local==remote before `gdb_load`; `compare-sections` "matched" only proves
  flash==the-loaded-ELF, not that it's your new build.
- **Flash slow / minor warnings mid-write** → bit-bang on a 700 MHz ARM11 is slow
  but reliable for 128 KB; only a final failure matters.
- **Inspect interactively** → `telnet localhost 4444` while OpenOCD runs, then
  `halt`, `reg`, `flash banks`.
