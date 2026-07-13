/**
 * @file hello.c
 * @brief Toolchain-validation app: spins forever. Its only job is to prove the
 *        bring-up chain end to end - the ARM7TDMI startup in sys/asm/init.s
 *        (clock + EMC init, per-mode stacks, .data relocation, .bss zeroing)
 *        reaches main(), and the image links against newlib-nano under the
 *        ml67q4051.ld memory map.
 *
 * Deliberately hardware-silent (no LED, no console). Validate over JTAG: the
 * CPU sitting in this loop, confirmed via `mon reg` / halt showing the PC here.
 */
int main(void)
{
  for (;;) {
    /* idle */
  }
  return 0;
}
