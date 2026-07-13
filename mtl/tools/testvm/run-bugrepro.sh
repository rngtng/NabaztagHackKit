#!/bin/sh
# Regression guard for the firmware VM memory-safety bugs found in issue #66
# (#69 OOB heap write, #70 sysCmp OOB read) -- all now fixed.
#
# Compiles the *real* firmware VM sources (../src/vm, ../src/net) together with
# bugrepro.c under AddressSanitizer, then runs each scenario. Paths are relative
# to this layer the same way the Makefile is: cwd is tools/testvm, and the
# firmware sits at ../ (mounted at /work in the Docker task).
#
# Every scenario must run CLEAN. A scenario that trips ASan means one of the
# fixed OOB bugs has been reintroduced -- the run fails so it can't land.
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
        printf '  PASS: clean (no out-of-bounds access)\n'
    else
        printf '  FAIL: REGRESSION -- AddressSanitizer aborted (a fixed OOB bug is back)\n'
        echo "$out" | grep -E 'AddressSanitizer|SUMMARY|vinterp\.c|vlog\.c' | head -6 | sed 's/^/    /'
        rc=1
    fi
done

printf '\n'
if [ "$rc" -ne 0 ]; then
    echo "REGRESSION: a previously-fixed firmware VM memory-safety bug (#69/#70) is back."
else
    echo "All scenarios clean -- fixed VM bugs stay fixed."
fi
exit "$rc"
