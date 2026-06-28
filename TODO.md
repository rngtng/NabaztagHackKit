# NabaztagHackKit — Remaining Work

Items are independent unless noted. Ranked roughly by effort/impact within each section.

---

## lib/ — minor cleanups

### 1. Remove dead app-level Forth copies
`src/app/forth/interpreter.mtl` and `src/app/forth/output.mtl` are not included
anywhere — `src/app/forth/forth.mtl` already pulls them in via `#include lib/forth`.
Delete both files.

### 2. lib/sse_server.mtl — eliminate internal write duplication
`lib/sse_server.mtl` reimplements `_sse_write` / `_sse_send` because it currently
includes only Layers 0–1. Fix: add `#include "lib/sock.mtl"` and replace the two
private helpers with `sock_write` / `sock_send`. Also replace `[sockCnx:cnx]` with
`sock_create cnx nil`. Removes the only remaining DRY violation in `lib/`.

### 3. lib/md5.mtl — document non-reentrancy
`md5.mtl` uses module-level global state (`md5bloc` etc.). Add a one-line comment
above the file noting sequential-call-only requirement. No code change needed.

---

## src/app2/ — MVP app path

`src/app2/` is an intentional slimmed-down rewrite of `src/app/` — no choreography,
no MIDI, no XMPP. Current status: compiles; stubs chor/MIDI/info/streaming modules.

Open questions to resolve:

- **Replace or coexist?** Decide whether `app2/` supersedes `app/` (rename to
  `src/app/`, archive old) or remains a separate build target (`task app2:compile`).
- **Missing telnet server**: `app2/main.mtl` has `#ifdef TELNETSERVER` but
  `src/app2/srv/telnet_server.mtl` doesn't exist yet. Port from `src/app/srv/`.
- **SSE relay**: `app2/` doesn't wire button/ears/rfid → `sse_broadcast`. Port
  the event hooks from `src/app/hw/` or include `src/app/srv/sse_server.mtl`.
- **Utils vs lib**: `app2/main.mtl` still includes `utils/utils.mtl`,
  `utils/json.mtl`, etc. — internal copies. Migrate to `lib/` equivalents and
  delete the `utils/` subtree once the app layers can resolve the module-name
  shadowing (see below).

---

## src/app/ — module shadowing

`src/app/` has its own `utils/json.mtl`, `utils/url.mtl`, `utils/b64.mtl`, etc.
that duplicate `lib/json.mtl` etc. The historic reason was that `lib/` didn't exist.
Now that `lib/` is canonical, these can be removed and replaced with `#include lib/…`
at their call sites — but it requires auditing each `utils/` file for app-specific
additions that need to be folded into `lib/` first.

---

## src/boot/ — unification roadmap

Detailed per-item roadmap lives in `src/boot/TODO.md`. Summary:

- **Dead code** (4 quick deletions in boot.mtl / wifi.mtl / config.mtl / config_server.mtl)
- **config.mtl unification** — boot and app use incompatible flash layouts; requires
  a migration step before they can share one file.
- **wifi / http / ipv4 / dns** — functionally identical to app versions; can be
  unified once the task-scheduler type (`Task`) is in `lib/task.mtl`.
- **Preprocessor**: switch `boot:compile` from `mtl_merge` (single-level) to
  `preproc.py` (recursive, handles `#ifdef`). Enables sub-modules to guard
  boot-specific code with `#ifdef BOOT` instead of language-level blocks.

---

## Tests — coverage gaps

Current `test/lib/` covers: b64, buffer, integer, json, list, net, sse_server, string, url.

Missing:
- `lib/forth/` — interpreter, stack, arithmetic, control flow, string words, list words
- `lib/http_server.mtl` — request parsing, response formatting
- `lib/sock.mtl` — sock_create, sock_send, sock_close_after
- `lib/md5.mtl` — hash correctness against known vectors
