/**
 * @file rfidprobe.c
 * @brief M9 probe (issue #117): confirm the I2C bus works and the CRX14 RFID
 *        coupler responds before trusting the full nab.rfid() binding.
 *
 * The board teardown (docs/hardware-dissection.md) photo- and text-confirms a
 * "STMicro CR14 contactless coupler... I2C interface" - so unlike the M6 AT45
 * flash phantom (#94, no chip on this board revision), the peripheral's
 * existence is already documented. Per the CLAUDE.md peripheral-exists rule we
 * still prove it responds over the bus, since a populated I2C header is not
 * proof the chip answers: bring up I2C, then do the same parameter-register
 * write(0x10)/read-back round trip init_rfid() does (turn the RF field on and
 * read it back) - a live coupler answers 0x10, a floating/dead bus does not.
 *
 * This exercises hal/i2c.c (the generic bus primitive) directly rather than
 * the higher hal/rfid.c layer, so a failure localises to "no I2C ACK" vs
 * "I2C fine, CRX14 silent" instead of blaming the whole driver stack.
 *
 * Output is ARM semihosting (the M3 #91 console), so run it debugger-attached:
 *   task repl:firmwareV2:hw APP=rfidprobe
 */
#include "ml674061.h"
#include "common.h"

#include "hal/i2c.h"
#include "hal/rfid.h"   /* CRX14_ADDR / CRX14_PARAMETER_REGISTER only */

/* ---- semihosting console (M3 #91 path) ----------------------------------- */
#define SYS_WRITEC 0x03

static inline int semihost(int op, void *arg)
{
  register int r0 asm("r0") = op;
  register void *r1 asm("r1") = arg;
  asm volatile("svc #0xAB" : "+r"(r0) : "r"(r1) : "memory");
  return r0;
}

static void sh_puts(const char *s)
{
  while (*s) {
    char c = *s++;
    semihost(SYS_WRITEC, &c);
  }
}

static void sh_puthex8(uint8_t v)
{
  const char *hex = "0123456789abcdef";
  char b[5];
  b[0] = '0'; b[1] = 'x';
  b[2] = hex[(v >> 4) & 0xF];
  b[3] = hex[v & 0xF];
  b[4] = '\0';
  sh_puts(b);
}

int main(void)
{
  init_i2c();

  sh_puts("M9 RFID probe (CRX14 on I2C 0xA0)\n");

  /* Turn the RF field ON: write PARAMETER_REGISTER=0x10, read it back. A live
   * CRX14 ACKs both phases and echoes 0x10; a missing/dead chip NAKs the
   * write_i2c/read_i2c retry loop (3 attempts - enough to rule out a transient
   * bus glitch; each failed attempt spins waiti2cmbb/waiti2cmcf for up to 1M
   * iterations so more retries quickly overflow the 120s semihosting cap). */
  uint8_t cmd[2] = {CRX14_PARAMETER_REGISTER, 0x10};
  uint8_t tries = 3;
  uint8_t wrote = 0;
  sh_puts("write(PARAM=0x10):");
  while (tries-- && !(wrote = write_i2c(CRX14_ADDR, cmd, 2)))
    sh_puts(".");
  sh_puts(wrote ? " ACK\n" : " NO ACK (I2C bus or CRX14 not responding)\n");

  uint8_t readback = 0xFF;
  uint8_t read_ok = 0;
  if (wrote) {
    tries = 3;
    sh_puts("read(PARAM):");
    while (tries-- && !(read_ok = read_i2c(CRX14_ADDR, &readback, 1)))
      sh_puts(".");
  }
  if (read_ok) {
    sh_puts(" value=");
    sh_puthex8(readback);
    sh_puts(readback == 0x10 ? "  -> CRX14 ALIVE\n" : "  -> unexpected value\n");
  } else if (wrote) {
    sh_puts(" NO ACK\n");
  }

  /* Leave the field off (completion_rfid + PARAM=0) so a subsequent nab.rfid()
   * boot starts from a clean state; ignore the result, this is just a probe. */
  cmd[1] = 0x00;
  write_i2c(CRX14_ADDR, cmd, 2);

  sh_puts("<<FV_DONE>>\n");   /* early-exit signal for flash.py */
  for (;;) {
    /* idle; the report above is the whole probe */
  }
  return 0;
}
