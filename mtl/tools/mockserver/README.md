# mockserver — mock HTTP file server for end-to-end simulation

The MTL simulator (`mtl/tools/mtl_linux`) uses real BSD sockets, so a running app
makes genuine outbound HTTP requests for the files its bytecode demands —
`bc.jsp` (the app bytecode), `init.forth`, and whatever that Forth script then
pulls (`*.forth`, `*.mp3`, `*.wav`, `locate.jsp`, `hooks/record.php`, …).

This layer is a stdlib-only Python (`http.server`) server that **logs every
request** and **serves the matching file** from an assets directory, or 404s. It
lets you simulate the device end-to-end against local stub or real files instead
of the dead Violet servers (#39).

## How it fits together

Assets live next to an app's `*.mtl` sources, e.g. `mtl/apps/piper/assets/`, and
are mounted read-only at `/assets`. The request path maps straight onto that
tree; a leading `/vl` prefix (the historical Violet server path) is stripped, so
both `/init.forth` and `/vl/init.forth` resolve to `assets/init.forth`.

Because the simulator dials a real socket, the orchestration task puts the sim
and this server on a shared docker network and points the sim at the server's
fixed IP with `mtl_simu --serverurl <ip>` (a literal IP, so the unwired in-sim
DNS is skipped).

## Usage

Run the full end-to-end simulation (builds the image, starts the server, runs
the sim pointed at it, streams the request log, tears everything down on exit):

```sh
task mtl:app-piper:simulate:mock      # piper (serves mtl/apps/piper/assets)
```

Each app owns its own `simulate:mock` target (#63); piper is the one wired up
today. To add another app with a `mtl/apps/<app>/assets/` folder, copy piper's
`simulate:mock` task into that app's `Taskfile.yaml`.

The mock's request log is prefixed `[mock]` in the combined output:

```
[mock] [mockserver] serving 1 file(s) from /assets
[mock] GET /init.forth -> 200 /assets/init.forth (312B text/plain)
[mock] GET /locate.jsp?sn=... -> 404
```

Run the server standalone against an assets folder (host port 8081):

```sh
docker run --rm --init -p 8081:80 -v "$(realpath mtl/apps/piper/assets):/assets:ro" nabaztag-sdk-mockserver
curl --noproxy localhost localhost:8081/init.forth
```

## Behaviour

- `GET`/`HEAD`: serve the file if present under the assets root (content-type
  guessed from extension: `.forth`→text/plain, `.mp3`→audio/mpeg, `.wav`→audio/wav,
  `.bin`→octet-stream), else 404. Path traversal is blocked.
- `POST` (e.g. `hooks/record.php`): the body length is logged; a matching file is
  served if one exists, otherwise an empty `200 OK` acknowledgement is returned.
- Every request is logged to stdout as `METHOD path?query -> status …`.

## Files

- `server.py` — the server (stdlib only).
- `Dockerfile` — `python:3.12-slim` (`scripts/claude-setup.sh` bakes the proxy CA
  in so image builds have network inside the Claude sandbox).
- `Taskfile.yaml` — `image` (internal), `serve` (standalone), `clean`.
