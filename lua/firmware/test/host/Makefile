# Host-side unit tests for the firmware's board-independent C (#241).
#
# Built with the HOST gcc (not the ARM cross-compiler): these link real
# firmware sources against stubbed hardware and run natively under
# AddressSanitizer + UBSan, so a memory-safety regression aborts with a precise
# report. Same idea as mtl/tools/testvm's bugrepro, for the lua track.
#
# Only board-independent sources belong here. Anything touching ml674061.h
# register macros cannot be compiled for the host and stays sim/hardware-tested.
#
#   make && ./event_test          -> run every scenario
#   ./event_test rfid-drop        -> one scenario (an abort can't mask the rest)

FW := ../..

CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror -g -O1 -I$(FW)/inc -I$(FW)/lua
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS := -fsanitize=address,undefined

TESTS := event_test fmt_test rfid_test i2c_test adc_test ieee80211_test

all: $(TESTS)

event_test: event_test.c $(FW)/src/utils/event.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# fmt.c overrides snprintf/vsnprintf; -U_FORTIFY_SOURCE keeps glibc from
# redirecting them to __*_chk and colliding with our definitions.
# -Wno-format*: gcc checks the format strings against GLIBC's printf, but the
# calls here deliberately exercise our own - truncation, a NULL %s, an unknown
# conversion - which are the contracts under test, not mistakes.
fmt_test: fmt_test.c $(FW)/src/utils/fmt.c
	$(CC) $(CFLAGS) -U_FORTIFY_SOURCE -Wno-format -Wno-format-truncation \
	      -o $@ $^ $(LDFLAGS)

# rfid.c drives the coupler purely through write_i2c/read_i2c, so the whole
# scan runs host-native with that boundary stubbed. -std=gnu11: common.h's
# __no_operation() uses the `asm` keyword, which strict ISO C rejects.
# -D_NAB_SIM makes CLR_WDT a no-op (same switch mtl/tools/testvm uses).
rfid_test: rfid_test.c $(FW)/src/hal/rfid.c
	$(CC) $(CFLAGS) -std=gnu11 -D_NAB_SIM -I$(FW)/sys/inc -o $@ $^ $(LDFLAGS)

# i2c.c is register-only, so it needs stubs/ml674061.h FIRST on the include
# path to shadow the real absolute peripheral addresses (see that header).
i2c_test: i2c_test.c $(FW)/src/hal/i2c.c
	$(CC) $(CFLAGS) -std=gnu11 -D_NAB_SIM -Istubs -I$(FW)/sys/inc -o $@ $^ $(LDFLAGS)

# adc.c is register-only too, so the same shadowing stub serves it. It links
# no other firmware source: the wheel read is the whole driver.
adc_test: adc_test.c $(FW)/src/hal/adc.c
	$(CC) $(CFLAGS) -std=gnu11 -D_NAB_SIM -Istubs -I$(FW)/sys/inc -o $@ $^ $(LDFLAGS)

# ieee80211.c is VENDORED, so it is exempt from our -Werror set (see the
# firmware Makefile) - -w keeps the vendor's warnings from failing this build
# while the test file itself stays under the strict flags via CFLAGS. It also
# needs stubs/ml60842.h to shadow the absolute USB register window that
# hcd.h's disable_ohci_irq() dereferences.
ieee80211_test: ieee80211_test.c $(FW)/src/net/ieee80211.c
	$(CC) $(CFLAGS) -std=gnu11 -D_NAB_SIM -w -Istubs -I$(FW)/sys/inc \
	      -o $@ $^ $(LDFLAGS)

check: $(TESTS)
	@rc=0; for t in $(TESTS); do \
	  echo "== $$t =="; ./$$t || rc=1; \
	done; exit $$rc

clean:
	rm -f $(TESTS)

.PHONY: all check clean
