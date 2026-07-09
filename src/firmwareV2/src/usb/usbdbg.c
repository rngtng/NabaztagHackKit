/**
 * @file usbdbg.c
 * @brief Debug console for the vendored usb/ sources (M11a, #143).
 *
 * dbg_buffer is V1's shared sprintf target (referenced only from DBG_* paths;
 * --gc-sections drops it from non-debug images). dbg_puts routes to the JTAG
 * semihosting console - same SYS_WRITEC path as the probe apps (M3 #91).
 */
#include "utils/debug.h"

char dbg_buffer[DBG_BUFFER_LENGTH];

#if defined(DEBUG_USB) || defined(DEBUG_WIFI)

#define SYS_WRITEC 0x03

static inline int semihost(int op, void *arg)
{
  register int r0 asm("r0") = op;
  register void *r1 asm("r1") = arg;
  asm volatile("svc #0xAB" : "+r"(r0) : "r"(r1) : "memory");
  return r0;
}

void dbg_puts(const char *s)
{
  while (*s) {
    char c = *s++;
    semihost(SYS_WRITEC, &c);
  }
}

#endif
