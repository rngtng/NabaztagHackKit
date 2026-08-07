/**
 * @file syscalls.h
 * @brief The newlib syscalls this firmware supplies itself, and the console
 *        timestamp one of them keeps.
 *
 * Bare metal has no OS behind newlib, so these four are ours: stdout/stdin over
 * the polled UART0 console, the heap in ExtRAM, and a halt-in-place abort().
 * Our definitions win over libnosys' stubs because an object file beats an
 * archive member - a property of being IN the link, not of living in any
 * particular file, so it survived the move out of main.c (#324). Confirm it in
 * obj/firmware.map, not by assumption.
 *
 * _write is also a LINK-TIME SEAM, which is the reason this header exists:
 * utils/fmt.c routes every Lua console write through it and used to
 * forward-declare it inline, and test/host/fmt_test.c substitutes its own
 * capturing definition. Naming the seam in a header is what makes fmt.c's
 * dependency an ordinary downward edge onto src/libc/ instead of an apparent
 * cycle back into main.c.
 */
#ifndef LIBC_SYSCALLS_H
#define LIBC_SYSCALLS_H

#include <stddef.h>
#include <stdint.h>

/* Console sink: every byte out of the UART, including Lua's print(). */
int _write(int fd, const char *ptr, int len);

/* Console source: blocks for one byte, EOT (0x04) reported as EOF. */
int _read(int fd, char *ptr, int len);

/* Heap into the 1 MB external RAM window. */
void *_sbrk(ptrdiff_t incr);

/* Halt in place - no OS here to take a signal. */
void abort(void) __attribute__((noreturn));

/**
 * @brief counter_timer as of the last byte _read() took off the console.
 *
 * The REPL's idle pump gates the ~5 ms RFID coupler scan on the console having
 * been quiet a while, so a scan cannot stall RX mid-transfer and overflow the
 * 16-byte UART FIFO. _read owns the timestamp; this accessor keeps the static
 * encapsulated here rather than exporting the variable itself.
 *
 * It is the ONLY part of the #324 move that is not free, and the cost is
 * measured, not estimated: the rest of the move is 0 B to the byte, this
 * accessor is +12 B with -4 B back at the call site, so +8 B net. Publishing
 * `extern uint32_t console_last_ms` instead measures at exactly 0 B - the
 * trade is 8 bytes against a mutable global in the interface, and out of
 * 7,644 B free the encapsulation is the better buy. Revisit if flash gets
 * tight; it is a one-line change in each of three files.
 */
uint32_t console_last_rx_ms(void);

#endif /* LIBC_SYSCALLS_H */
