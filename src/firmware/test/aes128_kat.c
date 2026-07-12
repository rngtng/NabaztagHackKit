/**
 * @file aes128_kat.c
 * @brief Known-answer tests for the AES-128 block cipher and RFC 3394 AES Key
 *        Unwrap in src/net/aes128.c (issue #152 - WPA2/CCMP GTK unwrap).
 *
 * Native host test (not firmware): aes128.c depends only on <string.h> and the
 * aes128.h header, so it links standalone. Run via `task firmware:test:crypto`.
 * Vectors: FIPS-197 Appendix B/C.1 (AES-128) and RFC 3394 section 4.1 (128-bit
 * KEK unwrap of a 128-bit key).
 */
#include <stdio.h>
#include <string.h>

#include "net/aes128.h"

static int check(const uint8_t *got, const uint8_t *exp, int n, const char *name)
{
  int i;
  if(memcmp(got, exp, n) == 0) {
    printf("PASS %s\n", name);
    return 1;
  }
  printf("FAIL %s\n  got: ", name);
  for(i = 0; i < n; i++) printf("%02x", got[i]);
  printf("\n  exp: ");
  for(i = 0; i < n; i++) printf("%02x", exp[i]);
  printf("\n");
  return 0;
}

int main(void)
{
  int ok = 1;

  /* FIPS-197 AES-128 known-answer (key/plaintext/ciphertext). */
  static const uint8_t key[16] = {
    0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};
  static const uint8_t pt[16] = {
    0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
  static const uint8_t ct[16] = {
    0x69,0xc4,0xe0,0xd8,0x6a,0x7b,0x04,0x30,0xd8,0xcd,0xb7,0x80,0x70,0xb4,0xc5,0x5a};
  struct aes128_context aes;
  uint8_t out[16], back[16];

  aes128_init(&aes, key, 16);
  aes128_crypt(&aes, out, pt, 16);
  ok &= check(out, ct, 16, "FIPS-197 AES-128 encrypt");
  aes128_decrypt(&aes, back, ct, 16);
  ok &= check(back, pt, 16, "FIPS-197 AES-128 decrypt");

  /* RFC 3394 section 4.1: unwrap a 128-bit key with a 128-bit KEK. */
  {
    static const uint8_t kek[16] = {
      0x00,0x01,0x02,0x03,0x04,0x05,0x06,0x07,0x08,0x09,0x0a,0x0b,0x0c,0x0d,0x0e,0x0f};
    static const uint8_t wrapped[24] = {
      0x1f,0xa6,0x8b,0x0a,0x81,0x12,0xb4,0x47,0xae,0xf3,0x4b,0xd8,
      0xfb,0x5a,0x7b,0x82,0x9d,0x3e,0x86,0x23,0x71,0xd2,0xcf,0xe5};
    static const uint8_t expkey[16] = {
      0x00,0x11,0x22,0x33,0x44,0x55,0x66,0x77,0x88,0x99,0xaa,0xbb,0xcc,0xdd,0xee,0xff};
    uint8_t uw[16], bad[24];

    if(aes128_unwrap(kek, wrapped, 24, uw)) {
      printf("PASS RFC3394 unwrap integrity\n");
    } else {
      printf("FAIL RFC3394 unwrap integrity\n");
      ok = 0;
    }
    ok &= check(uw, expkey, 16, "RFC3394 unwrap plaintext");

    /* Tampering with the wrapped data must fail the integrity check. */
    memcpy(bad, wrapped, 24);
    bad[0] ^= 0x01;
    if(aes128_unwrap(kek, bad, 24, uw) == 0) {
      printf("PASS RFC3394 rejects tampered input\n");
    } else {
      printf("FAIL RFC3394 rejects tampered input\n");
      ok = 0;
    }
  }

  printf(ok ? "\nALL KAT PASS\n" : "\nKAT FAILURE\n");
  return ok ? 0 : 1;
}
