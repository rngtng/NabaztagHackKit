# app-piper — the full MTL app

The production MTL app (vendored from `nabaztag-piper`, a ServerlessNabaztag
fork — see [`PROVENANCE.md`](../../../PROVENANCE.md)): HTTP + telnet runtime
serving the Forth interpreter, choreographies, audio, RFID and ear/LED
behaviour. Most generic code has been extracted into
[`mtl/lib/`](../../lib/README.md); what remains here is piper's app policy
(config flash blob, servers, `assets/` content).

```sh
task mtl:app-piper:build            # compile to device bytecode
task mtl:app-piper:simulate         # run in the mtl_linux simulator
task mtl:app-piper:simulate:mock    # end-to-end: sim + mock HTTP server serving assets/
```

New apps should start from [`mtl/apps/template/`](../template/README.md), not
from piper.
