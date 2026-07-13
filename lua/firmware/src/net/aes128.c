/**
 * @file aes128.c
 * @author RedoX <dev@redox.ws> - 2017
 * @date 2017/02/02
 * @brief RT2501 Wifi/Network driver
 */
#include "net/aes128.h"

void aes128_init(struct aes128_context *aes, const uint8_t *key, uint16_t length)
{
  (void)aes;
  (void)key;
  (void)length;
}

void aes128_crypt(struct aes128_context *aes, uint8_t *out,
    const uint8_t *in, uint16_t length)
{
  (void)aes;
  (void)out;
  (void)in;
  (void)length;
}

void aes128_decrypt(struct aes128_context *aes, uint8_t *out,
    const uint8_t *in, uint16_t length)
{
  (void)aes;
  (void)out;
  (void)in;
  (void)length;
}

