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

#if !defined(DEBUG_USB) && !defined(DEBUG_WIFI)

void dump(uint8_t *src, int32_t len)
{
  (void)src;
  (void)len;
}

#else

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

/* V1 hal/uart.c's dump(), semihosting output: 16 hex bytes per line */
void dump(uint8_t *src, int32_t len)
{
  static const char hex[] = "0123456789abcdef";
  int32_t i;
  for (i = 0; i < len; i++) {
    char b[4] = {hex[(src[i] >> 4) & 0xF], hex[src[i] & 0xF],
                 (i % 16 == 15) ? '\n' : ' ', '\0'};
    dbg_puts(b);
  }
  if (len % 16)
    dbg_puts("\n");
}

#endif
