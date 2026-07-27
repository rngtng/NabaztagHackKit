# simui — browser UI for the simulator (#43)

A pixel-retro Nabaztag in the browser. One container runs **NiceGUI + the
embedded Unicorn simulator** (`tools/simulator/simulate.py`) in one process: the
`Sim` runs on a background thread while the page renders it and injects input.
There is no socket — the UI reads the sim's device-state attributes
(`led_rgb`, `ears`, `button`, `rfid_uid`) to draw the five LEDs and the ears, and
writes `button` / `rfid_uid` to inject — the seam [#42](../../firmware/README.md#peripheral-injection--state-42)
built. A short resident app (default `apps/ui-demo.lua`) registers `nab.on`
callbacks and returns, so the firmware sits at the REPL prompt: it keeps reacting
to whatever the browser injects **and** accepts typed Lua.

```sh
task lua:simui:serve                       # http://localhost:8080, runs apps/ui-demo.lua
task lua:simui:serve APP=apps/rfid-led-ears.lua PORT=9000
```

Then, in the page: **Place Green / Yellow** (or a custom UID) puts a tag on the
coupler → LEDs colour + an ear spins; **Remove tag** clears it; **hold the head
button** → LEDs go white. The **REPL box** runs any Lua line on the device
(`print(6*7)`, `nab.led('nose',0,0,127)`, …). The console panel streams the
device's `print()` output + results.

## How it renders + animates

The rabbit is drawn **once** as inline SVG (`rabbit_static_svg`) with a stable id
+ CSS transition on every animated element. A `ui.timer` polls device state each
100 ms and pushes only the changed values via JS (`tick_js` → `.style.transform`
/ `.style.fill` / `.style.opacity`), so the browser tweens ear rotation and the
LED / nose / crown glow between samples instead of snapping. (The ASCII `--leds`
strip in `simulate.py` is unchanged and stays the zero-dependency console/CI view.)

## How the REPL works

The firmware is parser-less (#128) — it runs only `luac` bytecode. So each typed
line is compiled **in-container** (the `luac` binary is pulled into the image
from `tools/luac`), framed as `#LC` (expression-first, like `luash.py`), and
pushed byte-by-byte onto the sim's live **RX queue** (`Sim(rx_queue=…)`). An
empty queue reads as "no byte yet", never EOF, so the REPL idles and pumps events
(#195) — that is what lets the injected-input reactivity and the live REPL share
the one console. `x = 10` on one line is visible to `print(x)` on the next
(globals persist; each line is its own chunk).

## Layout

* `app.py` — the NiceGUI page: embeds `Sim` with a live RX queue, renders the
  rabbit as inline SVG + drives it via JS each tick, injects button/RFID, and
  compiles + frames REPL lines onto the queue.
* `Dockerfile` — `python:3.12-slim` + `unicorn`/`pyelftools`/`nicegui`, plus the
  `luac` binary pulled from the `tools/luac` image (multi-stage `COPY --from`);
  built with the lua layer root as context so it can COPY the sibling
  `simulate.py`.
* `Taskfile.yaml` — owns the image build + the `serve` verb (frames the resident
  app via `tools/luac`, then `docker run -p`).
