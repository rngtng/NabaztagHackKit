# firmwareV2 - bring-up build for the eLua-successor (Lua 5.4) port to the
# Nabaztag:tag V2 (OKI ML67Q4051, ARM7TDMI). See README.md and issue #87.
#
# Builds ONE selectable bring-up app from src/app/ against the shared ARM7TDMI
# startup in sys/. Toolchain flags mirror src/firmware (proven on this chip).
#
#   make APP=hello     -> bin/hello.{elf,hex,bin}   (M0, #88 - toolchain check)
#   make APP=blink     -> bin/blink.{elf,hex,bin}   (M1, #89 - LED blink)
#
# All targets run inside the Docker toolchain image (see Dockerfile); the only
# host requirement is Docker + Task. Artifacts land in bin/ (and obj/).

# Which app in src/app/ to build. Every app provides its own main().
APP ?= hello
TARGET = $(APP)

# Sources: the selected app + any shared C (src/*.c, src/hal/*.c) + startup.
C_FILES  = src/app/$(APP).c
C_FILES += $(wildcard src/*.c)
C_FILES += $(wildcard src/hal/*.c)
C_FILES += $(wildcard sys/src/*.c)
AS_FILES = $(wildcard sys/asm/*.s)

# M4 (#92): the Lua app additionally links the vendored Lua 5.4 core (lua/).
# We compile the core + a trimmed set of stdlibs (base/string/table) to fit the
# 124 KB flash budget: no math (pulls ~16 KB of libm trig), no io/os/package/
# debug/loadlib/coroutine/utf8 (no filesystem, OS, or dynamic loading on this
# target; the rest do not earn their flash here). The standalone lua.c/luac.c and
# linit.c are replaced by our own REPL + openlibs in src/app/lua.c. base's
# dofile/loadfile are dropped in lua/lbaselib.c. See the src/app/lua.c header.
LUA_CORE = lapi lcode lctype ldebug ldo ldump lfunc lgc llex lmem lobject \
           lopcodes lparser lstate lstring ltable ltm lundump lvm lzio
LUA_LIB  = lauxlib lbaselib lstrlib ltablib
ifeq ($(APP),lua)
  C_FILES += $(patsubst %,lua/%.c,$(LUA_CORE) $(LUA_LIB))
endif

# Compiler options (ARM7TDMI, no FPU, Thumb/interwork - same set as src/firmware)
CFLAGS  =
CFLAGS += --specs=nosys.specs
CFLAGS += -O1
# Global default is -O1; flash-tight files get a per-file -Os override (Lua below;
# the wifi/net port will likely need it too). The -Os WPA-join break in V1 firmware
# was NOT strict aliasing (disproven - byte-identical codegen), and the missing
# compiler barrier found in the OHCI-ISR critical-section guards did not cure it
# alone either - the root cause is still unpinned (V1 pins src/usb+src/net at -O1,
# bisect in progress). Rule until it is pinned: NEVER put the vendored net/usb
# sources in the per-file -Os list, and carry over the barrier'd
# disable/enable_ohci_irq() from V1's inc/usb/hcd.h regardless.
CFLAGS += -mthumb -mthumb-interwork
CFLAGS += -Wall -Wextra -Wpedantic -Wpointer-arith
CFLAGS += -fdata-sections -ffunction-sections
CFLAGS += -fno-exceptions -fno-delete-null-pointer-checks
CFLAGS += -mcpu=arm7tdmi -MMD
CFLAGS += -Iinc/ -Isys/inc

LDSCRIPT = sys/ml67q4051.ld

# Linker options
LDFLAGS  =
LDFLAGS += --specs=nosys.specs
LDFLAGS += -Wl,-Map,obj/$(TARGET).map
LDFLAGS += -nostartfiles
LDFLAGS += -Wl,--gc-sections           # drop unreferenced code/data (paired with -f*-sections)
LDFLAGS += -mcpu=arm7tdmi -T$(LDSCRIPT)

# newlib-nano + math + nosys syscall stubs
LIBS = -lc_nano -lm -lnosys

# M4 (#92): the Lua app needs the vendored headers on the include path.
# NB: newlib-nano's float-capable printf (-u _printf_float) is deliberately NOT
# forced - it costs ~7.6 KB and does not fit in 124 KB (even base+string+float
# overflows). So float *printing* is stubbed (a float like 1/2 prints as ".0");
# integer results (the #92 DoD, `print(1+1)`) are exact and float arithmetic is
# still correct internally. Re-add `LDFLAGS += -Wl,-u,_printf_float` once code
# moves off internal flash (M6) or a lib is dropped to make room.
ifeq ($(APP),lua)
  CFLAGS  += -Ilua
endif

# Toolchain
CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size
NM      = arm-none-eabi-nm
DUMP    = arm-none-eabi-objdump

OBJS  = $(C_FILES:%.c=obj/%.o)
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

# Vendored Lua is compiled for size (-Os, to fit the 124 KB flash budget) and
# with relaxed warnings (it is not our code to fix); our own sources keep the
# strict -Wall -Wextra -Wpedantic and default -O1 set above.
obj/lua/%.o : CFLAGS += -Os -Wno-pedantic -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function

obj/%.o : %.c
	@test -d $(@D) || mkdir -pm 775 $(@D)
	$(CC) -c $(CFLAGS) $< -o $@

obj/%.o : %.s
	@test -d $(@D) || mkdir -pm 775 $(@D)
	$(CC) -o $@ $(CFLAGS) -x assembler-with-cpp -c $<

# compiler-generated dependency info
-include $(OBJS:.o=.d)

bin/%.elf : $(OBJS)
	@test -d $(@D) || mkdir -pm 775 $(@D)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

clean:
	@rm -rf bin/ obj/

.PHONY: all elf hex bin dis clean
