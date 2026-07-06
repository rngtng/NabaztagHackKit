# Mkfirmware

Transform a Nabaztag firmware `.bin` into the signed `.sim` format the web
config page accepts.

## Usage

Run directly from this directory:

    task sign SOURCE=Nab.bin                  # → firmware.sim (signed)
    task sign SOURCE=Nab.bin OUT=fw.sim       # override the output name
    task verify FILE=fw.sim                   # check a .sim is well-formed
    task clean                                # remove the mkfirmware image

## Full build pipeline

From the repo root, one task chains boot → firmware → sign → verify:

    task build:sim                            # → build/firmware/Nab.sim (signed + verified)

It runs, in order:

    task build:boot                           # 1. build boot bytecode → build/boot/
    task build:firmware                       # 2. compile C firmware  → build/firmware/Nab.bin
    task mkfirmware:sign   SOURCE=... OUT=...  # 3. sign  → build/firmware/Nab.sim
    task mkfirmware:verify FILE=...            # 4. check the .sim is well-formed

The resulting `Nab.sim` can be uploaded via the rabbit's blue-LED config page.
