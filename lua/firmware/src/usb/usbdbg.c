/**
 * @file usbdbg.c
 * @brief Debug console for the vendored usb/ sources.
 *
 * dbg_buffer is V1's shared sprintf target (referenced only from DBG_* paths;
 * --gc-sections drops it from non-debug images). dbg_puts routes to UART0
 * (hal/uart.c); the host reads the traces on the Pi's /dev/serial0. The app
 * must have called init_uart() at boot (all apps do).
 */
#include "utils/debug.h"

#if defined(DEBUG_USB) || defined(DEBUG_WIFI)
#include <stdint.h>
#include "hal/uart.h"
#endif

char dbg_buffer[DBG_BUFFER_LENGTH];

#if !defined(DEBUG_USB) && !defined(DEBUG_WIFI)

void dump(uint8_t *src, int32_t len)
{
  (void)src;
  (void)len;
}

#else

void dbg_puts(const char *s)
{
  putst_uart((uint8_t *)s);
}

/* V1 hal/uart.c's dump(), UART output: 16 hex bytes per line */
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
