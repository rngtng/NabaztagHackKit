# Minimal native-gcc image for testvm.
# Sources are mounted at runtime (/work = src/firmware/); only the toolchain is baked in.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends gcc make \
    && rm -rf /var/lib/apt/lists/*
