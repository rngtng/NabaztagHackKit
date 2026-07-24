/**
 * @file fmt.h
 * @brief Formatting helpers that are ours rather than libc's (see fmt.c).
 *
 * Only the non-standard ones are declared here. vsnprintf/snprintf are
 * declared by <stdio.h> (we merely override the definitions), and the luai_*
 * hooks are declared by luaconf.h at their use sites in the Lua core.
 */
#ifndef _FMT_H_
#define _FMT_H_

#include <stdint.h>

/* 8-byte RFID UID -> "a1b2c3d4e5f60708" (lowercase hex). Writes exactly 16
 * characters plus the NUL terminator, so out[] must hold 17 bytes. */
void fmt_hex8(char out[17], const uint8_t uid[8]);

#endif
