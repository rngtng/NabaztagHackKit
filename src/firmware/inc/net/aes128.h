/**
 * @file aes128.h
 * @author RedoX <dev@redox.ws> - 2017
 * @date 2017/02/02
 * @brief RT2501 Wifi/Network driver
 */
#ifndef _AES128_H
#define _AES128_H_

#include <stdint.h>

struct aes128_context {
	uint8_t state[256];
};

void aes128_init(struct aes128_context *aes, const uint8_t *key, uint16_t length);

void aes128_crypt(struct aes128_context *aes, uint8_t *out,
                  const uint8_t *in, uint16_t length);
void aes128_decrypt(struct aes128_context *aes, uint8_t *out,
                    const uint8_t *in, uint16_t length);

#endif /* _aes128_H_ */
