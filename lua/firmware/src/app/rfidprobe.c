/**
 * @file rfidprobe.c
 * @brief Confirm the I2C bus works and the CRX14 RFID coupler responds before
 *        trusting the full nab.rfid() binding. Bring up I2C, then do the same
 *        parameter-register write(0x10)/read-back round trip init_rfid() does
 *        (turn the RF field on and read it back) - a live coupler answers 0x10,
 *        a floating/dead bus does not.
 *
 * Exercises hal/i2c.c (the generic bus primitive) directly rather than the
 * higher hal/rfid.c layer, so a failure localises to "no I2C ACK" vs "I2C fine,
 * CRX14 silent" instead of blaming the whole driver stack.
 *
 * Output is on UART0 (38400 8N1), read on the Pi's /dev/serial0 (see
 * uartprobe.c for the flash+listen recipe):
 *   task lua:firmware:flash APP=rfidprobe
 */
#include "ml674061.h"
#include "common.h"

#include "hal/i2c.h"
#include "hal/rfid.h"   /* CRX14_ADDR / CRX14_PARAMETER_REGISTER only */
#include "hal/uart.h"

static void sh_puts(const char *s)
{
  putst_uart((uint8_t *)s);
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
  init_uart();
  init_i2c();

  sh_puts("M9 RFID probe (CRX14 on I2C 0xA0)\n");

  /* Turn the RF field ON: write PARAMETER_REGISTER=0x10, read it back. A live
   * CRX14 ACKs both phases and echoes 0x10; a missing/dead chip NAKs the
   * write_i2c/read_i2c retry loop (3 attempts - enough to rule out a transient
   * bus glitch; each failed attempt spins waiti2cmbb/waiti2cmcf for up to 1M
   * iterations so more retries quickly overflow the 120s flash-run cap). */
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
    /* idle */
  }
  return 0;
}
