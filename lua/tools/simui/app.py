#!/usr/bin/env python3
"""Browser UI for the lua-firmware simulator (#43) - a pixel-retro Nabaztag.

Embeds the Unicorn simulator (tools/simulator/simulate.py) *in this process*:
`Sim` runs on a background thread while NiceGUI serves the page. There is no
socket or IPC - the UI reads the sim's device-state attributes to render the
rabbit (LEDs, ears) and writes `sim.button` / `sim.rfid_uid` to inject input,
exactly the seam #42 built. A short resident app (default apps/ui-demo.lua)
registers nab.on callbacks and returns, so the device keeps reacting to injected
input while sitting at the REPL prompt (below).

The page also drives an in-browser Lua REPL: the sim's UART RX is a live queue
(simulate.Sim(rx_queue=...)), the resident app registers nab.on callbacks and
returns so the firmware sits at the REPL prompt, and each typed line is compiled
to #LC bytecode by the in-container luac (parser-less firmware, #128) and pushed
onto the queue. Device output streams back into the console panel.

Env in: FV_ELF (firmware ELF), FV_FRAMES (the resident app pre-framed to #LC
bytecode by the Taskfile), FV_PORT.
"""
import math
import os
import queue
import subprocess
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
# The console RX is a live queue: seed it with the resident app's #LC frames
# (the firmware runs them, registers its nab.on callbacks and returns to the
# prompt) and keep it open so the browser REPL can push more frames. An empty
# queue reads as "no byte yet" (never EOF), so the REPL idles + pumps events.
RX = queue.Queue()
with open(FRAMES, "rb") as fh:
    for _b in fh.read():
        RX.put(_b)
# Real-time pacing (speed=1.0) so fades + ear motion look right; a huge budget so
# the session runs indefinitely; console buffered (console_only) so nothing
# scribbles the container stdout - the UI reads sim.console instead.
sim = Sim(ELF, budget=10**12, verbose=False, rx_queue=RX,
          console_only=True, speed=1.0)
threading.Thread(target=sim.run, daemon=True).start()


def _luac(src: bytes):
    """Compile Lua source to stripped #LC-ready bytecode via the in-container
    luac (matches the firmware's LUA_32BITS dump). Returns (chunk, stderr);
    chunk is None on a compile error. Source rides stdin, chunk rides stdout."""
    p = subprocess.run(["luac", "-s", "-o", "/dev/stdout", "-"],
                       input=src, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
    if p.returncode != 0:
        return None, p.stderr.decode("utf-8", "replace")
    return p.stdout, ""


def compile_line(line: bytes):
    """Compile one REPL line expression-first (mirrors luash.compile_line): try
    `return <line>` so a bare expression echoes its value, else the line verbatim."""
    chunk, _ = _luac(b"return " + line)
    if chunk is not None:
        return chunk, ""
    return _luac(line)


def send_frame(chunk: bytes):
    """Push one #LC frame (header line + 64-col hex payload) onto the RX queue,
    byte by byte - the exact format the firmware's load_lc_frame decodes."""
    hexs = chunk.hex()
    body = f"#LC:{len(chunk)}\n" + "\n".join(
        hexs[i:i + 64] for i in range(0, len(hexs), 64)) + "\n"
    for b in body.encode():
        RX.put(b)

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
# Ear rotation. Each real ear turns on a cone tilted OUTWARD, so head-on it
# sweeps within its own side and never crosses to the other ear: the left ear
# runs left <-> up (the [-90, 0] deg hemisphere), the right ear up <-> right
# ([0, 90]). Rest is ~45 deg to the side (the sweep midpoint). We drive a
# continuous phase from the encoder delta and map it through sin() - which is
# exactly what a constant-speed cone rotation looks like projected head-on
# (eases at the side/up extremes, quicker through the middle) - so the ear never
# swings past vertical into the other ear.
EAR_REST = (-45, 45)         # left/right rest splay, degrees (also the sweep centre)
EAR_AMP = (45, -45)          # signed amplitude: left dips to -90/0, right to 0/+90
# ~8 encoder counts/ms; this scale gives a calm ~2-3 s per full sweep cycle.
RAD_PER_COUNT = 0.0003
_ear_last = [None, None]     # previous encoder pos per ear
_ear_phase = [0.0, 0.0]      # accumulated sweep phase per ear, radians


def ear_angles():
    """Advance each ear's sweep phase from the encoder delta since the last poll
    and return the two absolute angles. left stays in [-90, 0], right in [0, 90],
    so they never overlap."""
    out = []
    for i, e in enumerate(sim.ears):
        pos = e["pos"]
        last = _ear_last[i]
        _ear_last[i] = pos
        if last is not None:
            d = (pos - last) & 0xFFFF          # unwrap the 16-bit encoder
            if d >= 0x8000:
                d -= 0x10000
            _ear_phase[i] += d * RAD_PER_COUNT
        out.append(EAR_REST[i] + EAR_AMP[i] * math.sin(_ear_phase[i]))
    return out


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
    la, ra = ear_angles()   # continuous, smoothed rotation from the encoder delta
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

        # in-browser REPL: type Lua, compile off-device to #LC bytecode (#128),
        # push it onto the sim's RX queue; the device output streams into `log`.
        def repl_submit():
            line = (repl_in.value or "").strip()
            repl_in.value = ""
            if not line:
                return
            log.push("> " + line)
            chunk, err = compile_line(line.encode())
            if chunk is None:
                for e in err.splitlines():
                    if e.strip():
                        log.push(e)
                return
            send_frame(chunk)

        repl_in = ui.input(placeholder="lua > (Enter to run)").props("dense dark") \
            .classes("w-full").style("font-family:monospace")
        repl_in.on("keydown.enter", repl_submit)

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
