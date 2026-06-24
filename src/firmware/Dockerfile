# Self-contained ARM7TDMI firmware toolchain.
#
# No --platform pin: gcc-arm-none-eabi ships native for both arm64 and amd64 on
# bookworm, so this runs at full speed on Apple Silicon and Intel alike.
#
# Build-tools only — the firmware sources are bind-mounted at /work at runtime
# (see Taskfile compile), so artifacts land back on the host in bin/ and obj/.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        gcc-arm-none-eabi \
        libnewlib-arm-none-eabi \
        make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /work

CMD ["make"]
