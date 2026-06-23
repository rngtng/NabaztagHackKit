# Provenance

Sources are **vendored** (copied, not submodules — see [NABAZTAG_SDK.md](NABAZTAG_SDK.md)
§5). Each entry records origin repo + commit so changes can be diffed and backported
upstream. "Local changes" lists everything we altered from the vendored copy.

---

## `tools/mtl_linux/`
- **Origin:** https://github.com/rngtng/mtl_linux — the MTL toolchain: **compiler AND
  simulator** (rngtng).
- **Commit:** `7606eb3ec7afa40d6af4c1d33165068e24ac07e3` (upstream HEAD). Vendored straight
  from upstream — not via nabgcc's old submodule pin. The Docker/Task/README/LICENSE and
  the GCC-14.2 + macOS + warning compile fixes all landed upstream (commits `547ce0c..7606eb3`),
  so the backport bridge is already crossed for this tree.
- **Contents:** source only — `src/ inc/ utils/ Makefile config.txt`. Build artifacts and
  the bundled `nominal.mtl` fixture are excluded; `SOURCE` is supplied at run time instead.
- **Local changes:**
  - `Taskfile.yaml` — parameterized `IMAGE`/`DIR` (so the root Taskfile injects
    `IMAGE=nabaztag-sdk`, `DIR=./tools/mtl_linux/`) and added `-w /work` to the compile run;
  - `config.txt` — dropped the hard-coded `SOURCE nominal.mtl` line (source is a run-time
    arg now), kept the `MAC` default.
- **Built by:** the tools are compiled INTO the image (`Dockerfile` runs `make comp simu`,
  copies in `config.txt`); the Taskfile exposes two verbs, surfaced at the root as
  `task mtl:compile` / `task mtl:simulate` (image `nabaztag-sdk`). Verified: compiles a
  minimal program → bytecode; simulator runs from the baked `config.txt`.
- **Role:** the **single toolchain** — compile AND simulate (§4.1, decided after
  comparing both simulators). The only gap vs piper's simu is an HTTP file server, being
  cherry-picked in (~200 LOC, piper's `http_server.c`) rather than forking a second VM.

> ⚠ **Build gotcha:** `Makefile` is matched by a *global* git ignore (`makefile`, case-
> insensitive) and so is currently **not committed** — the Dockerfile `COPY Makefile` step
> fails on a fresh clone. Fix: add `!tools/mtl_linux/Makefile` to the repo `.gitignore` and
> `git add -f tools/mtl_linux/Makefile`.
