# Mkfirmware

Transform a Nabaztag firmware `.bin` into the signed `.sim` format the web config
page accepts.

## Full build pipeline

From the repo root, one task chains boot → firmware → sign → verify:

    task mtl:firmware:package                 # → mtl/build/firmware/Nab.sim (signed + verified)

It runs, in order:

    task mtl:boot:build                       # 1. build boot bytecode → mtl/build/boot/
    task mtl:firmware:build                   # 2. compile C firmware  → mtl/build/firmware/Nab.bin
    (mkfirmware sign)                         # 3. sign  → mtl/build/firmware/Nab.sim
    (mkfirmware verify)                       # 4. check the .sim is well-formed

The resulting `Nab.sim` can be uploaded via the rabbit's blue-LED config page.

## Standalone sign / verify

The `sign` and `verify` steps are also runnable on their own from this dir:

    cd mtl/tools/mkfirmware
    task sign   SOURCE=Nab.bin [OUT=fw.sim]   # sign a .bin
    task verify FILE=fw.sim                   # check a .sim is well-formed

`.sim` layout: `-violet-` sentinel + `itoh8(2*size)` length header + hex body +
`-violet-` sentinel.
