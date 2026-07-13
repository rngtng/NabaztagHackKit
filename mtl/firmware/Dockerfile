# Native (multi-arch) toolchain for the NabGCC toolkit — runs on
# both Intel and Apple Silicon.
#
# Bundles:
#   gcc-arm-none-eabi + newlib-nano  -> the ARM7TDMI firmware (task compile)
#   gcc + libc6-dev                  -> the native VM test harness (task vm)
#
# The firmware expects the prebuilt boot bytecode from mtl_linux in src/simu/dumpbc.c
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        gcc-arm-none-eabi \
        libnewlib-arm-none-eabi \
        gcc \
        libc6-dev \
        make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work

CMD ["make"]
