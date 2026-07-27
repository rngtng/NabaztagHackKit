/**
 * @file ota.c
 * @brief Whole-image OTA flash writer: V1's flash_uc, ported and disciplined.
 *
 * See hal/ota.h for the contract and the brick-risk note. This mirrors
 * hal/config.c's cfg_program exactly - same OKI SPD erase/program sequences,
 * same "self-contained .ramfunc leaf, no calls out to flash" rule - but loops
 * over every sector from address 0 (config.c is fixed to sector 31) and ends
 * with V1 flash_uc's watchdog-overflow reset instead of returning.
 *
 * The whole body is one leaf with the program sequence inlined (no helper, as
 * in config.c): a BL to a flash-resident function - or an ARM/Thumb interworking
 * veneer in flash - would fetch from the flash being erased and hang.
 */
#include "ml674061.h"
#include "common.h"

#include "hal/ota.h"

#define CFG_FLASH  0x08000000UL   /* internal flash base */
#define SECTOR_SZ  0x1000UL       /* 4 KB erase granularity */

__attribute__((section(".ramfunc"), noinline))
void ota_flash_image(const uint8_t *img, uint32_t len)
{
  uint32_t base, i;

  /* Refuse anything that would run into the config sector or is empty - do it
   * here too, not only in the caller, so the writer is safe on its own. */
  if (len == 0 || len > OTA_MAX_IMAGE)
    return;

  set_bit(TBGCON, TBGCON_WDHLT); /* halt watchdog clock during programming */

  for (base = 0; base < len; base += SECTOR_SZ) {
    /* Erase the sector at CFG_FLASH+base (SPD sector-erase, config.c's seq). */
    put_value(FLACON, 0x03);
    __no_operation();
    __no_operation();
    __no_operation();
    put_value(CFG_FLASH + 0x15554, 0xAA);
    put_value(CFG_FLASH + 0x0AAA8, 0x55);
    put_value(CFG_FLASH + 0x15554, 0x80);
    put_value(CFG_FLASH + 0x15554, 0xAA);
    put_value(CFG_FLASH + 0x0AAA8, 0x55);
    put_value(CFG_FLASH + base, 0x30);
    __no_operation();
    __no_operation();
    __no_operation();
    while ((get_value(FLACON) & (0x04 | 0x8 | 0x2)) != 0x2);

    /* Program the sector, 4 bytes at a time, straight from the image. Bytes
     * past `len` (the final partial sector) stay erased (0xFF); an all-FF word
     * is already the erased state, so skip it. */
    for (i = 0; i < SECTOR_SZ; i += 4) {
      uint32_t idx = base + i;
      uint8_t b0, b1, b2, b3;
      if (idx >= len)
        break;
      b0 = img[idx];
      b1 = (idx + 1 < len) ? img[idx + 1] : 0xFF;
      b2 = (idx + 2 < len) ? img[idx + 2] : 0xFF;
      b3 = (idx + 3 < len) ? img[idx + 3] : 0xFF;
      if (b0 == 0xFF && b1 == 0xFF && b2 == 0xFF && b3 == 0xFF)
        continue;
      put_value(FLACON, 0x03);
      __no_operation();
      __no_operation();
      __no_operation();
      put_value(CFG_FLASH + 0x15554, 0xAA);
      put_value(CFG_FLASH + 0x0AAA8, 0x55);
      put_value(CFG_FLASH + 0x15554, 0xA0);
      put_value(CFG_FLASH + idx, b0);
      put_value(CFG_FLASH + idx + 1, b1);
      put_value(CFG_FLASH + idx + 2, b2);
      put_value(CFG_FLASH + idx + 3, b3);
      __no_operation();
      __no_operation();
      __no_operation();
      while ((get_value(FLACON) & (0x04 | 0x8 | 0x2)) != 0x2);
    }
  }

  put_value(FLACON, 0x02); /* programming prohibited again */
  __no_operation();
  __no_operation();
  __no_operation();
  __no_operation();

  /* V1 flash_uc's watchdog-overflow reset: reboot into the freshly written
   * image. Interrupts are already masked by the caller; this never returns. */
  put_value(INTST, 0x3c);  /* release write protection */
  put_value(INTST, 0x00);  /* clear status bit */
  put_value(TBGCON, 0x5A); /* release write protection */
  put_value(TBGCON, 0x40); /* WD clock = APB/32, reset on overflow, counter on */
  put_value(OVFAST, 0x63); /* RSTOUT_N assert time ~100us */
  put_value(WDTCON, 0x3C); /* start watchdog */
  put_hvalue(WDTCNT, 0x5A5A); /* release write protection */
  put_hvalue(WDTCNT, 0xFF00); /* force counter to overflow (~256us) */
  while (1);
}
