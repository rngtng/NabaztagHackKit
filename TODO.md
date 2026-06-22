# NabaztagSDK — TODO / roadmap

Actionable steps and parked ideas. The *why* lives in [NABAZTAG_SDK.md](NABAZTAG_SDK.md);
this is the *do-this-next*. Ordered by dependency. `⛔` = gates later work.

Legend: `[ ]` todo · `[~]` in progress · `[x]` done · `(net-new)` = doesn't exist in any
upstream · `(vendor)` = copy from an existing repo.

---

## Phase 0 — Repo reboot
- [x] Clone `NabaztagHackKit` → `nabaztag-sdk`, branch `v2`, origin = rngtng/NabaztagHackKit
- [x] Move design doc in (`NABAZTAG_SDK.md`)
- [ ] **Gut the old gem** (confirm first — destructive): delete `Gemfile`, `*.gemspec`,
      `Rakefile`, `lib/`, `spec/`, `ext/`, old `bytecode/_original`. Keep `bytecode/_docs/`
      (grammar/commands) → move to `docs/`. Replace `README.md` + `CHANGELOG.md`.
- [ ] Lay down the `tools/ firmware-c/ mtl/ forth/ docs/ reference/` skeleton (§5)
- [ ] Add `PROVENANCE.md` (origin repo + commit SHA + local-changes per vendored tree)
- [ ] Decide: push `v2` to origin now, or keep local until skeleton builds? (see open Q)

## Phase 1 — Toolchain parity spike ⛔ (do first, gates §4.1 split)
- [ ] Build `mtl_linux` compiler + piper `mtl_simu`
- [ ] Compile one sample `.mtl` with `mtl_linux`, run the bytecode in piper's `mtl_simu`
- [ ] Diff opcode sets against `vbc.h` (152 opcodes). Record result in PROVENANCE/CHANGELOG
- [ ] **If they drift** → collapse to a single toolchain (likely all-piper, per tie-break)

## Phase 2 — Vendoring (copy, no submodules)
- [ ] (vendor) nabgcc → `firmware-c/` (Layer 0 C VM + drivers + WPA2 + flashing)
- [ ] (vendor) mtl_linux C++ src → `tools/mtl-compiler/`
- [ ] (vendor) piper `compiler/mtl_simu` C++ src → `tools/mtl-simu/`
- [ ] (vendor) piper `firmware/` MTL + `mtl_library/lib` → `mtl/lib/` (one library, §8)
- [ ] (vendor) piper `vl/` (strip weather/HA) → `forth/`
- [ ] (vendor, read-only) firmware_nabaztag → `reference/` (provenance, never built)
- [ ] Record every vendored tree's origin SHA in `PROVENANCE.md`

## Phase 3 — Build system (Docker + Task, low-dep)
- [ ] Port `Dockerfile` + `Dockerfile.mtl` from nabgcc; **drop `php-cli`** (§7)
- [ ] Root `Taskfile.yaml` with `includes:` → per-layer namespaces
      `tool: / fw: / boot: / mtl: / forth: / dev:` (§6)
- [ ] (net-new) Port `preproc.pl` (Perl) → **Python** in `tools/pack/` ; keep
      `__DATE__`/revision injection; fold in `preproc_remove_extra_protos`
- [ ] Adopt `firmware_nabaztag/utility/mkfirmware.py` (Python `.sim` packer) over the PHP;
      wire as `fw:flash:sim`
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
- [ ] `mtl:simu` — piper `mtl_simu` (app on host, faked HW)
- [ ] `mtl:test` — mtl_library MTL assertion framework in the simulator
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
