# The name of the target file
TARGET = Nab

# User options
OPTIONS =
#OPTIONS += -DDEBUG
#OPTIONS += -DDEBUG_WIFI
#OPTIONS += -DDEBUG_USB

# C Files to compile (take all)
#C_FILES = $(wildcard src/*.c)
C_FILES  = src/bc.c src/main.c
C_FILES += $(wildcard src/**/*.c)
C_FILES += $(wildcard sys/src/*.c)
AS_FILES = $(wildcard sys/asm/*.s)

# Compiler options
CFLAGS =
CFLAGS += -O1
CFLAGS += -g
CFLAGS += -mthumb -mthumb-interwork
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wpointer-arith
CFLAGS += -fdata-sections -ffunction-sections
CFLAGS += -fno-exceptions -fno-delete-null-pointer-checks
CFLAGS += -mcpu=arm7tdmi -MMD
CFLAGS += $(OPTIONS)
CFLAGS += -Iinc/ -Isys/inc

LDSCRIPT = sys/ml67q4051.ld

# Linker options
LD_FLAGS =
#~ LDFLAGS +=  -Wl,--gc-sections
#~ LDFLAGS += -Wl,-s
LDFLAGS += -Wl,-Map,obj/$(TARGET).map
LDFLAGS += -nostartfiles
LDFLAGS += -mcpu=arm7tdmi -T$(LDSCRIPT)
LDFLAGS += -Wa,-adhln=obj/$(TARGET).lst

# Additional libraries
# -lnosys supplies bare-metal syscall stubs (_write, _exit, ...) newlib-nano needs at link time
LIBS = -lc_nano -lm -lnosys

# Compiler toolchain
CC = arm-none-eabi-gcc
OBJCOPY = arm-none-eabi-objcopy
SIZE = arm-none-eabi-size
NM = arm-none-eabi-nm
DUMP = arm-none-eabi-objdump

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

obj/%.o : %.c
	@test -d $(@D) || mkdir -pm 775 $(@D)
	@test -d obj/lst/$(<D) || mkdir -pm 775 obj/lst/$(<D)
	$(CC) -c $(CFLAGS) -Wa,-adhln=obj/lst/$<.lst $< -o $@  $(LIBS)

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

.PHONY: clean elf hex bin dis
