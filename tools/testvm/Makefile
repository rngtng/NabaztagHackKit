TARGET  = testvm
OBJ_DIR = obj

OPTIONS  = -DDEBUG -D_NAB_SIM -DDEBUG_VM

# bc.c is injected at task time; everything else lives relative to cwd = /work/testvm/
C_FILES  = bc.c testvm.c
C_FILES += $(wildcard ../src/vm/*.c)
C_FILES += $(wildcard ../src/net/*.c)
C_FILES += $(wildcard stubs/hal/*.c)
C_FILES += $(wildcard stubs/usb/*.c)
C_FILES += $(wildcard stubs/utils/*.c)

# let make find sources by basename across all source dirs
VPATH = . ../src/vm ../src/net stubs/hal stubs/usb stubs/utils

CFLAGS  = -O0 -g
CFLAGS += -Wall -Wextra -Wno-unused-parameter -Wpointer-arith
CFLAGS += -fno-exceptions -fno-delete-null-pointer-checks -MMD
CFLAGS += $(OPTIONS)
CFLAGS += -I../inc/

CC   = gcc
LIBS = -lm

OBJS = $(patsubst %.c,$(OBJ_DIR)/%.o,$(notdir $(C_FILES)))

all: $(TARGET)

$(OBJ_DIR)/%.o : %.c
	@mkdir -p $(OBJ_DIR)
	$(CC) -c $(CFLAGS) $< -o $@

$(TARGET): $(OBJS)
	$(CC) -o $@ $(OBJS) $(LIBS)

clean:
	@rm -f $(TARGET)
	@rm -Rf $(OBJ_DIR)/

-include $(wildcard $(OBJ_DIR)/*.d)

.PHONY: all clean
