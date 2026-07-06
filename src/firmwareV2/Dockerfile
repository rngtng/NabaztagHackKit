# ARM7TDMI cross toolchain for the firmwareV2 (eLua-successor) bring-up.
# Multi-arch base (Intel + Apple Silicon), same toolchain proven by src/firmware.
#
#   gcc-arm-none-eabi + newlib-nano  -> the ML67Q4051 firmware (make APP=...)
#
# Host needs only Docker + Task; everything builds in here.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        gcc-arm-none-eabi \
        libnewlib-arm-none-eabi \
        make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work

CMD ["make"]
