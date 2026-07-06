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

`app.mtl` wires `forth_init_dictionary` to
`set forth_dictionary = [dict: conc app_forth_words forth_core_words];`
(mirroring `src/app-piper/forth/dictionary.mtl`'s pattern), so `/eval` gets
the **generic core** (`lib/forth/dictionary.mtl`, which now includes the
memory/variable words) plus two shared opt-in packs — the time/date pack
(`lib/forth/time.mtl`, over `lib/sys/time.mtl`) and the task-control pack
(`lib/forth/task.mtl`, over `lib/sys/task.mtl`), both shared with app-piper —
and no hardware or network words. Add more by extending `app_forth_words` the
same way.

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

**Meta**
| Word | Effect |
|---|---|
| `evaluate` | `str --` run a string as nested Forth code against the same interpreter state |

**Memory** (`lib/forth/memory.mtl`, part of the core — a generic cell allocator with no hardware/config cells, unlike app-piper's richer version)
| Word | Effect |
|---|---|
| `allocate-cell` | `-- addr` |
| `free-cell` | `addr --` |
| `variable` | `"name" --` (immediate) define a variable, allocating a cell for it |
| `@` | `addr -- x` fetch |
| `!` | `x addr --` store |
| `+!` | `x addr --` add to the cell in place |
| `?` | `addr --` print the cell's contents |
| `state` | `-- addr` the interpreter state cell (0 normal, -1 compiling); `state @` to read it |

**Time** (`lib/forth/time.mtl`, thin wrappers over `lib/sys/time.mtl`, shared with app-piper)
| Word | Effect | Notes |
|---|---|---|
| `uptime` | `-- s` | seconds since the process started |
| `time-ms` | `-- ms` | monotonic millisecond clock |
| `time?` | `-- flag` | true once the wall clock has been synced from the time server |
| `update-time` | `--` | trigger an NTP fetch (device only; no-op in the simulator, which has no UDP stack) |
| `time&date` | `-- sec min hour day month year` | local wall-clock fields |
| `local>string` | `-- str` | local time as `Day, DD Mon YYYY HH:MM:SS <city>` |
| `utc>string` | `-- str` | UTC time as `Day, DD Mon YYYY HH:MM:SS GMT` |
| `ms` | `delay --` | sleep `delay` ms (async; yields to the scheduler) |

The wall-clock words read the timezone/DST config through `config_get_city_code`
/ `config_get_dst`. On device these (and the WiFi/IP/proxy accessors) come from
`lib/net/config_defaults.mtl`, included by `app.mtl`'s `#ifndef SIMU` block;
override any field by `#define`-ing `CONFIG_CITY_CODE` / `CONFIG_DST` / … before
that include (see `lib/README.md`). The simulator uses a fixed `UTC` / no-DST
stub. Without a synced clock — always the case in the simulator — they report
zeros / the epoch.

```sh
curl -s -d 'uptime .' localhost:8080/eval
# -> {"output": "0", "stack": ""}
curl -s -d 'utc>string' localhost:8080/eval
# -> {"output": "", "stack": "Mon, 00  0000 00:00:00 GMT"}
```

**Task** (`lib/forth/task.mtl`, exposing `lib/sys/task.mtl`'s scheduler — the
same one `main.mtl` already runs via `loopcb #task_scheduler`). A backgrounded word
runs on the scheduler thread, so it can loop or repeat without blocking the
HTTP handler that serves `/eval`.

| Word | Effect | Notes |
|---|---|---|
| `task-start` | `text delay name --` | run `text` (a Forth string) every `delay` ms as a task named `name`; the task id is shown by `.tasks` (ids start at 1) |
| `task-stop` | `task-id -- flag` | remove the task; `flag` is true if that id existed |
| `task-suspend` | `task-id -- flag` | pause the task (stays listed as `suspend`) |
| `task-resume` | `task-id -- flag` | resume a suspended task |
| `.tasks` | `--` | print the task table: id, status, name, last-run-ms `/`period-ms |

Background a word that increments a variable every second, then watch it climb
(memory persists across `/eval` calls — see below):

```sh
curl -s -d 'variable counter  0 counter !' localhost:8080/eval
curl -s -d '"1 counter +!" 1000 "inc" task-start' localhost:8080/eval
curl -s -d '.tasks' localhost:8080/eval
# -> {"output": "    1 run     inc                           -1/1000\n \n", "stack": ""}
sleep 3
curl -s -d 'counter @' localhost:8080/eval
# -> {"output": "", "stack": "3"}
curl -s -d '1 task-suspend .' localhost:8080/eval   # freeze it (-1 = true)
curl -s -d '1 task-resume  .' localhost:8080/eval   # or resume
curl -s -d '1 task-stop    .' localhost:8080/eval   # or remove it entirely
```

Try it:
```sh
curl -s -d ': square dup * ; 5 square' localhost:8080/eval
curl -s -d ': count5 0 begin 1+ dup 5 >= until ; count5' localhost:8080/eval
curl -s -d ': pick10 if 10 else 20 then ; true pick10' localhost:8080/eval
curl -s -d '"2 3 +" evaluate .' localhost:8080/eval
# -> {"output": "5", "stack": ""}
```

Memory persists across `/eval` calls **for as long as the process runs**
(the simulator/device is one long-lived process; a restart resets
`forth_memory`/`forth_dictionary` and any variables defined so far):
```sh
curl -s -d 'variable counter  0 counter !' localhost:8080/eval
curl -s -d '1 counter +!  counter @' localhost:8080/eval
# -> {"output": "", "stack": "1"}
curl -s -d '1 counter +!  counter @' localhost:8080/eval
# -> {"output": "", "stack": "2"}
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
