# Host-side unit tests for the mtl firmware's vendored 802.11 stack (#307).
#
# Built with the HOST gcc (not the ARM cross-compiler): these link the REAL
# firmware sources against a stubbed board layer and run natively under
# AddressSanitizer + UBSan, so a memory-safety regression aborts with a precise
# report. Same idea as tools/testvm's bugrepro, and the direct counterpart of
# lua/firmware/test/host - see that directory's README for the shared rationale.
#
# This is the mtl track's first automated coverage of the WPA join path;
# `mtl:verify` was builds + crypto KATs only (#230).
#
#   make && ./eapol_test          -> run every scenario
#   ./eapol_test msg3-mic         -> one scenario (an abort can't mask the rest)

FW := ../..

CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror -g -O1 -I$(FW)/inc
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS := -fsanitize=address,undefined

TESTS := ieee80211_test eapol_test

all: $(TESTS)

# Shared by the test files and the sources under test:
# -std=gnu11: common.h's __no_operation() uses the `asm` keyword, which strict
# ISO C rejects. -D_NAB_SIM makes CLR_WDT a no-op (same switch testvm uses).
# stubs/ goes FIRST on the include path to shadow the absolute USB register
# window that hcd.h's disable_ohci_irq() dereferences; sys/inc/ml674061.h needs
# no stub here, since neither net/ source touches an SoC register.
NETFLAGS := -std=gnu11 -D_NAB_SIM -Istubs -I$(FW)/sys/inc

# The sources under test are VENDORED, so they are exempt from our -Werror set
# (see the firmware Makefile): -w keeps the vendor's warnings from failing this
# build. They are compiled SEPARATELY from the test files for that reason - a
# single $(CC) invocation would apply -w to the test file too, and the test
# files are ours and stay under the strict flags.
%.vendored.o: $(FW)/src/net/%.c
	$(CC) $(CFLAGS) $(NETFLAGS) -w -c -o $@ $<

%_test.o: %_test.c
	$(CC) $(CFLAGS) $(NETFLAGS) -c -o $@ $<

ieee80211_test: ieee80211_test.o ieee80211.vendored.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

eapol_test: eapol_test.o eapol.vendored.o
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

check: $(TESTS)
	@rc=0; for t in $(TESTS); do \
	  echo "== $$t =="; ./$$t || rc=1; \
	done; exit $$rc

clean:
	rm -f $(TESTS) *.o

.PHONY: all check clean
