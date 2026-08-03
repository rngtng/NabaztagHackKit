/**
 * @file utils/lcframe.h
 * @brief Integrity check for the `#LC` bytecode frame (#298).
 *
 * The image has no parser, so `luaU_undump` is its entire input surface - every
 * REPL line, every tools/luac payload, and (once #183's principle 4 lands) every
 * remotely loaded script slot. PUC-Rio does not claim that loader is safe on
 * malformed input: "Lua does not check the consistency of binary chunks", and
 * `task lua:firmware:test:bytecode` measures what that costs - 13% of
 * single-byte corruptions kill the runtime, and on this part there is no MMU, so
 * they corrupt the ExtRAM heap rather than stopping.
 *
 * The frame carried nothing to detect that with. Its wire is UART0 at 115200
 * 8N1 with no hardware flow control and a 16-byte RX FIFO - which is why the
 * sender already has to pace bytes (tools/openocd/README.md) - so a flipped
 * nibble was simply handed to the loader.
 *
 * Fletcher-32 over the decoded chunk bytes: ~40 bytes of flash, no table, and it
 * catches every single-bit and single-byte error and every burst under 32 bits.
 * That is the transport half of the problem. It is NOT a signature and does not
 * make the loader safe against a chosen payload - see #298 for the two decisions
 * still open (hardening lundump, authenticating remote slots).
 *
 * Senders: tools/luac/replpipe.py and tools/luac/luash.py.
 */
#ifndef _LCFRAME_H_
#define _LCFRAME_H_

#include <stddef.h>
#include <stdint.h>

/** @brief Fletcher-32 over n bytes. Must match the senders' implementation. */
uint32_t lcframe_checksum(const uint8_t *data, size_t n);

#endif /* _LCFRAME_H_ */
