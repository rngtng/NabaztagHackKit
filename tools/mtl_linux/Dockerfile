# Self-contained MTL toolchain image.
#
# Built into the image (the tools are 32-bit / -m32, so the image is pinned to
# amd64 and runs emulated on Apple Silicon — fine for small one-shot tools):
#   /src/mtl_compiler, /src/mtl_simu   the binaries
#   /src/config.txt                    simulator defaults (SOURCE/MAC/BOOT/SERVERURL)
#   /src/nominal.mtl                   the bundled sample app
# So `mtl:simulate` (cwd = /src) runs out of the box with no host-side config.
# At runtime your working tree mounts at /work; pass app sources as /work/<file>.
#
# Only the build inputs and the two runtime files are COPYed in explicitly, so
# host artifacts (obj/, *.o, prebuilt binaries) never enter the image — no
# .dockerignore needed.
FROM --platform=linux/amd64 debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        g++-multilib \
        make \
    && rm -rf /var/lib/apt/lists/*

WORKDIR /src

# Build inputs (objects land in obj/, kept out of src/ and inc/).
COPY Makefile ./
COPY inc/ inc/
COPY src/ src/
RUN make comp simu

# Runtime defaults read from cwd = /src by the simulator.
COPY config.txt ./
