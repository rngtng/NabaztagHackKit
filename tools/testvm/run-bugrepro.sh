#!/bin/sh
# Build and run the firmware VM bug-reproduction harness (issue #66).
#
# Compiles the *real* firmware VM sources (../src/vm, ../src/net) together with
# bugrepro.c under AddressSanitizer, then runs each scenario. Paths are relative
# to this layer the same way the Makefile is: cwd is tools/testvm, and the
# firmware sits at ../ (mounted at /work in the Docker task).
#
# A scenario that still trips ASan means the corresponding bug is PRESENT; once
# the guard is fixed the same scenario runs clean. Exits nonzero if any bug
# still reproduces, so this doubles as a regression guard.
set -eu

cd "$(dirname "$0")"
CC=${CC:-gcc}
OUT=${OUT:-./bugrepro}

$CC -O0 -g -w -D_NAB_SIM -DDEBUG_VM -DDEBUG \
    -fsanitize=address -fno-omit-frame-pointer \
    -I../inc -I../sys/inc \
    bugrepro.c \
    ../src/vm/vmem.c ../src/vm/vinterp.c ../src/vm/vloader.c \
    ../src/vm/vlog.c ../src/vm/vnet.c ../src/vm/vaudio.c \
    ../src/net/aes128.c ../src/net/eapol.c ../src/net/hash.c \
    ../src/net/ieee80211.c ../src/net/rc4.c \
    ../src/utils/debug.c \
    stubs/hal/*.c stubs/usb/*.c stubs/utils/*.c \
    -lm -o "$OUT"

rc=0
for scen in syscmp store setstruct; do
    printf '\n==== scenario: %s ====\n' "$scen"
    if out="$("$OUT" "$scen" 2>&1)"; then
        printf '  PASS (clean): bug not reproduced -> looks fixed\n'
    else
        printf '  FAIL: bug reproduced (AddressSanitizer aborted)\n'
        echo "$out" | grep -E 'AddressSanitizer|SUMMARY|vinterp\.c|vlog\.c' | head -6 | sed 's/^/    /'
        rc=1
    fi
done

printf '\n'
if [ "$rc" -ne 0 ]; then
    echo "One or more firmware VM bugs still reproduce (see issue #66)."
else
    echo "All scenarios clean."
fi
exit "$rc"
