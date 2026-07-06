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

# Compiler options (ARM7TDMI, no FPU, Thumb/interwork - same set as src/firmware)
CFLAGS  =
CFLAGS += --specs=nosys.specs
CFLAGS += -O1
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
LDFLAGS += -mcpu=arm7tdmi -T$(LDSCRIPT)

# newlib-nano + math + nosys syscall stubs
LIBS = -lc_nano -lm -lnosys

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
