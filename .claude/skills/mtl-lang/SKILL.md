---
name: mtl-lang
description: Writing or debugging MTL source (.mtl files under src/, lib/, test/) — the compiler's parsing/typechecking quirks and how to read its error output. Use whenever editing an .mtl file, chasing a "Syntax error"/"Typechecking error"/"is EMPTY"/"?OM Error", or adding a new lib module.
---

# MTL language gotchas

Full rationale: `CLAUDE.md` → "MTL language gotchas". Testing conventions: `test/README.md`.

- **A compiler error's reported line is the closing bracket of the enclosing
  function, not the offending line.** Binary-search by commenting out chunks of
  the file (or included test files) to isolate the real line fast — don't stare
  at the reported line.
- **`::` (cons) binds tighter than function application.**
  `f x :: y :: nil` parses as `f (x :: y :: nil)`, not `(f x) :: y :: nil`.
- **A negative literal in non-first argument position parses as binary minus.**
  `fun "v" -1` is `(fun "v") - 1`. Force a negative literal with `(0-1)`.
- **`strcmp` returns sign only** (-1/0/1) — not a character-difference int like C stdlib.
- **`assert_equalS` breaks on strings containing `\0`.** Compare byte-by-byte
  with `strget` instead.
- **No local function definitions** — `let fun` is invalid. Hoist to top level.
- **`if-then` with no `else` types the false branch as `I` (0).** Always add
  `else nil` when the true branch returns a list, or downstream code that
  expects a list will choke on a bare `0`.
- **Duplicate `fun` definitions are legal; a call site binds whichever
  definition was most recent *at its point of compilation*.** A later
  redefinition does NOT retroactively rebind earlier callers. This is used
  deliberately (e.g. piper's `tcpudp_emu.mtl` override, `test/lib/_test.mtl`'s
  stubs) — put overrides/stubs *before* the `include` of whatever consumes them.
- **But duplicate definitions of the same name still share one type** — the
  checker unifies all signatures. A stub must be type-compatible with the real
  implementation (can't stub `I` where the real fn takes `Tcp`).
- **`(expr).field` doesn't parse as a function argument.** Bind it first:
  `let sock.sockCnx -> cnx in f cnx.field`.
- **Every declared `proto` needs a real definition somewhere in the compiled
  program**, or the compiler fails late with `<name> is EMPTY !!!` and no
  file/line. When isolating a module for a standalone test, stub *every* proto
  it declares — not just the ones the test path calls.
- **The simulator heap is small (~200K words).** `#GC` log lines show
  `used=NN%`; a healthy run stays low. A jump to `used=99%` then `?OM Error`
  means a runaway allocation — almost always an infinite loop (a parser branch
  that consumes no input and doesn't advance its cursor) or a big structure
  (dict, table) being rebuilt every call instead of once at init.

## Before concluding a build is broken

`task build:*` and `task test` always exit 0 even on a real MTL failure — the
compiler/simulator report fatal errors on stderr but never fail the process.
The Taskfile wrapper greps for `Syntax error`/`Typechecking error`/`is EMPTY`/
`?OM Error` to turn that into a real exit code — trust the exit code, don't
eyeball the output yourself and don't assume 0 means clean.
