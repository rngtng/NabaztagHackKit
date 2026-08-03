# Browser UI for the lua-firmware simulator (#43): NiceGUI + the embedded
# Unicorn sim in one image. Built with the lua layer root as the Docker context
# (see Taskfile.yaml), like tools/luac / tools/simulator, so it can COPY the
# sibling tools/simulator/simulate.py it runs in-process.
#
# The in-browser REPL compiles each typed line to #LC bytecode in-container
# (parser-less firmware, #128), so we need the host-native luac. Rather than
# rebuild it here, pull the already-built binary from the luac image (a
# build-time dependency the serve task builds first). Both images are bookworm-
# based, so the glibc/libm the luac binary links resolve unchanged.
ARG LUAC_IMAGE=nabaztag-sdk-luac
FROM ${LUAC_IMAGE} AS luac

FROM python:3.12-slim

RUN pip install --no-cache-dir unicorn==2.1.1 pyelftools==0.31 nicegui==2.11.1
COPY --from=luac /usr/local/bin/luac /usr/local/bin/luac

WORKDIR /sim
COPY tools/simulator/simulate.py /sim/simulate.py
# The #LC framing (header + Fletcher-32, #298) comes from replpipe.py rather than
# being reimplemented here: this file already drifted once, emitting pre-#298
# checksum-less frames that the firmware then refused. One sender, one format.
COPY tools/luac/replpipe.py /sim/replpipe.py
COPY tools/simui/app.py /sim/app.py

EXPOSE 8080
ENTRYPOINT ["python", "/sim/app.py"]
