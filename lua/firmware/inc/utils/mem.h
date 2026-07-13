/**
 * @file mem.h
 * @brief hcdmem allocator banks (M11a, #143).
 *
 * V2 shim for src/firmware's utils/mem.h (which also carried V1's flash
 * routines - not ported). The usb/ sources allocate from two banks:
 *   COMRAM - the ML60842's own 4 KB ARM/USB shared RAM (set up by hcd_init)
 *   EXTRAM - a dedicated 32 KB window at the top of external SRAM
 *            (sys/ml67q4051.ld __usbram_*), outside _sbrk's Lua-heap range
 */
#ifndef _MEM_H_
#define _MEM_H_

#include <stdint.h>
#include "usb/hcdmem.h"

#define COMRAM 0
#define EXTRAM 1

extern char __usbram_start__, __usbram_end__;

/* V1 main.c's setup_malloc(): register the EXTRAM bank; call once before
 * usbhost_init() (first EXTRAM allocation happens at device enumeration). */
static inline void setup_usb_malloc(void)
{
  hcd_malloc_init((int32_t)(intptr_t)&__usbram_start__,
                  (uint32_t)(&__usbram_end__ - &__usbram_start__),
                  16, EXTRAM);
}

#endif
