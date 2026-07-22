/**
 * @file hal/ota.h
 * @brief Whole-image OTA flash writer (#235): V1's flash_uc, blob-only.
 *
 * The one mechanism that replaces the entire internal-flash firmware from a RAM
 * buffer and reboots into it - the JTAG-free update path. Port of the proven V1
 * writer (mtl/firmware/src/utils/mem.c: flash_uc / write_uc_flash_sec - the MTL
 * VM's sysFlash has shipped this for years; see PROVENANCE.md), reduced to this
 * one job and sharing the SPD erase/program sequences already ported to
 * hal/config.c.
 *
 * BRICK RISK (#235): unlike config.c (bounded to sector 31), this writes from
 * flash address 0. There is no A/B slot - the image is ~110 KB of 124 KB - so
 * the unpowerable window is real. The engineering minimises it: the caller
 * verifies the whole image (magic + target id + length + CRC, all in Lua under
 * net.ota, BEFORE this is ever called), and this routine only ever writes
 * sectors 0..N below the config sector (it refuses len > OTA_MAX_IMAGE), so a
 * wrong/corrupt file cannot start a flash and the persisted creds survive.
 */
#ifndef _HAL_OTA_H_
#define _HAL_OTA_H_

#include <stdint.h>

/* Largest flashable image: everything below the config sector (0x1F000). An
 * over-budget image is refused before any erase (the running build already
 * fails the link on overflow, but the writer guards independently). */
#define OTA_MAX_IMAGE 0x1F000UL

/* Erase-and-program the internal flash from `img` (len bytes, in RAM - a Lua
 * string lives in the ExtRAM heap) starting at 0x08000000, then trigger a
 * watchdog reset into the new image. Runs from IntRAM (.ramfunc): a
 * self-contained leaf that fetches no code or data from the flash it is
 * erasing. The caller MUST mask interrupts first (vectors live in flash) and
 * MUST have verified the image. Returns only on refusal (len 0 or
 * > OTA_MAX_IMAGE); on success it never returns - the watchdog reboots. */
void ota_flash_image(const uint8_t *img, uint32_t len);

#endif /* _HAL_OTA_H_ */
