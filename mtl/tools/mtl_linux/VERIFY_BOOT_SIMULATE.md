# Verifying mtl:boot:simulate

`mtl:boot:simulate` preprocesses `boot.mtl` with `SIMU`+`BOOT` defines, stubs
`button2` to pressed (so the simulator enters config mode), and exposes the HTTP
config server on `http://localhost:8080`. Requires Docker Hub access (pulls
`debian:bookworm-slim` and `python:3.12-slim`).

## Steps

```bash
# 1. Build images and run the simulation
task mtl:boot:simulate
```

First run builds two Docker images (~2-3 min); later runs are fast.

Expected log output:
- `:started` — boot MTL is running
- `button :` followed by a non-zero value — config mode triggered
- `-------------master` — confirmed config mode; TCP server accepting on port 80
- LED set to `0x0000ff` (blue) — config server active

```bash
# 2. In a second terminal, hit the config page
curl -s http://localhost:8080 | grep -i "nabaztag\|ssid\|wifi"
```

Expected: HTML with the WiFi setup form (SSID selector, key field, etc.).

```bash
# 3. Open in a browser
open http://localhost:8080       # macOS
xdg-open http://localhost:8080   # Linux
```

Expected: the classic Nabaztag blue-LED WiFi configuration wizard.

## Notes

- `getButton()` in `src/simu/linux/simu.c` is hardcoded to `1`, so every
  simulation sees button-pressed. Fine for boot testing; could later become a
  `--button` CLI flag to `mtl_simu`.
- Different host port: `task mtl:boot:simulate PORT=9090`.
- Stop with Ctrl-C.
