/**
 * @file syscalls.c
 * @brief The newlib syscalls this bare-metal target has to supply itself:
 *        stdout/stdin over the polled UART0 console, the heap in ExtRAM, and
 *        an abort() that halts instead of raising a signal.
 *
 * Lifted verbatim out of src/main.c (#324). They sat next to main(), which is
 * the one TU nothing can link, so the console's read/write ends were as
 * untestable as they were unnamed. Here they join libc_shim.c under the one
 * concern this firmware genuinely has - everything that exists to keep newlib
 * out of the 124 KB flash budget - and _write finally has a header to be
 * declared in (inc/libc/syscalls.h), which is what turns utils/fmt.c's
 * dependency on it into an ordinary downward edge.
 *
 * These definitions win over libnosys' stubs because an object file beats an
 * archive member. That holds for any .o in the link, so the move is safe -
 * but it is a linker property, so check obj/firmware.map rather than trust it.
 *
 * No behaviour change in the move: same code, same order, one static (the
 * console RX timestamp) now reached through an accessor instead of directly.
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>

#include "hal/uart.h"    /* console: polled UART0 TX/RX (#207) */
#include "utils/delay.h" /* counter_timer - the 1 ms tick */

#include "libc/syscalls.h"

/* ---- stdout: the UART console -------------------------------------------- */

int _write(int fd, const char *ptr, int len)
{
  (void)fd;
  for (int i = 0; i < len; i++)
    putch_uart((uint8_t)ptr[i]);
  return len;
}

/* ---- stdin: the UART console --------------------------------------------- */
/* EOF: getch_uart() is non-blocking (-1 = RX FIFO empty) and a raw UART has no
 * native end-of-stream, so _read() blocks until a byte arrives and treats EOT
 * (0x04, what Ctrl-D sends) as EOF - EOF is what ends the REPL loop and fires
 * <<FV_DONE>>. The host feeder (replpipe/flash.py/simulator) appends EOT after
 * the input it sends; hex #LC frames and source lines never contain 0x04. */
#define CONSOLE_EOF 0x04   /* EOT / Ctrl-D: end of console input */

/* Tick timestamp of the last console RX byte: the REPL's idle pump gates the
 * ~5 ms RFID coupler scan on the console having been quiet a while, so a scan
 * can't stall RX mid-transfer and overflow the 16-byte FIFO (see repl). */
static uint32_t console_last_ms;

int _read(int fd, char *ptr, int len)
{
  (void)fd;
  if (len <= 0)
    return 0;
  int c;
  while ((c = getch_uart()) < 0)
    ;                      /* block: no byte yet (UART has no native EOF) */
  console_last_ms = counter_timer;
  if (c == CONSOLE_EOF)
    return 0;              /* EOT -> EOF, ends the REPL */
  ptr[0] = (char)c;
  return 1;               /* one char per call; sh_gets reassembles the line */
}

uint32_t console_last_rx_ms(void)
{
  return console_last_ms;
}

/* ---- abort: halt, don't raise(SIGABRT) ----------------------------------- */
/* newlib's abort() calls raise() + _exit(), pulling the signal machinery
 * (raise/signal/_kill_r/_getpid). Bare metal has no OS to signal, so halt in
 * place. */
void abort(void)
{
  for (;;) {
  }
}

/* ---- heap ---------------------------------------------------------------- */
/* Heap = the 1 MB external RAM window; IntRAM is too small for a Lua state. */
extern char __extram_start__, __extram_end__;

void *_sbrk(ptrdiff_t incr)
{
  static char *cur;
  if (cur == NULL)
    cur = &__extram_start__;
  if (cur + incr > &__extram_end__) {
    errno = ENOMEM;
    return (void *)-1;
  }
  char *prev = cur;
  cur += incr;
  return prev;
}
