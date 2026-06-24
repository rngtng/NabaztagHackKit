# Mkfirmware

Transform a Nabaztag firmware `.bin` into the signed `.sim` format the web
config page accepts. Standalone targets (also exposed from the repo root as
`utils:sign` / `utils:verify`):

    task sign SOURCE=Nab.bin                  # → firmware.sim (signed)
    task sign SOURCE=Nab.bin OUT=fw.sim       # override the output name
    task verify FILE=fw.sim                   # check a .sim is well-formed
    task clean                                # remove the mkfirmware image

From the repo root the whole pipeline is just `task firmware` (compile → sign →
verify → bin/Nab.sim).
