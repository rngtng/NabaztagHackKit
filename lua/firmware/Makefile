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

# M11 (#119): debug builds (DEBUG_USB=1 / DEBUG_WIFI=1) get their own target
# name and object tree - .o files don't record CFLAGS, so sharing obj/ would
# silently mix debug and release objects across builds.
ifneq ($(DEBUG_USB)$(DEBUG_WIFI),)
  TARGET = $(APP)-dbg
  OBJDIR = obj/dbg
else
  TARGET = $(APP)
  OBJDIR = obj
endif

# Sources: the selected app + any shared C (src/*.c, src/hal/*.c) + startup.
# src/usb (M11a, #143) is compiled for every app; --gc-sections drops it from
# images that never call into it, so the APP=lua flash budget is untouched.
C_FILES  = src/app/$(APP).c
C_FILES += $(wildcard src/*.c)
C_FILES += $(wildcard src/hal/*.c)
C_FILES += $(wildcard src/usb/*.c)
C_FILES += $(wildcard src/net/*.c)
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
# Bytecode-only image (#128): the on-device parser/lexer/codegen are NOT linked.
# lcode+llex+lparser (~18.9 KB) exist only to compile source; the rabbit loads
# only luac bytecode (see src/app/lua.c + tools/luac). The two remaining refs
# into this trio - luaY_parser (ldo.c) and luaX_init (lstate.c) - are guarded
# out by -DLUA_NOPARSER (below). lundump/lvm (bytecode load + run) stay.
# ldump (#213) is the same story on the way out: the device only *loads*
# bytecode, never serialises it, so ldump.c and its refs (string.dump in
# lstrlib.c, lua_dump in lapi.c) are dropped behind the same define.
LUA_NOPARSE = lcode llex lparser ldump
ifeq ($(APP),lua)
  C_FILES += $(patsubst %,lua/%.c,$(filter-out $(LUA_NOPARSE),$(LUA_CORE)) $(LUA_LIB))
endif

# Compiler options (ARM7TDMI, no FPU, Thumb/interwork - same set as src/firmware)
CFLAGS  =
CFLAGS += --specs=nosys.specs
CFLAGS += -O1
# Global default is -O1; flash-tight files get a per-file -Os override (Lua and
# the vendored wifi net/usb dirs, below). The V1 -Os WPA-join break was NOT
# strict aliasing (disproven - byte-identical codegen) and the OHCI-ISR barrier
# fix did not cure it alone; the root cause is still unpinned. What is EMPIRICAL:
# V1 connects with global -Os + ieee80211_associate alone at -O1 (in-source
# O1_FN). V2 first tried net/usb at -O1 (making O1_FN a no-op) and died in
# 802.11 AUTH - so the fix is the opt-level DELTA on associate, not -O1 itself.
# Rule: build the vendored net/usb dirs at -Os (per-file block below) so O1_FN
# keeps its delta, and carry over the barrier'd disable/enable_ohci_irq() from
# V1's inc/usb/hcd.h regardless.
CFLAGS += -mthumb -mthumb-interwork
CFLAGS += -Wall -Wextra -Wpedantic -Wpointer-arith
CFLAGS += -Wcast-align   # early signal for the uint8_t*->wider-pointer casts above
# -Werror: our sources are warning-clean as of #150, so a new warning must fail
# the build. Vendored Lua opts out with -Wno-error in its per-file block below
# (it is not our code to fix); our own src/ files get no such exemption.
CFLAGS += -Werror
CFLAGS += -fdata-sections -ffunction-sections
CFLAGS += -fno-exceptions -fno-delete-null-pointer-checks
# -MP: emit a phony target for every prerequisite header so a header deleted
# after a prior build (obj/ is host-mounted and persists) does not wedge the
# next incremental build with "No rule to make target 'inc/net/rc4.h'" - the
# #124 WPA2-CCMP scavenge dropped rc4.h and stale .d files still listed it.
CFLAGS += -mcpu=arm7tdmi -MMD -MP
CFLAGS += -Iinc/ -Isys/inc

# M11a (#143): DEBUG_USB=1 / DEBUG_WIFI=1 route the vendored stack's DBG_*
# traces to the UART console (debug/probe builds only - pulls in newlib
# sprintf). Debug objects/artifacts live in obj/dbg + *-dbg.elf (above).
ifneq ($(DEBUG_USB),)
  CFLAGS += -DDEBUG_USB
endif
ifneq ($(DEBUG_WIFI),)
  CFLAGS += -DDEBUG_WIFI
endif

# M11 (#119): test-AP credentials for wifiprobe's association stage, taken
# from the environment (host: `source .env`, forwarded into Docker by the
# Taskfile). Never hardcode credentials in source or commit them - .env is
# gitignored; the defines only ever land in local build artifacts.
ifneq ($(SSID),)
  CFLAGS += -DWIFI_SSID='"$(SSID)"' -DWIFI_PSK='"$(PSK)"'
endif
# Optional: associate directly to a known BSSID/channel, bypassing the scan
# (a weak band-edge AP can be a coin-flip to catch - this tests the assoc path
# deterministically). BSSID as "aa:bb:..:ff", parsed at runtime.
ifneq ($(BSSID),)
  CFLAGS += -DWIFI_BSSID='"$(BSSID)"' -DWIFI_CHANNEL=$(CHANNEL)
endif

LDSCRIPT = sys/ml67q4051.ld

# Linker options
LDFLAGS  =
LDFLAGS += --specs=nosys.specs
LDFLAGS += -Wl,-Map,$(OBJDIR)/$(TARGET).map
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
  CFLAGS  += -DLUA_NOPARSER   # bytecode-only: no on-device source parsing (#128)
  CFLAGS  += -Igen            # gen/boot_lc.h: resident boot chunk (tools/luac/embed.py)
endif

# Toolchain
CC      = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE    = arm-none-eabi-size
NM      = arm-none-eabi-nm
DUMP    = arm-none-eabi-objdump

OBJS  = $(C_FILES:%.c=$(OBJDIR)/%.o)
OBJS += $(AS_FILES:%.s=$(OBJDIR)/%.o)

all: elf hex bin dis

elf: bin/$(TARGET).elf
	$(SIZE) $<

hex: bin/$(TARGET).elf
	$(OBJCOPY) -O ihex $< bin/$(TARGET).hex

bin: bin/$(TARGET).elf
	$(OBJCOPY) -O binary $< bin/$(TARGET).bin

dis: bin/$(TARGET).elf
	$(NM) -lS $< > $(OBJDIR)/$(TARGET).sym
	$(DUMP) -d $< > $(OBJDIR)/$(TARGET).dis

# Vendored Lua is compiled for size (-Os, to fit the 124 KB flash budget) and
# with relaxed warnings (it is not our code to fix); our own sources keep the
# strict -Wall -Wextra -Wpedantic and default -O1 set above.
# -Wno-cast-align: Lua's GCObject tagged-union casts (cast(T*, o) in lstate.h/
# lobject.h/llimits.h) trip -Wcast-align, but luaM_* allocates every object
# through lua_Alloc at L_Umaxalign, so the pointers are always suitably aligned.
# This is the vendored-Lua equivalent of the alignment audit in #150; our own
# src/ code stays -Wcast-align-clean.
$(OBJDIR)/lua/%.o : CFLAGS += -Os -Wno-pedantic -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function -Wno-cast-align -Wno-error

# Vendored V1 USB host stack + 802.11/WPA stack (M11a #143, M11b): built at
# -Os to MATCH V1's proven-connecting config exactly (V1 global is -Os with
# ieee80211_associate alone bumped to -O1 via the in-source O1_FN). The WPA
# join is NOT fixed by -O1 per se - it needs associate to differ from the -Os
# code around it; that opt-level DELTA is the (timing/layout) fix. Building
# these dirs at -O1 makes O1_FN a no-op, kills the delta, and reverts the
# failure (V2 died in 802.11 AUTH until this line went back to -Os). Also
# recovers the #128 flash budget. -Wno-error like vendored Lua: DEBUG builds
# un-erase the DBG sprintf calls and expose V1-era format-string warnings.
$(OBJDIR)/src/usb/%.o : CFLAGS += -Os -Wno-error -Wno-pedantic -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable
$(OBJDIR)/src/net/%.o : CFLAGS += -Os -Wno-error -Wno-pedantic -Wno-unused-parameter -Wno-sign-compare -Wno-unused-function -Wno-unused-variable -Wno-unused-but-set-variable

$(OBJDIR)/%.o : %.c
	@test -d $(@D) || mkdir -pm 775 $(@D)
	$(CC) -c $(CFLAGS) $< -o $@

$(OBJDIR)/%.o : %.s
	@test -d $(@D) || mkdir -pm 775 $(@D)
	$(CC) -o $@ $(CFLAGS) -x assembler-with-cpp -c $<

# compiler-generated dependency info
-include $(OBJS:.o=.d)

bin/%.elf : $(OBJS) $(LDSCRIPT)
	@test -d $(@D) || mkdir -pm 775 $(@D)
	$(CC) $(LDFLAGS) -o $@ $(OBJS) $(LIBS)

clean:
	@rm -rf bin/ obj/

.PHONY: all elf hex bin dis clean
