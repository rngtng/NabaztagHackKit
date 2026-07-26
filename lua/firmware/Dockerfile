# ARM7TDMI cross toolchain for the firmwareV2 (eLua-successor) bring-up.
# Multi-arch base (Intel + Apple Silicon), same toolchain proven by src/firmware.
#
#   gcc-arm-none-eabi + newlib-nano  -> the ML67Q4051 firmware (make APP=...)
#   gcc (host) + libasan             -> test/host's native unit tests (#241)
#
# The host gcc is NOT a second firmware toolchain: test/host builds
# board-independent firmware sources (the event core, the printf shims)
# natively against stubbed hardware, under ASan/UBSan. See test/host/README.md.
#
# Host needs only Docker + Task; everything builds in here.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        gcc-arm-none-eabi \
        libnewlib-arm-none-eabi \
        gcc \
        libc6-dev \
        make \
        python3 \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work

CMD ["make"]
