#!/usr/bin/env python3
"""Sign/decrypt Nabaztag firmware .sim files for web upload."""
import sys

INVB = [
      1, 171, 205, 183,  57, 163, 197, 239, 241,  27,  61, 167,  41,  19,  53, 223,
    225, 139, 173, 151,  25, 131, 165, 207, 209, 251,  29, 135,   9, 243,  21, 191,
    193, 107, 141, 119, 249,  99, 133, 175, 177, 219, 253, 103, 233, 211, 245, 159,
    161,  75, 109,  87, 217,  67, 101, 143, 145, 187, 221,  71, 201, 179, 213, 127,
    129,  43,  77,  55, 185,  35,  69, 111, 113, 155, 189,  39, 169, 147, 181,  95,
     97,  11,  45,  23, 153,   3,  37,  79,  81, 123, 157,   7, 137, 115, 149,  63,
     65, 235,  13, 247, 121, 227,   5,  47,  49,  91, 125, 231, 105,  83, 117,  31,
     33, 203, 237, 215,  89, 195, 229,  15,  17,  59,  93, 199,  73,  51,  85, 255,
]

SENTINEL = b'-violet-'
ALPHA = 47
KEY0 = 0x47


def _encrypt(data: bytes) -> bytes:
    key, out = KEY0, bytearray()
    for b in data:
        out.append((ALPHA + b * INVB[key >> 1]) & 0xFF)
        key = (2 * b + 1) & 0xFF
    return bytes(out)


def _decrypt(data: bytes) -> bytes:
    key, out = KEY0, bytearray()
    for c in data:
        inv = pow(INVB[key >> 1], -1, 256)
        b = ((c - ALPHA) * inv) & 0xFF
        out.append(b)
        key = (2 * b + 1) & 0xFF
    return bytes(out)


def sign(src: str, dst: str) -> None:
    data = open(src, 'rb').read()
    body = _encrypt(data).hex().encode()
    open(dst, 'wb').write(SENTINEL + f'{2 * len(data):08x}'.encode() + body + SENTINEL)
    print(f'signed {src} -> {dst} ({len(data)} bytes)')


def decrypt(src: str, dst: str) -> None:
    raw = open(src, 'rb').read()
    assert raw[:8] == SENTINEL and raw[-8:] == SENTINEL, 'bad sentinels'
    size = int(raw[8:16], 16) // 2
    body = bytes.fromhex(raw[16:16 + 2 * size].decode())
    open(dst, 'wb').write(_decrypt(body))
    print(f'decrypted {src} -> {dst} ({size} bytes)')


USAGE = 'usage: mkfirmware.py sign <in.bin> <out.sim> | decrypt <in.sim> <out.bin>'

if __name__ == '__main__':
    if len(sys.argv) != 4 or sys.argv[1] not in ('sign', 'decrypt'):
        sys.exit(USAGE)
    {'sign': sign, 'decrypt': decrypt}[sys.argv[1]](sys.argv[2], sys.argv[3])
