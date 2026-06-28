/**
 * @file debug.h
 * @author Violet - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief Debug stuff
 */
 #ifndef _DEBUG_H_
#define _DEBUG_H

#include "vm/vlog.h"

extern char dbg_buffer[];

#ifdef DEBUG
#define DBG(x) consolestr(x)
#else
#define DBG(x)
#endif

#ifdef DEBUG_USB
#define DBG_USB(x) consolestr(x)
#else
#define DBG_USB(x)
#endif

#ifdef DEBUG_WIFI
#define DBG_WIFI(x) consolestr(x)
#else
#define DBG_WIFI(x)
#endif

#endif /* _DEBUG_H */
