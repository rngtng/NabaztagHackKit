# MTL Preprocessor

A C-style preprocessor for MTL source, backed by [pcpp](https://github.com/ned14/pcpp).
Handles `#include`, `#define`, `#ifdef`/`#ifndef`, `#pragma once` — plus
MTL-specific extras: extension-less includes, HTML/CSS quote escaping, and
duplicate proto removal.

## Usage

Direct Docker invocation (mount the `mtl/` tree — includes like `lib/…` resolve
against the working directory):

```
docker run --rm -v "$(pwd)/mtl:/work:ro" -w /work preprocessor boot/boot.mtl > merged.mtl
docker run --rm -v "$(pwd)/mtl:/work:ro" -w /work preprocessor --define BOOT --define SIMU -I boot boot/boot.mtl > merged.mtl
```

Local (requires `pip install pcpp`; run from `mtl/`):

```
python tools/preprocessor/preproc.py boot/boot.mtl
python tools/preprocessor/preproc.py --test   # run self-tests
```

Normally it runs as an internal step of the layer tasks (`mtl:lib:test`,
`mtl:<app>:build`, `mtl:boot:build`).

## Options

| Flag | Description |
|------|-------------|
| `-D NAME` / `--define NAME` | Define a preprocessor symbol (as `NAME 1`) |
| `-I DIR` | Add `DIR` to the include search path |
| `--test` | Run the built-in self-test and exit |
| `--find-line FILE LINENUM` | Map a merged-file line number back to its source location |

The working directory (the `mtl/` tree in our tasks) is always on the search
path, so `#include "lib/foo.mtl"` resolves from anywhere.

## Features

### Standard C preprocessor directives

```c
#include "path/to/file.mtl"
#define MY_FLAG
#ifdef MY_FLAG
  ...
#endif
#ifndef OTHER
  ...
#endif
#pragma once
```

### Extension-less includes

MTL conventionally omits the `.mtl` suffix. The preprocessor tries the bare name
first; if that resolves to a directory or doesn't exist, it retries with `.mtl`
appended.

```c
#include "util"        // resolves to util.mtl
#include "lib/forth"   // resolves to lib/forth.mtl even if lib/forth/ directory exists
```

### Implicit `#pragma once`

Every file is treated as `#pragma once` automatically; including it multiple
times is a no-op. Explicit `#pragma once` directives are also respected.

### Duplicate proto removal

MTL requires forward declarations (`proto`) before use. When a file with a
`proto foo` is included after the matching `fun foo` has already appeared, the
duplicate proto is commented out:

```mtl
// before merging, util.mtl has:
proto strstr 1;;
fun strstr s p i = ...;;

// if proto strstr appears again later in merged output:
//proto strstr 1;;   ← automatically commented out
```

### HTML/CSS embedding

Files with `.html`/`.css` extensions have their double-quotes escaped (`"` →
`\"`) on include, so they embed safely inside MTL string literals:

```mtl
var page="
#include "config.html"
";;
```

### Predefined macro

`__DATE__` is set to the current ISO-8601 timestamp (Europe/Paris timezone if
available) at preprocessing time.

## Building the Docker image

```
docker build -t preprocessor mtl/tools/preprocessor/
```

## Caveats

- `//` line and `/* */` block comments are stripped by pcpp. Inline commented-out
  code (e.g. `(/*debug*/ expr)`) is removed from the output.
- Backslash-continued lines in string literals are joined by pcpp (standard C
  behaviour). The string value is identical; only formatting changes.
- `--find-line` reports the merged-file line number only, since pcpp suppresses
  `#line` directives for MTL compiler compatibility.
