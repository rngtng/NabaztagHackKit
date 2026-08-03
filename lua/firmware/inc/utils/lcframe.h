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

/** @brief Outcome of parsing a `#LC:<len>:<sum>` header line. */
typedef enum {
  LCFRAME_OK = 0,
  LCFRAME_ERR_LEN,      /**< no decimal length after "#LC:" */
  LCFRAME_ERR_TOOLONG,  /**< the declared length is over the cap */
  LCFRAME_ERR_NOSUM,    /**< no ":<sum>" field - a sender predating #298 */
  LCFRAME_ERR_BADSUM    /**< the sum field is not 8 hex digits */
} lcframe_status;

/**
 * @brief Parse a `#LC:<len>:<sum>` header line (the leading "#LC:" included).
 *
 * @param line  the header line, NUL-terminated.
 * @param max   cap on the declared length (the caller's LC_MAX).
 * @param len   OUT: **the payload the sender has already put on the wire**, in
 *              bytes - i.e. 2*len hex chars still queued behind this header.
 *              Set on failure as well as success, and that is the point: a
 *              caller that returns without consuming them leaves the console
 *              reading a bytecode payload as REPL lines. 0 means "nothing to
 *              consume" - either there is no payload, or the header is too
 *              malformed to say how much (see below).
 * @param sum   OUT: the sender's Fletcher-32, valid only on LCFRAME_OK.
 *
 * `*len` is 0 for LCFRAME_ERR_LEN, because a header with no parseable length
 * did not come from one of our senders - it is something hand-typed at the
 * prompt, and there is no payload behind it. It is also 0 for
 * LCFRAME_ERR_TOOLONG: that length is over the cap by definition, and draining
 * it would mean reading megabytes off the console to stay in sync. Desync is
 * the lesser evil there, and a real sender never declares one.
 */
lcframe_status lcframe_parse_header(const char *line, long max,
                                    long *len, uint32_t *sum);

/** @brief A short human-readable message for a parse status (never NULL). */
const char *lcframe_strerror(lcframe_status st);

#endif /* _LCFRAME_H_ */
