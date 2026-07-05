# app-template — start here for a new app

A new app is **assembly + business logic**; everything else comes from `lib/`:

- `main.mtl` — picks the lib building blocks (TCP adapter, HTTP server, Forth
  core) and starts them. You rarely touch this beyond adding includes.
- `app.mtl` — the business logic. This template serves a Forth playground:
  `GET /` returns a page, `POST /eval` runs the body through the Forth core.

## Run it (simulator)

```sh
task simulate:app:template   # config UI on http://localhost:8080
curl -s localhost:8080/
curl -s -d '2 3 + .' localhost:8080/eval
# -> {"output": "5", "stack": ""}
```

## Forth commands available today

`main.mtl` wires `fun forth_init_dictionary= forth_init_core_dictionary;;`
(`lib/forth/dictionary.mtl`), so `/eval` only gets the **generic core** — no
hardware, network, memory/variable, or task words. Those exist (see
`src/app-piper/forth/dictionary.mtl`) but aren't included here — an app adds
them by prepending its own entries to `forth_core_words` before calling
`forth_init_core_dictionary` (see `src/app-piper/forth/dictionary.mtl` for
the pattern).

Stack effect notation: `( before -- after )`, top of stack on the right.

**Compilation & dictionary**
| Word | Effect | Notes |
|---|---|---|
| `:` `;` | `: name ... ;` | Define a new word. `if`/`case` etc. only work inside `: ... ;` (compile mode). |
| `constant` | `x "name" --` | `42 constant answer` |
| `defined?` | `"name" -- flag` | |
| `words` | `--` | Lists every word in the dictionary |
| `(` `)` | — | Comment, skipped by the tokenizer |

**Stack manipulation**
| Word | Effect |
|---|---|
| `dup` | `n -- n n` |
| `drop` | `n --` |
| `swap` | `n1 n2 -- n2 n1` |
| `over` | `n1 n2 -- n1 n2 n1` |
| `rot` | `n1 n2 n3 -- n2 n3 n1` |
| `pick` | `... i -- ... ni` |
| `nip` | `n1 n2 -- n2` |
| `tuck` | `n1 n2 -- n1 n2 n1` |
| `?dup` | `x -- 0 \| x x` |
| `depth` | `-- count` |
| `>r` `r>` `r@` `rdrop` `rdepth` | move to/from the return stack |
| `2>r` `2r>` `2r@` | two-item return-stack versions |

**Comparison** — `<` `<=` `=` `>` `>=` `<>` (all `n1 n2 -- flag`), `<0` `0=` `>0` `0<>` (all `n -- flag`)

**Arithmetic**
| Word | Effect |
|---|---|
| `+` `-` `*` `/` | `n1 n2 -- result` |
| `1+` `1-` `2+` `2-` | `n -- n'` |
| `mod` | `n1 n2 -- remainder` |
| `/mod` | `n1 n2 -- remainder quotient` |
| `*/` | `n1 n2 n3 -- (n2*n1)/n3` |
| `*/mod` | `n1 n2 n3 -- remainder quotient` |
| `min` `max` | `n1 n2 -- result` |
| `abs` | `n -- \|n\|` |

**Logical** — `and` `or` `xor` (`n1 n2 -- flag`), `invert` (`n -- flag`), `true`, `false`

**Output**
| Word | Effect |
|---|---|
| `.` | `n --` print top of stack |
| `.s` | `--` print the whole stack, non-destructively |
| `emit` | `char --` |
| `cr` | `--` newline |
| `space` | `--` one space |
| `bl` | `-- char` push the space character |
| `read-line` | `-- line` (async: waits for input) |

**List** — `nil` (`-- list`), `::` cons (`list x -- list'`), `hd` (`list -- list x`), `tl` (`list -- list'`), `nth` (`list/str n -- x`), `count` (`list/str -- n`), `str-join` (`list -- str`)

**String**
| Word | Effect |
|---|---|
| `$repeat` | `s n -- s'` |
| `pad-right` / `pad-left` | `s n c -- s'` |
| `lower` / `upper` | `s -- s'` |
| `url-encode` / `url-decode` | `s -- s'` |
| `>number` | `s -- n` |

**JSON** — `json-parse` (`str -- list/dict`), `json-get` (`w path -- x`, dotted/bracket path e.g. `"a.b"`, `"a[1]"`)

**Control** (only valid inside `: ... ;`) — `if` / `else` / `then`, `begin` / `until`, `case` / `of` / `endof` / `endcase`, plus the lower-level `jmp` / `?jmp`, `abort` (clears both stacks), `exit`

Try it:
```sh
curl -s -d ': square dup * ; 5 square' localhost:8080/eval
curl -s -d ': count5 0 begin 1+ dup 5 >= until ; count5' localhost:8080/eval
curl -s -d ': pick10 if 10 else 20 then ; true pick10' localhost:8080/eval
```

## Build device bytecode

```sh
task build:app:template
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
3. `task simulate:app TARGET=app-<name>`
