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
# yet pinned). Bisection knob: source dirs listed here build at -O1 (appended
# after the global -Os, so -O1 wins for just those files). Empty = all -Os.
# Hardware results so far:
#   round 1: src/usb src/net -> JOINS (culprit is inside these two dirs)
#   round 2: src/usb alone   -> flash + join-test pending
#            JOINS -> culprit in src/usb: round 3 bisects its files
#            FAILS -> culprit in src/net: set O1_DIRS = src/net (expect JOINS),
#                     then round 3 bisects the net files
O1_DIRS = src/usb

obj/%.o : %.c
	@test -d $(@D) || mkdir -pm 775 $(@D)
	@test -d obj/lst/$(<D) || mkdir -pm 775 obj/lst/$(<D)
	$(CC) -c $(CFLAGS) $(if $(filter $(O1_DIRS),$(<D)),-O1) -Wa,-adhln=obj/lst/$<.lst $< -o $@  $(LIBS)

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
