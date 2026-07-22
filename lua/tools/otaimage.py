#!/usr/bin/env python3
"""Stamp a firmware .bin into an uploadable OTA image (#235).

Prepends a fixed 16-byte header - magic, header version, target-hardware id,
firmware version, image length and CRC-32 - to the raw firmware binary,
producing the `.fw` file the setup-page uploader (net/ota.lua) verifies before
it flashes. The device refuses any file whose magic / hardware id / length /
CRC do not match, so a wrong or corrupt upload can never start a flash (the
core of #235's brick-safety - verification happens before any erase).

Header layout (big-endian, matches net/ota.lua's string.unpack ">c4BBI2I4I4"):
  magic[4]='NBZF'  hdr_ver:u8=1  hw_id:u8  fw_ver:u16  img_len:u32  crc32:u32

The CRC is the standard zlib/IEEE CRC-32 over the image bytes only (not the
header); net/ota.lua recomputes the same polynomial in pure Lua. Stdlib only.
"""
import argparse
import struct
import sys
import zlib

MAGIC = b"NBZF"
HDR_VERSION = 1
HW_ID_LUA = 1          # ml67q4051 lua board - must match net/ota.lua's HW_ID
FLASH_BUDGET = 0x1F000  # image must fit below the config sector (matches ota.h)


def stamp(image: bytes, hw_id: int, fw_version: int) -> bytes:
    crc = zlib.crc32(image) & 0xFFFFFFFF
    header = struct.pack(">4sBBHII", MAGIC, HDR_VERSION, hw_id, fw_version,
                         len(image), crc)
    return header + image


def main() -> None:
    ap = argparse.ArgumentParser(
        description="Stamp a raw firmware .bin into a .fw OTA image")
    ap.add_argument("input", help="raw firmware binary (e.g. bin/firmware.bin)")
    ap.add_argument("-o", "--output", required=True, help="output .fw path")
    ap.add_argument("--hw-id", type=int, default=HW_ID_LUA,
                    help=f"target hardware id (default {HW_ID_LUA})")
    ap.add_argument("--version", type=int, default=1,
                    help="firmware version, u16 (default 1)")
    args = ap.parse_args()

    with open(args.input, "rb") as f:
        image = f.read()
    if not image:
        sys.exit("otaimage: empty input image")
    if len(image) > FLASH_BUDGET:
        sys.exit(f"otaimage: image {len(image)} B exceeds the "
                 f"{FLASH_BUDGET} B flash budget")
    if not 0 <= args.version <= 0xFFFF:
        sys.exit("otaimage: --version must fit in a u16 (0..65535)")

    out = stamp(image, args.hw_id, args.version)
    with open(args.output, "wb") as f:
        f.write(out)
    print(f"otaimage: {args.output} ({len(out)} B = 16 B header + "
          f"{len(image)} B image, hw {args.hw_id}, v{args.version}, "
          f"crc {zlib.crc32(image) & 0xFFFFFFFF:08x})")


if __name__ == "__main__":
    main()
