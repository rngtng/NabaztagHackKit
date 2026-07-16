/**
 * @file config.c
 * @brief Config-sector persistence: V1's internal-flash writer, blob-only.
 *
 * Port of the proven V1 writer (mtl/firmware/src/utils/mem.c:
 * write_uc_flash_sec / read_uc_flash - OKI 2005 via RedoX's 2015 GCC port,
 * see PROVENANCE.md), reduced to the one job this track needs: persist a
 * single fixed-layout record in the last 4 KB sector (0x1F000-0x1FFFF) of
 * internal flash. V1 kept a caller-supplied 4 KB shadow buffer to preserve
 * unrelated data sharing the sector; here the record IS the whole sector
 * layout, so the shadow (and V1's index/length parameters) are gone.
 *
 * Brick-risk containment (#214): cfg_program takes NO address - the sector
 * base is the compile-time constant CFG_SECTOR, and the byte offset is
 * bounded by sizeof(struct config_rec) << 4096. The SPD erase command is
 * issued at CFG_SECTOR only, so the writer physically cannot touch the
 * firmware below 0x1F000.
 */
#include "ml674061.h"
#include "common.h"

#include "hal/config.h"

#include <string.h>

#define CFG_FLASH   0x08000000UL             /* internal flash base */
#define CFG_SECTOR  (CFG_FLASH + 0x1F000UL)  /* sector 31: the config sector */
#define CFG_VERSION 1

/* On-flash record. Byte-granular on purpose (no compiler padding holes) and
 * word-padded by hand: cfg_program writes 4 bytes per SPD command. */
struct config_rec {
  uint8_t magic[4];                 /* "NCFG" */
  uint8_t version;                  /* CFG_VERSION; bump on layout change */
  uint8_t cksum;                    /* makes the whole record sum to 0 mod 256 */
  char ssid[CONFIG_SSID_MAX + 1];
  char psk[CONFIG_PSK_MAX + 1];
  char url[CONFIG_URL_MAX + 1];
  uint8_t pad[3];                   /* size up to a multiple of 4 */
};
_Static_assert(sizeof(struct config_rec) % 4 == 0, "word-granular programming");

/* Byte sum of the whole record; a valid record sums to 0 (mod 256). */
static uint8_t rec_sum(const struct config_rec *rec)
{
  const uint8_t *p = (const uint8_t *)rec;
  uint8_t sum = 0;
  for (uint32_t i = 0; i < sizeof *rec; i++)
    sum += p[i];
  return sum;
}

/* Erase the config sector and program `nb` bytes from `src` at its base.
 *
 * Runs from IntRAM (.ramfunc -> the .data output section): flash supplies no
 * code OR data while erasing/programming itself, so this must be a
 * self-contained leaf - no calls out (a BL to flash, incl. an interworking
 * veneer or memcpy, would fetch from the busy flash) and `src` must live in
 * RAM. The caller masks IRQs first (vectors + handlers are in flash) and
 * `nb` is a multiple of 4 (record is word-padded). The SPD command
 * sequences, FLACON handshakes and watchdog-clock halt are V1's, verbatim.
 */
__attribute__((section(".ramfunc"), noinline))
static void cfg_program(const uint8_t *src, uint32_t nb)
{
  uint32_t i;

  set_bit(TBGCON, TBGCON_WDHLT);    /* halt watchdog clock: this takes ~63 ms */

  /* SPD command: erase sector 31 (4 KB, 0x1F000-0x1FFFF) */
  put_value(FLACON, 0x03);          /* flash programming permitted */
  __no_operation();
  __no_operation();
  __no_operation();
  put_value(CFG_FLASH + 0x15554, 0xAA);
  put_value(CFG_FLASH + 0x0AAA8, 0x55);
  put_value(CFG_FLASH + 0x15554, 0x80);
  put_value(CFG_FLASH + 0x15554, 0xAA);
  put_value(CFG_FLASH + 0x0AAA8, 0x55);
  put_value(CFG_SECTOR, 0x30);
  __no_operation();
  __no_operation();
  __no_operation();
  /* wait for the completion of the command */
  while ((get_value(FLACON) & (0x04 | 0x8 | 0x2)) != 0x2);

  /* SPD command per 4 bytes; an all-FF word is already the erased state */
  for (i = 0; i < nb; i += 4) {
    if (src[i] == 0xFF && src[i + 1] == 0xFF &&
        src[i + 2] == 0xFF && src[i + 3] == 0xFF)
      continue;
    put_value(FLACON, 0x03);
    __no_operation();
    __no_operation();
    __no_operation();
    put_value(CFG_FLASH + 0x15554, 0xAA);
    put_value(CFG_FLASH + 0x0AAA8, 0x55);
    put_value(CFG_FLASH + 0x15554, 0xA0);
    put_value(CFG_SECTOR + i, src[i]);
    put_value(CFG_SECTOR + i + 1, src[i + 1]);
    put_value(CFG_SECTOR + i + 2, src[i + 2]);
    put_value(CFG_SECTOR + i + 3, src[i + 3]);
    __no_operation();
    __no_operation();
    __no_operation();
    while ((get_value(FLACON) & (0x04 | 0x8 | 0x2)) != 0x2);
  }

  put_value(FLACON, 0x02);          /* programming prohibited again */
  __no_operation();
  __no_operation();
  __no_operation();
  __no_operation();

  clr_bit(TBGCON, TBGCON_WDHLT);    /* watchdog clock back on */
}

int8_t config_load(nab_config_t *out)
{
  const struct config_rec *rec = (const struct config_rec *)CFG_SECTOR;

  if (rec->magic[0] != 'N' || rec->magic[1] != 'C' ||
      rec->magic[2] != 'F' || rec->magic[3] != 'G')
    return -1;                      /* blank sector reads 0xFF everywhere */
  if (rec->version != CFG_VERSION || rec_sum(rec) != 0)
    return -1;

  memcpy(out->ssid, rec->ssid, sizeof out->ssid);
  memcpy(out->psk, rec->psk, sizeof out->psk);
  memcpy(out->url, rec->url, sizeof out->url);
  /* a checksummed record has NUL-terminated fields by construction (below),
   * but cap them anyway - out feeds straight into Lua strings */
  out->ssid[CONFIG_SSID_MAX] = '\0';
  out->psk[CONFIG_PSK_MAX] = '\0';
  out->url[CONFIG_URL_MAX] = '\0';
  return 0;
}

int8_t config_save(const nab_config_t *cfg)
{
  struct config_rec rec;            /* on the stack = IntRAM, readable while
                                       flash programs itself */

  if (strlen(cfg->ssid) > CONFIG_SSID_MAX || strlen(cfg->psk) > CONFIG_PSK_MAX ||
      strlen(cfg->url) > CONFIG_URL_MAX)
    return -2;

  /* zero-fill so the tails past each NUL are deterministic - the unchanged
   * check below is a straight memcmp against flash */
  memset(&rec, 0, sizeof rec);
  rec.magic[0] = 'N';
  rec.magic[1] = 'C';
  rec.magic[2] = 'F';
  rec.magic[3] = 'G';
  rec.version = CFG_VERSION;
  strcpy(rec.ssid, cfg->ssid);
  strcpy(rec.psk, cfg->psk);
  strcpy(rec.url, cfg->url);
  rec.cksum = (uint8_t)-rec_sum(&rec);

  if (memcmp((const void *)CFG_SECTOR, &rec, sizeof rec) == 0)
    return 1;                       /* identical - don't burn an erase cycle */

  __disable_interrupt();
  cfg_program((const uint8_t *)&rec, sizeof rec);
  __enable_interrupt();

  return (int8_t)(memcmp((const void *)CFG_SECTOR, &rec, sizeof rec) == 0 ? 0 : -1);
}
