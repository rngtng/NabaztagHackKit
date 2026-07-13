/**
 * @file hello.c
 * @brief M0 toolchain-validation app (issue #88).
 *
 * Does nothing but spin. Its only job is to prove the bring-up chain end to
 * end: the ARM7TDMI startup in sys/asm/init.s (clock + external-memory-
 * controller init, per-mode stacks, .data relocation, .bss zeroing) reaches
 * main(), and the whole image links against newlib-nano under the
 * ml67q4051.ld memory map.
 *
 * This is deliberately hardware-silent - no LED, no console - because M0 has
 * neither a peripheral driver (M1, #89) nor a console (M3, #91) yet. Validate
 * that it links and, once M2 (#90) lands the flash workflow, that it loads and
 * runs over JTAG (the CPU sitting in this loop, confirmed via `mon reg` /
 * halt showing the PC parked here).
 */
int main(void)
{
  for (;;) {
    /* idle - M1 replaces this app with an LED blink */
  }
  return 0;
}
