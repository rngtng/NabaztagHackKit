/**
 * @file utils/lcread.h
 * @brief Reading an `#LC` bytecode frame off the console (#328).
 *
 * `utils/lcframe.h` parses the *header* and checks the payload's integrity.
 * This is the other half: the part that pulls the hex payload off the console
 * and decides what to do with a frame it refuses. It lived in `main.c`, which
 * carries `main()` - so nothing could link it and nothing in it could be
 * tested, and the one corner that needed a test worst (below) stayed open for
 * exactly that reason.
 *
 * The seam is deliberately Lua-free: this returns a decoded buffer, and the
 * caller does the `luaL_loadbuffer`. No test under `test/host/` links the Lua
 * core, and this file is the reason none has to.
 *
 * ## The rule this file exists to keep
 *
 * **Refusing a frame is only half of refusing it.** The payload is already on
 * the wire behind the header, so a path that returns without consuming
 * `2*len` hex chars leaves the REPL's line reader taking bytecode as REPL
 * input - one spurious error per wrapped line, for every frame a sender gets
 * wrong. That is what made a single checksum-less frame from `tools/simui`
 * look like ten failures (#308). Every status below therefore says what it
 * leaves on the console, and `test/host/lcread_test.c` asserts it.
 *
 * ## Where the console is left, per status
 *
 * | status | payload consumed | console left at |
 * |---|---|---|
 * | `LCREAD_OK`            | yes, exactly `2*len` + the trailing newline | the next line |
 * | `LCREAD_ERR_CHECKSUM`  | yes, same as OK - it is read before it is checked | the next line |
 * | `LCREAD_ERR_HEADER`    | yes, when the header names a length (see below) | the next line, less one byte in the `TOOLONG` case |
 * | `LCREAD_ERR_MEM`       | yes, drained and discarded | the next line |
 * | `LCREAD_ERR_PAYLOAD`   | as much as there was | one byte into the next line |
 *
 * Consuming *nothing* is as much a part of that contract as consuming the
 * payload: a sender emits `ceil(2*len/64)` payload lines, so a header with no
 * credible length (`LCFRAME_ERR_LEN` - something hand-typed) and a frame of 0
 * bytes both have no payload line at all, and reaching for a trailing newline
 * that was never sent eats the user's next line instead.
 *
 * The two `LCREAD_ERR_PAYLOAD` / `TOOLONG` costs are the same single cost: the
 * console has no pushback, so the reader can only discover it has run past the
 * payload by consuming the byte that proves it. Draining stops on the first
 * non-hex byte **having eaten it** - for a truncated payload that is the first
 * byte of the following line. Giving `_read` one byte of lookahead would close
 * it; that is a change to the console itself, not to this reader.
 *
 * ## `LCREAD_ERR_TOOLONG`, the corner that used to desync
 *
 * `lcframe_parse_header` reports `*len == 0` for `LCFRAME_ERR_TOOLONG` on
 * purpose: a header claiming 4 GB must not make the device read 8 GB of hex.
 * The reader used to take that literally and drop a single line, so the rest of
 * that frame's hex *was* read as REPL input. It now drains by content instead -
 * hex chars and the framing whitespace, up to the first byte that is neither -
 * which never trusts the declared length and reads only what the sender
 * actually sent. Those bytes arrive whether they are drained or not; the choice
 * is only between discarding them and echoing an error per wrapped line.
 */
#ifndef _LCREAD_H_
#define _LCREAD_H_

#include <stddef.h>
#include <stdint.h>

/** @brief Sanity cap on a single bytecode chunk, in decoded bytes. */
#define LCREAD_MAX 65536

/** @brief Outcome of reading one `#LC` frame off the console. */
typedef enum {
  LCREAD_OK = 0,
  LCREAD_ERR_HEADER,    /**< the header was refused (see lcframe_status) */
  LCREAD_ERR_PAYLOAD,   /**< the payload was truncated or not hex */
  LCREAD_ERR_CHECKSUM,  /**< the frame arrived damaged */
  LCREAD_ERR_MEM        /**< no room on the ExtRAM heap for the chunk */
} lcread_status;

/**
 * @brief Read one `#LC:<len>:<sum>` frame: header, hex payload, checksum.
 *
 * @param line  the header line as the console read it, NUL-terminated,
 *              leading `"#LC:"` included.
 * @param buf   OUT: on `LCREAD_OK`, a `malloc`ed buffer of `*len` decoded
 *              bytes that **the caller frees**. NULL on every other status.
 *              NULL with `*len == 0` on success too, for an empty frame.
 * @param len   OUT: decoded payload size in bytes; 0 unless `LCREAD_OK`.
 * @param msg   OUT: a short human-readable reason, never NULL, static
 *              storage. `"ok"` on success.
 *
 * Consumes the payload on every path - that is the contract, see the table in
 * this file's header comment. Blocks on the console for as long as the sender
 * takes; there is no timeout, on this or any other console read.
 */
lcread_status lcread_frame(const char *line, char **buf, long *len,
                           const char **msg);

#endif /* _LCREAD_H_ */
