/**
 * @file debug.h
 * @author Violet - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief Debug stuff
 */
#ifndef _DEBUG_H
#define _DEBUG_H

#define DBG_BUFFER_LENGTH 256

#include "common.h"
#include "hal/uart.h"
#include <stdio.h>

extern char dbg_buffer[DBG_BUFFER_LENGTH];

#ifdef DEBUG
#define DBG(x) putst_uart((uint8_t*)x)
#else
#define DBG(x)
#endif

#ifdef DEBUG_USB
#define DBG_USB(x) putst_uart((uint8_t*)x)
#else
#define DBG_USB(x)
#endif

#ifdef DEBUG_WIFI
#define DBG_WIFI(x) putst_uart((uint8_t*)x)
#else
#define DBG_WIFI(x)
#endif

#ifdef DEBUG_VM
#define DBG_VM(x) putst_uart((uint8_t*)x)
#else
#define DBG_VM(x)
#endif

#ifdef DEBUG_AUDIO
#define DBG_AUD(x) putst_uart((uint8_t*)x)
#else
#define DBG_AUD(x)
#endif

#ifdef DEBUG_RFID
#define DBG_RFID(x) putst_uart((uint8_t*)x)
#else
#define DBG_RFID(x)
#endif

#ifdef DEBUG_MAIN
#define DBG_MAIN(x) putst_uart((uint8_t*)x)
#else
#define DBG_MAIN(x)
#endif


#endif /* _DEBUG_H */
