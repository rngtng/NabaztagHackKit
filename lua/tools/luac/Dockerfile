# Host-native luac for the firmwareV2 parser-less end-game (#133).
#
# The final wifi image only accepts luac bytecode (lundump resident;
# lparser/llex/lcode dropped, -19 KB), so every path that feeds Lua *source* to
# the rabbit needs a host-side compile step. lundump.c rejects chunks whose
# header sizes don't match the target, and our luaconf.h is LUA_32BITS
# (32-bit lua_Integer, 32-bit float lua_Number). A stock distro luac (64-bit
# int / double) is useless - so we build luac from the *vendored* lua/ tree with
# the *same* luaconf.h, guaranteeing the dump format matches and picking up any
# future local Lua patch automatically. Container arch is irrelevant to the
# output (dump sizes are varint-encoded; host + ARM7 are both little-endian).
#
# -DLUA_HOST_LUAC drops the device-only Local-config block in luaconf.h (the
# semihosting console + compact number helpers that live in the ARM firmware),
# so host luac falls back to the stock C library / libm - no ARM toolchain here.
#
# NOTE: this image is built with the *lua layer root* as its context (see
# Taskfile.yaml) so the COPY below can reach the firmware/lua/ tree that lives
# in the sibling firmware folder - mirroring tools/testvm, which likewise
# consumes a sibling folder's C. The COPY layer cache-busts whenever those
# sources change, so `luac` is always rebuilt in step with the vendored Lua.
FROM debian:bookworm-slim

RUN apt-get update \
    && apt-get install -y --no-install-recommends \
        gcc \
        libc6-dev \
    && rm -rf /var/lib/apt/lists/*

# The vendored Lua core+libs, incl. our LUA_32BITS luaconf.h and luac.c (which
# is vendored but never compiled into the firmware - it has its own main()).
COPY firmware/lua/ /opt/lua/

# Build luac: every .c except lua.c (the interpreter's main() would clash with
# luac.c's). -DLUA_HOST_LUAC selects the stock-libc fallbacks in luaconf.h.
RUN cd /opt/lua \
    && gcc -O2 -DLUA_HOST_LUAC -I. -o /usr/local/bin/luac \
        $(ls *.c | grep -vx 'lua.c') -lm \
    && luac -v

WORKDIR /work
ENTRYPOINT ["luac"]
