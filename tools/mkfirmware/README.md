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

The full pipeline — compile C firmware, then sign it — is:

    task build:boot                           # 1. build boot bytecode → build/boot/
    task build:firmware                       # 2. compile C firmware  → build/firmware/
    cd tools/mkfirmware && task sign SOURCE=../../build/firmware/Nab.bin

The resulting `firmware.sim` can be uploaded via the rabbit's blue-LED config page.
