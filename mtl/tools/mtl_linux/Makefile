# The name of the target file
SIMU_TARGET = mtl_simu
COMP_TARGET = mtl_compiler
VCOMP_TARGET = libvcomp.a

# OS
OS = linux

# User options
OPTIONS = -DVSIMU
# C Files to compile (take all)


SIMU_C_FILES  = $(wildcard src/vm/*.c)
SIMU_C_FILES += $(wildcard src/simu/*.c)
SIMU_C_FILES += $(wildcard src/simu/$(OS)/*.c)

SIMU_CPP_FILES  = $(wildcard src/vm/*.cpp)
SIMU_CPP_FILES += $(wildcard src/simu/*.cpp)
SIMU_CPP_FILES += $(wildcard src/simu/$(OS)/*.cpp)

COMP_C_FILES  = $(wildcard src/comp/*.c)
COMP_CPP_FILES  = $(wildcard src/comp/*.cpp)

VCOMP_C_FILES  = $(wildcard src/vcomp/*.c)
VCOMP_CPP_FILES  = $(wildcard src/vcomp/*.cpp)

# Compiler options
CFLAGS += -O0 -m32
CFLAGS += -g
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wpointer-arith
#CFLAGS += -fdata-sections -ffunction-sections
CFLAGS += -fexceptions -fno-delete-null-pointer-checks
#CFLAGS +=  -MMD
CFLAGS += $(OPTIONS)
CFLAGS += -I./inc/ -I./inc/vm
CFLAGS += -I./inc/vcomp
CFLAGS += -I./inc/comp
CFLAGS += -I./inc/simu/$(OS)

CPP_FLAGS = $(CFLAGS)

# Linker options
LDFLAGS = -m32
# LDFLAGS +=  -Wl,--gc-sections
# LDFLAGS += -Wl,-s

SIMU_LDFLAGS = $(LDFLAGS)
COMP_LDFLAGS = $(LDFLAGS)
VCOMP_LDFLAGS = $(LDFLAGS)

# Additional libraries
LIBS = $(VCOMP_TARGET) -lm

# Compiler toolchain
CC = gcc
CPP = g++
FLEX = flex
OBJCOPY = objcopy
SIZE = size
NM = nm
DUMP = objdump
DEBUG = gdb


SIMU_OBJS =  $(SIMU_CPP_FILES:%.cpp=obj/%.o)
SIMU_OBJS += $(SIMU_C_FILES:%.c=obj/%.o)

COMP_OBJS =  $(COMP_CPP_FILES:%.cpp=obj/%.o)
COMP_OBJS += $(COMP_C_FILES:%.c=obj/%.o)

VCOMP_OBJS =  $(VCOMP_CPP_FILES:%.cpp=obj/%.o)
VCOMP_OBJS += $(VCOMP_C_FILES:%.c=obj/%.o)




all: vcomp comp simu

vcomp: $(VCOMP_TARGET)

comp: vcomp $(COMP_TARGET)

simu: vcomp $(SIMU_TARGET)


obj/%.o : %.cpp
	@test -d $(@D) || mkdir -pm 775 $(@D)
	$(CPP) -c $(CPP_FLAGS) $< -o $@

obj/%.o : %.c
	@test -d $(@D) || mkdir -pm 775 $(@D)
	$(CC) -c $(CFLAGS) $< -o $@

$(SIMU_TARGET): $(SIMU_OBJS)
	$(CPP) $(LDFLAGS) -o $@ $^ $(LIBS)

$(COMP_TARGET): $(COMP_OBJS)
	$(CPP) $(LDFLAGS) -o $@ $^ $(LIBS)

$(VCOMP_TARGET): $(VCOMP_OBJS)
	$(AR) rcs $@ $^
#	ranlib $@


clean:
	@rm -f $(VCOMP_TARGET) $(COMP_TARGET) $(SIMU_TARGET)
	@rm -Rf obj/

debug: $(TARGET)
	$(DEBUG) $< < gdb_load

.PHONY: clean debug elf
