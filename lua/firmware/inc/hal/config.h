/**
 * @file hal/config.h
 * @brief Persistent config blob in the internal-flash config sector (#214).
 *
 * The last 4 KB sector of internal flash (0x1F000-0x1FFFF, already carved out
 * of IntROM by sys/ml67q4051.ld) holds one fixed-layout record: wifi SSID +
 * PSK + server URL behind a magic/version/checksum header. The writer is V1's
 * proven write_uc_flash_sec (mtl/firmware/src/utils/mem.c) reduced to this one
 * sector - see src/hal/config.c and PROVENANCE.md.
 *
 * This HAL owns the sector layout end to end (sandbox principle 5): callers
 * exchange a nab_config struct, never an address or length. Writes erase-cycle
 * the sector, so config_save skips (returns 1) when flash already holds the
 * identical record - callers need no own wear guard.
 */
#ifndef _HAL_CONFIG_H_
#define _HAL_CONFIG_H_

#include <stdint.h>

#define CONFIG_SSID_MAX 32   /* 802.11 SSID limit */
#define CONFIG_PSK_MAX  64   /* WPA passphrase is 8..63 chars */
#define CONFIG_URL_MAX  64

typedef struct {
  char ssid[CONFIG_SSID_MAX + 1];   /* all fields NUL-terminated; "" = unset */
  char psk[CONFIG_PSK_MAX + 1];
  char url[CONFIG_URL_MAX + 1];
} nab_config_t;

/* Copy the persisted record into *out. Returns 0 on a valid record, <0 when
 * the sector is blank (never written / erased) or fails the checksum. */
int8_t config_load(nab_config_t *out);

/* Persist *cfg: erase the config sector and program a fresh record, then
 * verify by read-back. Interrupts are disabled for the duration (~63 ms -
 * flash supplies no code/data while programming itself, and the IRQ vectors
 * live in flash). Returns 0 written+verified, 1 skipped (flash already holds
 * this exact record), <0 on overlong fields or a failed read-back. */
int8_t config_save(const nab_config_t *cfg);

#endif /* _HAL_CONFIG_H_ */
