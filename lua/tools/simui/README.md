# simui — browser UI for the simulator (#43)

A pixel-retro Nabaztag in the browser. One container runs **NiceGUI + the
embedded Unicorn simulator** (`tools/simulator/simulate.py`) in one process: the
`Sim` runs on a background thread while the page renders it and injects input.
There is no socket — the UI reads the sim's device-state attributes
(`led_rgb`, `ears`, `button`, `rfid_uid`) to draw the five LEDs and the ears, and
writes `button` / `rfid_uid` to inject — the seam [#42](../../firmware/README.md#peripheral-injection--state-42)
built. A short resident app (default `apps/ui-demo.lua`) runs on the device so it
keeps reacting to whatever the browser injects.

```sh
task lua:simui:serve                       # http://localhost:8080, runs apps/ui-demo.lua
task lua:simui:serve APP=apps/rfid-led-ears.lua PORT=9000
```

Then, in the page: **Place Green / Yellow** (or a custom UID) puts a tag on the
coupler → LEDs colour + an ear spins; **Remove tag** clears it; **hold the head
button** → LEDs go white. The console panel streams the device's `print()` output.

## What it is / isn't (v1)

* **Visual + input.** Renders LEDs (colour from the sim's reconstructed LED
  frame) and ears (rotation from the synthetic encoder), and injects
  button/RFID. The ASCII `--leds` strip in `simulate.py` is unchanged and stays
  the zero-dependency console/CI view — this is the richer interactive one.
* **No in-browser REPL yet.** Typing Lua at the device needs the sim's console
  I/O moved onto queues (it currently reads `sys.stdin`); that + animation polish
  is the v2 follow-up. Drive the REPL meanwhile with `lua:firmware:simulate:repl`.

## Layout

* `app.py` — the NiceGUI page: embeds `Sim`, renders the rabbit as inline SVG,
  a `ui.timer` polls device state each 100 ms, buttons write the injected input.
* `Dockerfile` — `python:3.12-slim` + `unicorn`/`pyelftools`/`nicegui`; built with
  the lua layer root as context (like `tools/luac`/`tools/simulator`) so it can
  COPY the sibling `simulate.py`.
* `Taskfile.yaml` — owns the image build + the `serve` verb (frames the resident
  app via `tools/luac`, then `docker run -p`).
