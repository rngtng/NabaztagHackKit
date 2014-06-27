# Makefile pour le compilateur de mtl.
COMMON_OBJS=properties.o dumpbc.o log.o

COMP_OBJS=main_compiler.o

SIMU_SRC=main_simu.c linux_simu.c linux_simuaudio.c linux_simunet.c vmem.c vloader.c vlog.c vinterp.c vaudio.c vnet.c
SIMU_OBJS=$(SIMU_SRC:.c=.o)

CXXFLAGS=-DVSIMU -g3
CFLAGS=-DVSIMU -g3  # -std=c99

CC=gcc
CXX=g++
OS=$(shell uname)
TARGET_OS=Linux

all : mtl_simu mtl_comp

.PHONY : vcomp/libvcomp.a

vcomp/libvcomp.a :
ifeq ($(OS),$(TARGET_OS))
	$(MAKE) -C vcomp libvcomp.a
endif

mtl_comp : vcomp/libvcomp.a $(COMMON_OBJS) $(COMP_OBJS)
ifeq ($(OS),$(TARGET_OS))
	g++  $^ -o $@ vcomp/libvcomp.a
endif

mtl_simu : vcomp/libvcomp.a $(COMMON_OBJS) $(SIMU_OBJS)
ifeq ($(OS),$(TARGET_OS))
	g++  $^ -o $@ vcomp/libvcomp.a
endif

clean :
	$(MAKE) -C vcomp clean
	rm -f $(COMMON_OBJS) $(COMP_OBJS) $(SIMU_OBJS)

install :
	#nothing to do

