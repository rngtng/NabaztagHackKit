/**
 * @file debug.h
 * @brief Debug-output shim for the vendored usb/ (later net/) sources (M11a, #143).
 *
 * V2 counterpart of src/firmware's utils/debug.h. V1 routed DBG_* to the
 * UART; this board has no UART pins, so debug builds route to the JTAG
 * semihosting console instead (usb/usbdbg.c). Build with DEBUG_USB=1
 * (see Makefile) to enable.
 *
 * The vendored sources call sprintf(dbg_buffer, ...) unconditionally before
 * each DBG_USB(dbg_buffer). In non-debug builds that would drag newlib's
 * sprintf (~7 KB) into every image, so sprintf is macro-erased here alongside
 * the DBG_* macros - the only sprintf callers in the vendored tree are these
 * debug lines.
 */
#ifndef _DEBUG_H
#define _DEBUG_H

#include <stdint.h>

#define DBG_BUFFER_LENGTH 256
extern char dbg_buffer[DBG_BUFFER_LENGTH];

/* V1's hal/uart.c hexdump (eapol.c calls it unconditionally on rx frames);
 * semihosting in DEBUG_* builds, empty stub otherwise (usb/usbdbg.c). */
void dump(uint8_t *src, int32_t len);

#if defined(DEBUG_USB) || defined(DEBUG_WIFI)
#include <stdio.h>
void dbg_puts(const char *s); /* semihosting, usb/usbdbg.c */
#else
#define sprintf(...) ((void)0)
#endif

#ifdef DEBUG_USB
#define DBG_USB(x) dbg_puts(x)
#else
#define DBG_USB(x)
#endif

#ifdef DEBUG_WIFI
#define DBG_WIFI(x) dbg_puts(x)
#else
#define DBG_WIFI(x)
#endif

#define DBG(x)

#endif
