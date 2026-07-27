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
# drawn on the face (see rabbit_static_svg), not here.
LED_XY = {"left": (85, 292, 13), "belly": (120, 292, 13), "right": (155, 292, 13),
          "bottom": (120, 320, 12)}


def _css(rgb):
    """7-bit (0..127) device RGB -> an 8-bit CSS colour, with a soft glow when lit."""
    r, g, b = (min(255, v * 2) for v in rgb)
    return f"rgb({r},{g},{b})"


OFF_COL = "#efece2"          # a lit-off LED reads as the shell, never black (#43)
EAR_REST = (-13, 13)         # left/right ear splay at rest, in degrees


def _ear(px, py, angle, eid):
    """A long thick tapering white ear rising from pivot (px,py), at rest angle
    `angle`. Rotation is a CSS transform about the base (transform-origin bottom-
    centre of the shape) so tick() can retarget it and the browser tweens the
    spin smoothly between the 100 ms samples (id `eid`)."""
    d = (f"M{px-15},{py} C{px-17},{py-52} {px-11},{py-120} {px-6},{py-152} "
         f"Q{px},{py-166} {px+6},{py-152} C{px+11},{py-120} {px+17},{py-52} "
         f"{px+15},{py} Z")
    return (f'<g id="{eid}" style="transform-box:fill-box;transform-origin:50% 100%;'
            f'transform:rotate({angle}deg)">'
            f'<path d="{d}" fill="#f4f3ef" stroke="#c7c2b6" stroke-width="2.5"/>'
            f'<path d="M{px-3},{py-24} C{px-4},{py-70} {px-2},{py-112} {px},{py-140}" '
            f'fill="none" stroke="#e3ded2" stroke-width="4" stroke-linecap="round"/></g>')


def rabbit_static_svg() -> str:
    """The rabbit's fixed structure, rendered once - a cone-bodied Nabaztag with
    two ears, the iconic face (oval eyes + dark triangle nose) and the belly
    LEDs. Every animated element carries a stable id and a CSS transition, and
    starts at rest / unlit; tick() drives the dynamic values via `tick_js` so the
    browser interpolates motion + glow rather than snapping each poll."""
    # cone body: a narrow rounded crown flaring smoothly to a wide rounded base.
    body = ("M120,148 C141,149 153,184 163,238 C170,282 183,306 187,318 "
            "Q190,332 169,334 L71,334 Q50,332 53,318 C57,306 70,282 77,238 "
            "C87,184 99,149 120,148 Z")
    parts = [
        '<svg viewBox="0 0 240 360" xmlns="http://www.w3.org/2000/svg" '
        'style="width:100%;max-width:320px">',
        # transitions: ears tween their rotation; LEDs + nose glow cross-fade;
        # the crown shade fades in/out with the button.
        '<style>#earL,#earR{transition:transform .12s linear}'
        '.led{transition:fill .28s ease,opacity .28s ease}'
        '#nose-glow,#crown{transition:opacity .2s ease}</style>',
        '<defs><filter id="glow" x="-70%" y="-70%" width="240%" height="240%">'
        '<feGaussianBlur stdDeviation="6"/></filter></defs>',
        _ear(112, 152, EAR_REST[0], "earL"), _ear(128, 152, EAR_REST[1], "earR"),
        f'<path d="{body}" fill="#f4f3ef" stroke="#c7c2b6" stroke-width="2.5"/>',
        # soft glossy highlight down the left of the body (kept inside the shell)
        '<path d="M101,232 C93,262 91,292 97,316" fill="none" stroke="#ffffff" '
        'stroke-width="9" stroke-linecap="round" opacity="0.4"/>',
        # the whole head IS the click button - the crown shade fades in while held
        # (opacity driven by tick), so nothing is drawn on it at rest.
        '<ellipse id="crown" cx="120" cy="170" rx="46" ry="34" fill="#000000" '
        'opacity="0"/>',
        # face: two simple oval eyes (the iconic Nabaztag look)
        '<ellipse cx="106" cy="200" rx="6.5" ry="9" fill="#1e1e1e" '
        'transform="rotate(10 106 200)"/>',
        '<ellipse cx="134" cy="200" rx="6.5" ry="9" fill="#1e1e1e" '
        'transform="rotate(-10 134 200)"/>',
        # nose: the blue LED glows through the shell (id nose-glow, fades in); on
        # top, the iconic dark nose - a small downward triangle with a short stem.
        '<ellipse id="nose-glow" cx="120" cy="224" rx="20" ry="22" fill="#4a7fa5" '
        'filter="url(#glow)" opacity="0"/>',
        '<path d="M112,218 Q120,215 128,218 L121,229 Q120,230 119,229 Z" '
        'fill="#242424"/>',
        '<path d="M120,229 L120,238" stroke="#242424" stroke-width="2.4" '
        'stroke-linecap="round"/>',
    ]
    # belly LEDs: a glow disc (fades in when lit) behind a solid core that
    # cross-fades from the shell colour to the lit colour.
    for name, (cx, cy, r) in LED_XY.items():
        parts.append(f'<circle id="led-{name}-glow" class="led" cx="{cx}" cy="{cy}" '
                     f'r="{r + 9}" fill="{OFF_COL}" filter="url(#glow)" opacity="0"/>')
        parts.append(f'<circle id="led-{name}-core" class="led" cx="{cx}" cy="{cy}" '
                     f'r="{r}" fill="{OFF_COL}" stroke="#e4e0d3" stroke-width="1"/>')
    return "".join(parts) + "</svg>"


def tick_js() -> str:
    """JS that retargets the animated elements to the sim's current state. The
    static SVG already carries the CSS transitions, so setting `.style.*` here
    makes the browser tween ear rotation, LED/nose glow and the crown shade
    between polls instead of snapping."""
    leds = {name: sim.led_rgb[i] for i, name in enumerate(LED_PHYS_NAME)}
    e = sim.ears
    # ears splay at rest and turn with the encoder count.
    la = EAR_REST[0] + (e[0]["pos"] % 360)
    ra = EAR_REST[1] - (e[1]["pos"] % 360)
    js = [
        "var S=function(id,f,o,t){var e=document.getElementById(id);if(!e)return;"
        "if(f!=null)e.style.fill=f;if(o!=null)e.style.opacity=o;"
        "if(t!=null)e.style.transform=t;};",
        f"S('earL',null,null,'rotate({la}deg)');",
        f"S('earR',null,null,'rotate({ra}deg)');",
        f"S('crown',null,{0.13 if sim.button else 0},null);",
    ]
    nose = leds["nose"]
    js.append(f"S('nose-glow','{_css(nose) if any(nose) else '#4a7fa5'}',"
              f"{0.8 if any(nose) else 0},null);")
    for name in LED_XY:
        v = leds[name]
        on = any(v)
        col = _css(v) if on else OFF_COL
        js.append(f"S('led-{name}-glow','{col}',{0.75 if on else 0},null);")
        js.append(f"S('led-{name}-core','{col}',{0.9 if on else 1},null);")
    return "".join(js)


# --- page --------------------------------------------------------------------
@ui.page("/")
def index():
    ui.add_head_html('<style>body{background:#1b1b22}'
                     '.pix{font-family:"Courier New",monospace;letter-spacing:1px}</style>')
    with ui.column().classes("items-center w-full pix").style("color:#e8e4da"):
        ui.label("NABAZTAG · sim").classes("text-2xl").style("color:#8fe3a0")
        ui.html(rabbit_static_svg())   # fixed structure; tick() animates it via JS
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
        ui.run_javascript(tick_js())
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
