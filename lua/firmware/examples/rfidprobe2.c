/**
 * @file rfidprobe2.c
 * @brief M9 probe: dump raw CRX14 frame-buffer bytes at each anti-collision
 *        step so we can identify where the UID read returns zeros.
 *
 * Mirrors rfidprobe.c's UART0 console; run with:
 *   task lua:firmware:flash EXAMPLE=rfidprobe2
 * Output is on UART0 (38400 8N1), read on the Pi's /dev/serial0 (see
 * uartprobe.c for the flash+listen recipe).
 */
#include "ml674061.h"
#include "common.h"

#include "hal/i2c.h"
#include "hal/rfid.h"
#include "hal/uart.h"

static void sh_puts(const char *s)
{
  putst_uart((uint8_t *)s);
}

static void sh_puthex8(uint8_t v)
{
  const char *h = "0123456789abcdef";
  char b[3] = {h[v>>4], h[v&0xF], '\0'};
  sh_puts(b);
}

static void sh_dump(const char *label, uint8_t *buf, uint8_t n)
{
  sh_puts(label);
  for (uint8_t i = 0; i < n; i++) {
    sh_puthex8(buf[i]);
    sh_puts(" ");
  }
  sh_puts("\n");
}

/* Reduced-retry helpers that don't hang for seconds on NAK */
static uint8_t wr(uint8_t *d, uint8_t n)
{
  for (uint8_t t = 3; t--; )
    if (write_i2c(CRX14_ADDR, d, n)) return 1;
  return 0;
}
static uint8_t rd(uint8_t *d, uint8_t n)
{
  for (uint8_t t = 3; t--; )
    if (read_i2c(CRX14_ADDR, d, n)) return 1;
  return 0;
}

static void rfid_delay(void)
{
  volatile unsigned long n = 200000;
  while (n--) CLR_WDT;
}

int main(void)
{
  uint8_t buf[20];
  uint8_t cmd[4];

  init_uart();
  init_i2c();
  sh_puts("M9 RFID raw-dump probe\n");

  /* -- init_rfid: turn on RF field (PARAM=0x10) -- */
  cmd[0] = CRX14_PARAMETER_REGISTER; cmd[1] = 0x00;
  wr(cmd, 2);                                   /* close: PARAM=0x00 */
  cmd[1] = 0x10;
  if (!wr(cmd, 2) || !rd(buf, 1)) { sh_puts("init_rfid failed\n"); goto done; }
  sh_dump("PARAM readback: ", buf, 1);
  rfid_delay();

  /* -- initiate_rfid -- */
  cmd[0] = CRX14_IO_FRAME_REGISTER;
  cmd[1] = 0x02; cmd[2] = 0x06; cmd[3] = 0x00;
  if (!wr(cmd, 4)) { sh_puts("initiate write failed\n"); goto done; }
  rfid_delay();

  /* read frame: 2 bytes */
  cmd[0] = CRX14_IO_FRAME_REGISTER;
  if (!wr(cmd, 1) || !rd(buf, 2)) { sh_puts("initiate read failed\n"); goto done; }
  sh_dump("after INITIATE (2B): ", buf, 2);

  if (buf[0] == 0x00) { sh_puts("no tag in field\n"); goto done; }

  /* -- slot_marker_rfid -- */
  cmd[0] = CRX14_SLOT_MARKER_REGISTER;
  if (!wr(cmd, 1)) { sh_puts("slot_marker write failed\n"); goto done; }
  rfid_delay();

  cmd[0] = CRX14_IO_FRAME_REGISTER;
  if (!wr(cmd, 1) || !rd(buf, 19)) { sh_puts("slot_marker read failed\n"); goto done; }
  sh_dump("after SLOT_MARKER (19B): ", buf, 19);

  /* extract chip_id of first detected tag */
  uint16_t mask = (uint16_t)(buf[2] << 8) | buf[1];
  uint8_t chip_id = 0xFF;
  for (uint8_t slot = 0; slot < 16; slot++) {
    if (mask & (1u << slot)) { chip_id = buf[slot + 3]; break; }
  }
  if (chip_id == 0xFF) { sh_puts("no chip_id in bitmask\n"); goto done; }
  sh_puts("chip_id: "); sh_puthex8(chip_id); sh_puts("\n");

  /* -- select_tag_rfid(chip_id) -- */
  cmd[0] = CRX14_IO_FRAME_REGISTER;
  cmd[1] = 0x02; cmd[2] = 0x0E; cmd[3] = chip_id;
  if (!wr(cmd, 4)) { sh_puts("select write failed\n"); goto done; }
  rfid_delay();

  cmd[0] = CRX14_IO_FRAME_REGISTER;
  if (!wr(cmd, 1) || !rd(buf, 2)) { sh_puts("select read failed\n"); goto done; }
  sh_dump("after SELECT (2B): ", buf, 2);

  /* -- get_uid_rfid -- */
  cmd[0] = CRX14_IO_FRAME_REGISTER;
  cmd[1] = 0x01; cmd[2] = 0x0B;
  if (!wr(cmd, 3)) { sh_puts("get_uid write failed\n"); goto done; }
  rfid_delay();

  cmd[0] = CRX14_IO_FRAME_REGISTER;
  if (!wr(cmd, 1) || !rd(buf, 9)) { sh_puts("get_uid read failed\n"); goto done; }
  sh_dump("after GET_UID (9B): ", buf, 9);

  /* interpret as UID (bytes 8..1 reversed into uid[0..7]) */
  sh_puts("UID (bytes 1-8 reversed): ");
  for (uint8_t i = 0; i < 8; i++) sh_puthex8(buf[7 - i + 1]);
  sh_puts("\n");

done:
  /* turn off field */
  cmd[0] = CRX14_PARAMETER_REGISTER; cmd[1] = 0x00;
  wr(cmd, 2);

  sh_puts("<<FV_DONE>>\n");
  for (;;) {}
  return 0;
}
