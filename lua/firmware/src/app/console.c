/**
 * @file console.c
 * @brief Semihosting-console probe: prove ARM semihosting I/O works on real
 *        hardware under OpenOCD, not only in the Unicorn sim.
 *
 * Issues the same Thumb `svc 0xAB` semihosting call lua.c uses for its REPL
 * console, via SYS_WRITEC (0x03) - one svc per char, the exact path newlib
 * _write() and hence Lua print() take. Each char is a separate SWI trap +
 * service + resume, so a clean run also proves OpenOCD's post-semihosting
 * resume path is stable on this ARM7TDMI (EmbeddedICE v1, no vector catch).
 *
 * Serviced by OpenOCD when attached over JTAG with `arm semihosting enable` and
 * a HARDWARE breakpoint at the SWI vector 0x8 (the auto soft breakpoint fails -
 * 0x8 is flash-mapped read-only). Run bare metal (no debugger) the firmware's
 * own swi_handler mis-decodes 0xAB and crashes, so this is debugger-attached by
 * design. Takes no HAL: a failure points at the semihosting path, not the
 * peripheral bring-up.
 */
#define SYS_WRITEC 0x03

static inline int semihost(int op, void *arg)
{
  register int r0 asm("r0") = op;
  register void *r1 asm("r1") = arg;
  asm volatile("svc #0xAB" : "+r"(r0) : "r"(r1) : "memory");
  return r0;
}

int main(void)
{
  const char *s = "M3 WRITEC OK\n";
  while (*s) {
    char c = *s++;
    semihost(SYS_WRITEC, &c);
  }

  for (;;) {
    /* idle - the line above is the whole test */
  }
  return 0;
}
