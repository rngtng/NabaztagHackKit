# Verifying boot:simulate

The `boot:simulate` task was added on the `sdk` branch. It preprocesses
`boot.mtl` with `SIMU` and `BOOT` defines, stubs `button2` to pressed (so
the simulator enters config mode), and exposes the HTTP config server on
`http://localhost:8080`.

Verification requires Docker Hub access (image pulls for
`debian:bookworm-slim` and `python:3.12-slim`).

## Steps

```bash
# 1. Build images and run the simulation
task simulate:boot
```

The first run builds two Docker images (~2–3 min). Subsequent runs are fast.

Expected output in the logs:
- `:started` — boot MTL is running
- `button :` followed by a non-zero value — config mode triggered
- `-------------master` — confirmed config mode
- LED set to `0x0000ff` (blue) — config server active
- `-------------master` and the TCP server accepting on port 80

```bash
# 2. In a second terminal, hit the config page
curl -s http://localhost:8080 | grep -i "nabaztag\|ssid\|wifi"
```

Expected: HTML containing the WiFi setup form (SSID selector, key field, etc.).

```bash
# 3. Open in browser
open http://localhost:8080   # mac
xdg-open http://localhost:8080  # linux
```

Expected: the classic Nabaztag blue-LED WiFi configuration wizard.

## What was changed (sdk branch, commit bb6e48f)

| File | Change |
|------|--------|
| `tools/mtl_linux/src/simu/linux/simu.c` | `getButton()` returns `1` (hardcoded; button always pressed in sim) |
| `tools/mtl_linux/Taskfile.yaml` | `mtl:simulate` accepts `PORT` var; adds `-p PORT:80` to docker run when set |
| `Taskfile.yaml` | New `boot:simulate` task: preprocess with `SIMU BOOT` defines → simulate with `PORT=8080` |

## Notes

- `getButton()` is hardcoded to `1` for now. Every simulation will see
  button-pressed. Fine for boot testing; can be made dynamic later with a
  `--button` CLI flag to `mtl_simu`.
- To run on a different host port: `task simulate:boot PORT=9090`
- Stop the simulation with Ctrl-C.
