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

CFLAGS := -std=c11 -Wall -Wextra -Wpedantic -Werror -g -O1 -I$(FW)/inc
CFLAGS += -fsanitize=address,undefined -fno-omit-frame-pointer
LDFLAGS := -fsanitize=address,undefined

TESTS := event_test

all: $(TESTS)

event_test: event_test.c $(FW)/src/utils/event.c
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

check: $(TESTS)
	@rc=0; for t in $(TESTS); do \
	  echo "== $$t =="; ./$$t || rc=1; \
	done; exit $$rc

clean:
	rm -f $(TESTS)

.PHONY: all check clean
