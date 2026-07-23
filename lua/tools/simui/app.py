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

# The 4 belly LEDs -> (cx, cy, r) on the 240x360 cone-body SVG (a row of three
# plus one below, like the real device's belly lights). The 5th, "nose", is
# drawn on the face (see rabbit_svg), not here.
LED_XY = {"left": (92, 252, 12), "belly": (120, 252, 12), "right": (148, 252, 12),
          "bottom": (120, 286, 12)}


def _css(rgb):
    """7-bit (0..127) device RGB -> an 8-bit CSS colour, with a soft glow when lit."""
    r, g, b = (min(255, v * 2) for v in rgb)
    return f"rgb({r},{g},{b})"


def _ear(px, py, angle):
    """A long tapering white ear pointing up from pivot (px,py), rotated `angle`
    degrees about that pivot - so the encoder spin reads as the ear turning."""
    d = (f"M{px-14},{py} C{px-16},{py-46} {px-10},{py-104} {px-5},{py-132} "
         f"Q{px},{py-144} {px+5},{py-132} C{px+10},{py-104} {px+16},{py-46} "
         f"{px+14},{py} Z")
    return (f'<g transform="rotate({angle} {px} {py})">'
            f'<path d="{d}" fill="#f4f3ef" stroke="#c7c2b6" stroke-width="2.5"/>'
            f'<path d="M{px-3},{py-20} C{px-4},{py-60} {px-2},{py-96} {px},{py-120}" '
            f'fill="none" stroke="#e3ded2" stroke-width="4" stroke-linecap="round"/></g>')


def rabbit_svg() -> str:
    """Render the current device state as one SVG string (redrawn each tick):
    a cone-bodied Nabaztag - two long ears (spinning with the encoder), a face
    (eyes + LED nose + mouth), and the belly LEDs."""
    leds = {name: sim.led_rgb[i] for i, name in enumerate(LED_PHYS_NAME)}
    e = sim.ears
    # ears splay out at rest (-/+13 deg) and turn with the encoder count.
    la = -13 + (e[0]["pos"] % 360)
    ra = 13 - (e[1]["pos"] % 360)
    nose = leds["nose"]
    nose_col = _css(nose) if any(nose) else "#4a7fa5"   # muted blue when unlit
    # cone/teardrop body: narrow rounded top flaring to a wide rounded base.
    body = ("M120,116 C150,116 168,150 174,202 C179,252 183,292 176,306 "
            "Q170,320 150,320 L90,320 Q70,320 64,306 C57,292 61,252 66,202 "
            "C72,150 90,116 120,116 Z")
    parts = [
        '<svg viewBox="0 0 240 360" xmlns="http://www.w3.org/2000/svg" '
        'style="width:100%;max-width:320px">',
        '<defs><filter id="glow" x="-60%" y="-60%" width="220%" height="220%">'
        '<feGaussianBlur stdDeviation="3.2"/></filter></defs>',
        _ear(104, 128, la), _ear(136, 128, ra),
        f'<path d="{body}" fill="#f4f3ef" stroke="#c7c2b6" stroke-width="2.5"/>',
        # soft glossy highlight down the left of the body
        '<path d="M92,150 C80,190 78,250 84,300" fill="none" stroke="#ffffff" '
        'stroke-width="10" stroke-linecap="round" opacity="0.5"/>',
        # face: two eyes + mouth (the nose is an LED, drawn in the loop below)
        '<ellipse cx="104" cy="188" rx="5" ry="7" fill="#2b2b2b"/>',
        '<ellipse cx="136" cy="188" rx="5" ry="7" fill="#2b2b2b"/>',
        '<path d="M120,214 L120,224 M120,224 Q112,228 108,222 M120,224 Q128,228 132,222" '
        'fill="none" stroke="#8a8578" stroke-width="2" stroke-linecap="round"/>',
    ]
    # LED nose on the face (glows when lit)
    if any(nose):
        parts.append(f'<circle cx="120" cy="205" r="12" fill="{nose_col}" '
                     f'filter="url(#glow)" opacity="0.85"/>')
    parts.append(f'<path d="M120,197 Q130,205 120,213 Q110,205 120,197 Z" '
                 f'fill="{nose_col}" stroke="#3a5a72" stroke-width="1"/>')
    # belly LEDs: a faint translucent disc when off (like the real device),
    # a saturated glow when lit.
    for name, (cx, cy, r) in LED_XY.items():
        if any(leds[name]):
            col = _css(leds[name])
            parts.append(f'<circle cx="{cx}" cy="{cy}" r="{r + 5}" fill="{col}" '
                         f'filter="url(#glow)" opacity="0.85"/>')
            parts.append(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{col}" '
                         f'stroke="#ece7db" stroke-width="1.5"/>')
        else:
            parts.append(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="#e7e2d6" '
                         f'stroke="#d5d0c3" stroke-width="1.5"/>')
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
