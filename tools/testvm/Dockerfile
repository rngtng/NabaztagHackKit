# Minimal native-gcc image for testvm.
# Sources are mounted at runtime (/work = src/firmware/, /work/testvm = tools/testvm/);
# only the toolchain is baked in.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends gcc libc6-dev make \
    && rm -rf /var/lib/apt/lists/*
