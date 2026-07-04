# app-template — start here for a new app

A new app is **assembly + business logic**; everything else comes from `lib/`:

- `main.mtl` — picks the lib building blocks (TCP adapter, HTTP server, Forth
  core) and starts them. You rarely touch this beyond adding includes.
- `app.mtl` — the business logic. This template serves a Forth playground:
  `GET /` returns a page, `POST /eval` runs the body through the Forth core.

## Run it (simulator)

```sh
task simulate:app          # config UI on http://localhost:8080
curl -s localhost:8080/
curl -s -d '2 3 + .' localhost:8080/eval
# -> {"output": "5", "stack": ""}
```

## Build device bytecode

```sh
task build:app SOURCE=src/app-template/main.mtl
```

Note: running standalone **on the rabbit** additionally needs WiFi/DHCP/DNS
bring-up, which still lives in `src/app-piper/{ipv4,net}` — extracting that
stack into `lib/` is a planned follow-up (see TODO.md). Until then, device
apps follow the app-piper pattern; this template is the simulator-first
blueprint for composing lib blocks.

## Make it yours

1. Copy this folder: `cp -r src/app-template src/app-<name>`
2. Rewrite `app.mtl` (keep `handle_request`, or drop HTTP entirely and use
   other blocks — `lib/net/sse_server.mtl`, `lib/sys/task.mtl`, …).
3. `task simulate:app SOURCE=src/app-<name>/main.mtl`
