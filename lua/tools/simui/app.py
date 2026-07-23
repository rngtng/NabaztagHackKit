#!/usr/bin/env python3
"""Browser UI for the lua-firmware simulator (#43) - a pixel-retro Nabaztag.

Embeds the Unicorn simulator (tools/simulator/simulate.py) *in this process*:
`Sim` runs on a background thread while NiceGUI serves the page. There is no
socket or IPC - the UI reads the sim's device-state attributes to render the
rabbit (LEDs, ears) and writes `sim.button` / `sim.rfid_uid` to inject input,
exactly the seam #42 built. A short resident app (default apps/ui-demo.lua) runs
on the device so it keeps reacting to whatever the browser injects.

v1 is visual + input only; the in-browser Lua REPL is a follow-up (needs the
sim's console I/O moved onto queues). Env in: FV_ELF (firmware ELF), FV_FRAMES
(the app pre-framed to #LC bytecode by the Taskfile), FV_PORT.
"""
import os
import sys
import threading

from nicegui import ui

sys.path.insert(0, os.path.dirname(__file__))
from simulate import Sim, LED_PHYS_NAME  # the embedded machine model (#42/#96)

ELF = os.environ.get("FV_ELF", "/mnt/firmware.elf")
FRAMES = os.environ.get("FV_FRAMES", "/in.lua")
PORT = int(os.environ.get("FV_PORT", "8080"))

# Known tags from apps/rfid-led-ears.lua / ui-demo.lua (confirmed on hardware).
TAGS = [("Green", "d0021a3506198b86"), ("Yellow", "d0021a35038f3a2f")]

# --- start the embedded simulator on a background thread ---------------------
with open(FRAMES, "rb") as fh:
    _frames = fh.read()
# Real-time pacing (speed=1.0) so fades + ear motion look right; a huge budget so
# the resident loop runs for the whole session; console buffered (console_only)
# so nothing scribbles the container stdout - the UI reads sim.console instead.
sim = Sim(ELF, budget=10**12, verbose=False, stdin=_frames,
          console_only=True, speed=1.0)
threading.Thread(target=sim.run, daemon=True).start()

# physical LED index -> (cx, cy, r) on the 200x300 rabbit SVG.
LED_XY = {"nose": (100, 168, 9), "belly": (100, 210, 11), "bottom": (100, 246, 9),
          "left": (72, 205, 9), "right": (128, 205, 9)}


def _css(rgb):
    """7-bit (0..127) device RGB -> an 8-bit CSS colour, with a soft glow when lit."""
    r, g, b = (min(255, v * 2) for v in rgb)
    return f"rgb({r},{g},{b})"


def rabbit_svg() -> str:
    """Render the current device state as one SVG string (redrawn each tick)."""
    leds = {name: sim.led_rgb[i] for i, name in enumerate(LED_PHYS_NAME)}
    # ears pivot at their base (top of the head); pos is the encoder count.
    e = sim.ears
    la = e[0]["pos"] % 360
    ra = -(e[1]["pos"] % 360)
    parts = [
        '<svg viewBox="0 0 200 300" xmlns="http://www.w3.org/2000/svg" '
        'style="width:100%;max-width:340px">',
        '<defs><filter id="glow"><feGaussianBlur stdDeviation="2.5"/></filter></defs>',
        # ears (rounded rects) rotating about their base at y=120
        f'<g transform="rotate({la} 78 120)">'
        f'<rect x="66" y="14" width="24" height="112" rx="12" '
        f'fill="#d7d2c8" stroke="#3a3a3a" stroke-width="3"/>'
        f'<rect x="72" y="26" width="12" height="80" rx="6" fill="#f3b0c3"/></g>',
        f'<g transform="rotate({ra} 122 120)">'
        f'<rect x="110" y="14" width="24" height="112" rx="12" '
        f'fill="#d7d2c8" stroke="#3a3a3a" stroke-width="3"/>'
        f'<rect x="116" y="26" width="12" height="80" rx="6" fill="#f3b0c3"/></g>',
        # head
        '<ellipse cx="100" cy="205" rx="74" ry="70" fill="#e8e4da" '
        'stroke="#3a3a3a" stroke-width="3"/>',
        '<ellipse cx="100" cy="176" rx="16" ry="7" fill="#e8e4da" '
        'stroke="#3a3a3a" stroke-width="2"/>',  # muzzle hint
    ]
    for name, (cx, cy, r) in LED_XY.items():
        col = _css(leds[name])
        lit = any(leds[name])
        if lit:
            parts.append(f'<circle cx="{cx}" cy="{cy}" r="{r + 4}" fill="{col}" '
                         f'filter="url(#glow)" opacity="0.8"/>')
        parts.append(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{col}" '
                     f'stroke="#2a2a2a" stroke-width="1.5"/>')
    return "".join(parts) + "</svg>"


# --- page --------------------------------------------------------------------
@ui.page("/")
def index():
    ui.add_head_html('<style>body{background:#1b1b22}'
                     '.pix{font-family:"Courier New",monospace;letter-spacing:1px}</style>')
    with ui.column().classes("items-center w-full pix").style("color:#e8e4da"):
        ui.label("NABAZTAG · sim").classes("text-2xl").style("color:#8fe3a0")
        rabbit = ui.html(rabbit_svg())
        status = ui.label("").style("color:#9aa0b5")

        # head button - press-and-hold (mouse down/up, release on leave)
        def press():  sim.button = True
        def release(): sim.button = False
        btn = ui.button("HOLD HEAD BUTTON").props("color=purple")
        btn.on("mousedown", press); btn.on("mouseup", release); btn.on("mouseleave", release)

        with ui.row().classes("items-center"):
            for label, uid in TAGS:
                ui.button(f"Place {label}", on_click=lambda u=uid: setattr(sim, "rfid_uid", u)) \
                    .props("outline")
            ui.button("Remove tag", on_click=lambda: setattr(sim, "rfid_uid", None)).props("outline color=grey")
        with ui.row().classes("items-center"):
            custom = ui.input("Custom UID (16 hex)").props("dense dark").style("width:200px")
            ui.button("Place", on_click=lambda: setattr(sim, "rfid_uid", (custom.value or "").strip() or None)) \
                .props("flat")

        log = ui.log(max_lines=200).classes("w-full").style(
            "height:150px;background:#0e0e12;color:#7fd88f;font-size:12px")

    seen = {"n": 0}

    def tick():
        rabbit.content = rabbit_svg()
        tag = sim.rfid_uid or "-"
        status.text = (f"button {'DOWN' if sim.button else 'up'}   tag {tag}   "
                       f"ears {sim.ears[0]['pos'] % 360}°/{sim.ears[1]['pos'] % 360}°")
        # stream new device-console bytes into the log
        buf = bytes(sim.console)
        if len(buf) > seen["n"]:
            new = buf[seen["n"]:].decode("latin1")
            seen["n"] = len(buf)
            for line in new.splitlines():
                if line.strip():
                    log.push(line)

    ui.timer(0.1, tick)


ui.run(host="0.0.0.0", port=PORT, title="Nabaztag simulator", reload=False, show=False)
