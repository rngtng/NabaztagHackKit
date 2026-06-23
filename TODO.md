# NabaztagSDK — TODO / roadmap

Actionable steps and parked ideas. The *why* lives in [NABAZTAG_SDK.md](NABAZTAG_SDK.md);
this is the *do-this-next*. Ordered by dependency. `⛔` = gates later work.

Legend: `[ ]` todo · `[~]` in progress · `[x]` done · `(net-new)` = doesn't exist in any
upstream · `(vendor)` = copy from an existing repo.

---

## ▶ NEXT SESSION — start here: the **precompiler**
It's the next build step and it **gates `bc.c` → firmware**: `mtl_compiler` eats ONE flat
`.mtl`, but boot/app are multi-file (`#include`/`#ifdef`). The precompiler flattens them —
it's what produced the `nominal.mtl` we deleted. True order:
`compiler → precompiler → (preprocess+compile boot.mtl) → bc.c → firmware-c`.

**Resolve these two deferred decisions first, then build (Phase 3 precompiler item):**
1. **Compile UX** — (a) transparent one verb `mtl:compile -- <entry.mtl> <out.bin>`
   (auto-preprocess then compile) via two chained images [recommended], (b) same but one
   image (python3 baked into the mtl image), or (c) two explicit verbs
   `mtl:preprocess` + `mtl:compile`.
2. **Tool home** — `tools/pack/` (Python tooling: also future `mksim.py`, `extract_words.py`)
   [recommended] vs `tools/mtl_linux/`.

## Phase 0 — Repo reboot
- [x] Clone `NabaztagHackKit` → `nabaztag-sdk`, branch `v2`, origin = rngtng/NabaztagHackKit
- [x] Move design doc in (`NABAZTAG_SDK.md`)
- [x] **Gut the old gem**: removed `Gemfile`, gemspec, `Rakefile`, `lib/`, `spec/`,
      `ext/` (+ `mtl_linux` submodule), Ruby `examples/`, rubocop/travis, old `bytecode/`.
      Kept `bytecode/_docs/` → moved to `docs/`. (history preserved on this branch)
- [ ] Fresh `README.md` + `CHANGELOG.md` (gem ones removed; replacements pending)
- [ ] Refresh `.gitignore` (still the Ruby one) — add `tools/bin/`, build artifacts
- [ ] Lay down the `tools/ firmware-c/ mtl/ forth/ reference/` skeleton (§5) — `docs/` exists
- [ ] Add `PROVENANCE.md` (origin repo + commit SHA + local-changes per vendored tree)
- [ ] Decide: push `v2` to origin now, or keep local until skeleton builds? (see open Q)

## Phase 1 — Toolchain decision ✅ DECIDED: single-toolchain (mtl_linux)
- [x] Build `mtl_linux` compiler **and** simulator — both build & run.
- [x] **Evaluated mtl_linux simu vs piper simu** (subagent): code-identical or stub except
      piper's ~191-line HTTP file server. Same `vbc.h` → bytecode-compatible, no parity
      risk. **Decision: use mtl_linux for compile AND simu; drop the split + parity spike.**
- [ ] (net-new, small) **port piper's `http_server.c` → mtl_linux simu**: copy
      `nabaztag-piper/compiler/mtl_simu/http_server.{c,h}` (~191 LOC), wire into the
      accept() handler in `src/simu/linux/simunet.c`, add `--http_server_path` /
      `--http_server_port` to `main.cpp`, add an `http_server` log type. (~200 LOC, <2h)
      → lets `mtl:simu` serve `bc.jsp` + `.forth` straight from the simulator.

## Phase 2 — Vendoring (copy, no submodules)
- [x] (vendor) **mtl_linux → `tools/mtl_linux/`** (rngtng@7606eb3, upstream HEAD), self-contained.
      Tools built INTO the image (`make comp simu`); Taskfile = two verbs `mtl:compile` /
      `mtl:simulate`. Verified compile → 41 KB. config.txt drops hard-coded SOURCE. ✅
      ⚠ `Makefile` is caught by a global git-ignore → not committed; `git add -f` it (+ a repo
      `!Makefile` un-ignore) or fresh clones can't build.
- [ ] (vendor) nabgcc → `firmware-c/` (Layer 0 C VM + drivers + WPA2 + flashing) —
      needs a real `bc.c`: generate via `mtl:compile` (bootstrap now unblocked)
- [ ] (vendor) piper `firmware/` MTL → `mtl/lib/` — the stdlib (piper wins: real
      audio/LED/list/net). **Do NOT take `mtl_library/lib`** (buffer/integer/list/system —
      superseded by piper).
- [ ] (vendor) `mtl_library/test/` → `mtl/test/` — the **only** MTL assertion framework
      (`assertions.mtl` + `helper.mtl`); piper has none. Deps: `lib/echo.mtl` + list
      helpers (`listtostr`/`strcatlist`/`TLtoS`). Keep `test/{lib,native}/*_test.mtl` as
      patterns, repoint at piper's modules.
- [ ] (vendor) piper `vl/` (strip weather/HA) → `forth/`
- [ ] (vendor, read-only) firmware_nabaztag → `reference/` (provenance, never built)
- [ ] Record every vendored tree's origin SHA in `PROVENANCE.md`

## Phase 3 — Build system (Docker + Task, low-dep)
- [~] Self-contained per-folder Dockerfiles (§5); **drop `php-cli`** (§7).
      `tools/mtl_linux/Dockerfile` done; native `firmware-c/Dockerfile`
      (arm-gcc + gcc + make) pending with `firmware-c/`
- [~] Root `Taskfile.yaml` with `includes:` → per-layer namespaces (§6).
      `mtl:compile` / `mtl:simulate` live; `fw:/forth:/dev:` pending
- [ ] **◀ NEXT — gates `bc.c`/firmware (see top).** (net-new) Port `preproc.pl` (Perl) →
      **Python** (merge the two diverged copies: mtl_library + piper). Features:
      `#include` / `#define` / `#ifdef`, `__DATE__`/revision injection; fold in
      `preproc_remove_extra_protos` (drop dup forward-decls) + mtl_library's
      `preproc_find_real_file.pl` (preprocessed-line → source-file mapping; piper lacks it).
      Decide compile-UX + tool-home first (top of file).
- [ ] (net-new) `tools/pack/mksim.py` — one clean Python 3 module replacing nabgcc's PHP
      packer. Port the Violet cipher from `firmware_nabaztag/utility/`:
      `mkfirmware.py` (pack `.sim`) + `mkboot.py` (pack `bc.jsp`, no header skip) +
      **`mkuncript.py` (unpack — nabgcc has no decryptor; enables round-trip verify)**.
      Add CLI args (originals hardcode filenames); skip `mkdump.py` (broken, objcopy
      already yields raw `.bin`). Wire as `fw:flash:sim`.
      Note: `vlispemu.exe` = Win-only VLISP simulator → superseded by `mtl_simu` (`mtl:simu`);
      keep the `.exe` in `reference/` only.
- [ ] Port `extract_words.py` as `mtl:words`
- [ ] Verify the whole thing builds with only **Docker + Task** on host (mac + Linux)

## Phase 4 — One MTL library, composed entrypoints (§8)
- [ ] Split vendored MTL into reusable modules: `forth/ stdlib/ net/ audio/ hw/ chor/ utils/`
- [ ] (net-new) `mtl/boot.mtl` — modern buildable boot: net bring-up + `conf.bin` +
      config web page + `bc.jsp` fetch-and-run, cherry-picked from piper `boot/*.mtl` +
      `firmware/net`. **Neither upstream ships this buildable** (§3.2)
- [ ] `mtl/app.mtl` — full app → `bc.jsp` (default, remote-load model)
- [ ] `#ifdef MONOLITHIC` — embed app into boot for the standalone/recovery image (§3.2)
- [ ] `#ifdef SIMU` — host-sim shims

## Phase 5 — Device I/O (net-new wins)
- [ ] (net-new) `dev:upload` (Python) — POST a `.sim` to the rabbit's on-device config
      page; kills the manual browser upload. Device side already supports it
      (`startconfigserver` + `getbinary` + `reboot` page 3) (§9.2)
- [ ] `dev:deploy` — push `bc.jsp` + `*.forth` to the web server (T1/T2 OTA)
- [ ] `forth:repl` — telnet REPL helper; document the `"init.forth" load-srv` hot-reload
- [ ] (idea) host-side `conf.bin` read/write tool — set WLAN/server-url without the
      browser config page at all

## Phase 6 — Test & simulate (keep all three levels, §4.4)
- [ ] `fw:test` — nabgcc `testvm` C-VM harness
- [ ] `mtl:simulate` — mtl_linux simu (app on host, faked HW) — already wired (Phase 2)
- [ ] `mtl:test` — mtl_library MTL assertion framework (vendored, Phase 2) on mtl_linux's
      simu. Enhance for CI: count failures + exit non-zero (today it only prints
      `is valid` / `!! expected…`)
- [ ] (net-new) add real assertions to `testvm` (VM array bounds, EAPOL MIC vector) — it's
      smoke-only today

## Phase 7 — Docs
- [ ] Merge: nabgcc `architecture.md` + HackKit `bytecode/_docs/{grammar,commands}.md` +
      generated `words.txt` → `docs/`
- [ ] `dev:docs` (doxygen for C + the MTL refs)
- [ ] README rewrite: what the SDK is, the layer cake, quickstart (Docker + Task)

---

## Known limitations to document (not blockers)
- WPA2 **group-key (broadcast/multicast)** broken — `aes128.c` stubs + RFC-3394 unwrap
  deferred. **Unicast works.** Ship-with-doc (decision §10.7)
- 32-bit MTL image is amd64-only → emulated (slow) on Apple Silicon — fine for one-shot
- JTAG (`fw:flash:jtag`) runs host-side `openocd` — containers can't reach USB
- `.jsp` is a filename only (legacy), not Java — the device requests that exact path

## Open questions / parking lot
- [ ] Push `v2` to GitHub now, or after the skeleton builds? (clean history vs early backup)
- [ ] Homebrew: discontinue `mtl_library` tap, or rename → `homebrew-nabaztag-sdk` for a
      native (non-Docker) install path?
- [ ] Bytecode-loader bounds/recursion caps — only matters if `bc.jsp` is ever untrusted;
      in the serverless model you serve your own. Leave as a documented assumption?
- [ ] Internal rename of `bc.jsp` (wire path is fixed; cosmetic only) — worth it?
- [ ] Do we ever want to *own* Layer 0 boot fully (replace the Violet bootloader on flash),
      or always assume the original boot is present and only flash the C VM + our boot.mtl?
