# The name of the target file
TARGET = Nab

MTL_BOOT=boot.0.0.0.13

# User options
OPTIONS =
#OPTIONS += -DDEBUG
#OPTIONS += -DDEBUG_WIFI
#OPTIONS += -DDEBUG_USB
OPTIONS += -DDEBUG_VM
#OPTIONS += -DDEBUG_VM_FULL
#OPTIONS += -DHIDE_VM_OUTPUT
OPTIONS += -DDEBUG_AUDIO
#OPTIONS += -DDEBUG_RFID
OPTIONS += -DDEBUG_MAIN

# C Files to compile (take all)
#C_FILES = $(wildcard src/*.c)
C_FILES  = src/bc.c src/main.c
C_FILES += $(wildcard src/**/*.c)
C_FILES += $(wildcard sys/src/*.c)
AS_FILES = $(wildcard sys/asm/*.s)

# Compiler options
CFLAGS =
CFLAGS += --specs=nosys.specs
CFLAGS += -Os   # size-optimised: the boot bytecode is embedded in the 124k flash image (#103)
# NB: -Os broke WPA join (config-AP/boot unaffected). NOT strict aliasing (disproven:
# -fno-strict-aliasing gave byte-identical codegen; the punned frame fields are all
# uint8_t = char type, which aliases legally anyway). A genuine missing-compiler-barrier
# bug in disable/enable_ohci_irq() was found and fixed (inc/usb/hcd.h +
# rt2501usb_buffer.c), but flashing that fix ALONE did not restore join - the root
# cause is still unpinned. Until it is, part of the wifi/USB stack builds at -O1 via
# O1_DIRS (below); keep that knob in sync with the hardware bisect state.
#CFLAGS += -g
CFLAGS += -mthumb -mthumb-interwork
CFLAGS += -Wall -Wextra -Wpedantic -Wpointer-arith
# -Wcast-align surfaces uint8_t*->uint32_t*/struct* casts (the frame type-punning
# that -Os miscompiled) at build time - the early signal for this bug class.
CFLAGS += -Wcast-align
# -Werror: the tree is warning-clean as of #150 (net/crypto casts fixed to be
# genuinely aligned, verified-aligned casts routed through (void *)), so any new
# warning must fail the build rather than scroll past. Add a scoped -Wno-error=<w>
# with a rationale if a future toolchain flags something benign.
CFLAGS += -Werror
CFLAGS += -fdata-sections -ffunction-sections
CFLAGS += -fno-exceptions -fno-delete-null-pointer-checks
CFLAGS += -mcpu=arm7tdmi -MMD
CFLAGS += $(OPTIONS)
CFLAGS += -Iinc/ -Isys/inc

LDSCRIPT = sys/ml67q4051.ld

# Linker options
LD_FLAGS =
LDFLAGS += --specs=nosys.specs
LDFLAGS += -Wl,--gc-sections    # drop unreferenced code/data (paired with -f*-sections); KEEP guards the vectors/startup/bytecode in ml67q4051.ld
#~ LDFLAGS += -Wl,-s
LDFLAGS += -Wl,-Map,obj/$(TARGET).map
LDFLAGS += -nostartfiles
LDFLAGS += -mcpu=arm7tdmi -T$(LDSCRIPT)
LDFLAGS += -Wa,-adhln=obj/$(TARGET).lst

# Additional libraries
LIBS = -lc_nano -lm -lnosys

# Compiler toolchain
CC = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE = arm-none-eabi-size
NM = arm-none-eabi-nm
DUMP = arm-none-eabi-objdump
PROGRAM = arm-none-eabi-gdb

OBJS = $(C_FILES:%.c=obj/%.o)
OBJS += $(AS_FILES:%.s=obj/%.o)

all: elf hex bin dis

elf: bin/$(TARGET).elf
	$(SIZE) $<

hex: bin/$(TARGET).elf
	$(OBJCOPY) -O ihex $< bin/$(TARGET).hex

bin: bin/$(TARGET).elf
	$(OBJCOPY) -O binary $< bin/$(TARGET).bin

dis: bin/$(TARGET).elf
	$(NM) -lS $< > obj/$(TARGET).sym
	$(DUMP) -d $< > obj/$(TARGET).dis

# -Os miscompiles the WPA-join path (join works at -O1, fails at -Os; cause not
# yet pinned). Bisection knobs, appended after the global -Os so -O1 wins for
# just the listed files: O1_DIRS = whole source dirs, O1_SRCS = single files.
# Both empty = all -Os. Hardware results so far:
#   round 1: O1_DIRS = src/usb src/net -> JOINS (culprit inside these two dirs)
#   round 2: O1_DIRS = src/usb         -> FAILS
#            O1_DIRS = src/net         -> JOINS  => culprit is in src/net
#   round 3: O1_SRCS = ieee80211.c     -> JOINS  => culprit is ieee80211.c
#            (rest of src/net was already cleared offline: EAPOL PTK/msg2 build
#            byte-identical -O1 vs -Os, crypto passes RFC 2202 KATs at -Os)
#   round 4: O1_FN on the RX/ISR dispatch path (ieee80211_input +
#            input_mgt/input_data/input_ctl) -> FAILS => culprit is in the
#            -Os remainder (TX/setup half), not the RX dispatch
#   round 5: O1_FN on the TX/setup path (rt2501_send, rt2501_auth,
#            rt2501_setmode, ieee80211_associate, ieee80211_send_auth)
#            -> JOINS => culprit is among those five
#   round 6: O1_FN on rt2501_send ONLY -> FAILS
#   round 7: O1_FN on ieee80211_associate ONLY -> JOINS (current state).
#            Join tracks associate's opt level across all 8 runs.
# BUT: instruction-level trace of associate -Os vs -O1 (task disasm:firmware,
# both variants) shows they are behaviorally EQUIVALENT - byte-identical assoc
# frame (incl. the -Os rodata WPA-IE blob, verified against the -O1 byte
# stores), identical rt2501_make_tx_descriptor/rt2501_tx args, identical
# state/timeout updates; the rest of the image differs only by a +0x6c layout
# shift in literal pools. Store widths differ (-Os merges to strh/str into
# COMRAM) but make_tx_descriptor word-stores the same COMRAM buffer in every
# working build, so width alone can't be it. => O1_FN is NOT fixing a
# miscompile; it perturbs timing/bus-access pattern inside the USB-ISR
# auth->assoc turnaround (or the join tests hid a confound, e.g. an RT2573
# wedge #125). Root cause still unpinned - next step is on-device
# instrumentation (readback of the assoc frame from COMRAM pre-tx + join-stage
# markers), not more opt-level shuffling.
O1_DIRS =
O1_SRCS =

obj/%.o : %.c
	@test -d $(@D) || mkdir -pm 775 $(@D)
	@test -d obj/lst/$(<D) || mkdir -pm 775 obj/lst/$(<D)
	$(CC) -c $(CFLAGS) $(if $(filter $(O1_DIRS),$(<D))$(filter $(O1_SRCS),$<),-O1) -Wa,-adhln=obj/lst/$<.lst $< -o $@  $(LIBS)

obj/%.o : %.s
	@test -d $(@D) || mkdir -pm 775 $(@D)
	@test -d obj/lst/$(<D) || mkdir -pm 775 obj/lst/$(<D)
	$(CC) -o $@ $(CFLAGS) -Wa,-adhln=obj/lst/$<.lst -x assembler-with-cpp -c $<

# compiler generated dependency info
-include $(OBJS:.o=.d)

%.elf : $(OBJS)
	@test -d $(@D) || mkdir -pm 775 $(@D)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

clean:
	@rm -Rf bin/
	@rm -Rf obj/

program: bin/$(TARGET).elf
	$(PROGRAM) $< < gdb_load

reset:
	$(PROGRAM) < gdb_reset

mtl_boot: mtl/boot/$(MTL_BOOT).mtl
	@cd mtl/mtl_linux; \
	make
	@test -d obj/ || mkdir -pm 775 obj/
	@test -d bin/ || mkdir -pm 775 bin/
	@cd obj/; \
	../mtl/mtl_linux/mtl_compiler ../mtl/boot/$(MTL_BOOT).mtl ../bin/$(MTL_BOOT).bin


.PHONY: clean program elf hex bin mtl_boot
