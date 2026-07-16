/**
 * @file hash.c
 * @author Sebastien Bourdeauducq - 2006 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief SHA1 hash utils (MD5 dropped with WEP/WPA1/TKIP support, #124)
 */
#include <string.h>
#include <stdint.h>

#include "net/hash.h"

/* ===== begin - public domain SHA1 implementation ===== */

struct SHA_CTX {
	uint32_t H[5];
	uint32_t W[80];
	uint16_t lenW;
	uint32_t sizeHi, sizeLo;
};

/**
 * @brief Initialize the SHA1 context
 *
 * @param [in]  ctx   Context pointer
 */
static void SHAInit(struct SHA_CTX *ctx)
{
	uint16_t i;

	ctx->lenW = 0;
	ctx->sizeHi = ctx->sizeLo = 0;

	/* Initialize H with the magic constants (see FIPS180 for constants) */
	ctx->H[0] = 0x67452301L;
	ctx->H[1] = 0xefcdab89L;
	ctx->H[2] = 0x98badcfeL;
	ctx->H[3] = 0x10325476L;
	ctx->H[4] = 0xc3d2e1f0L;

	for (i = 0; i < 80; i++)
		ctx->W[i] = 0;
}

#define SHA_ROTL(X,n) ((((X) << (n)) | ((X) >> (32-(n)))) & 0xffffffffL)

/**
 * @brief FIXME
 *
 * @param [in]  ctx Context pointer
 */
static void SHAHashBlock(struct SHA_CTX *ctx)
{
	uint16_t t;
	uint32_t A,B,C,D,E,TEMP;

	for (t = 16; t <= 79; t++)
		ctx->W[t] = SHA_ROTL(ctx->W[t-3]^ctx->W[t-8]^ctx->W[t-14]^ctx->W[t-16],1);

	A = ctx->H[0];
	B = ctx->H[1];
	C = ctx->H[2];
	D = ctx->H[3];
	E = ctx->H[4];

	for (t = 0; t <= 19; t++) {
		TEMP = (SHA_ROTL(A,5) + (((C^D)&B)^D)     + E + ctx->W[t] + 0x5a827999L);
		E = D; D = C; C = SHA_ROTL(B, 30); B = A; A = TEMP;
	}
	for (t = 20; t <= 39; t++) {
		TEMP = (SHA_ROTL(A,5) + (B^C^D)           + E + ctx->W[t] + 0x6ed9eba1L);
		E = D; D = C; C = SHA_ROTL(B, 30); B = A; A = TEMP;
	}
	for (t = 40; t <= 59; t++) {
		TEMP = (SHA_ROTL(A,5) + ((B&C)|(D&(B|C))) + E + ctx->W[t] + 0x8f1bbcdcL);
		E = D; D = C; C = SHA_ROTL(B, 30); B = A; A = TEMP;
	}
	for (t = 60; t <= 79; t++) {
		TEMP = (SHA_ROTL(A,5) + (B^C^D)           + E + ctx->W[t] + 0xca62c1d6L);
		E = D; D = C; C = SHA_ROTL(B, 30); B = A; A = TEMP;
	}

	ctx->H[0] += A;
	ctx->H[1] += B;
	ctx->H[2] += C;
	ctx->H[3] += D;
	ctx->H[4] += E;
}

/**
 * @brief Update the Context with new data
 *
 * @param [in]  ctx     Context pointer
 * @param [in]  dataIn  Data buffer
 * @param [in]  len     Data length
 */
static void SHAUpdate(struct SHA_CTX *ctx, const uint8_t *dataIn, uint16_t len)
{
	uint16_t i;

	/* Read the data into W and process blocks as they get full */
	for(i = 0; i < len; i++) {
		ctx->W[ctx->lenW / 4] <<= 8;
		ctx->W[ctx->lenW / 4] |= (uint32_t)dataIn[i];
		if ((++ctx->lenW) % 64 == 0) {
			SHAHashBlock(ctx);
			ctx->lenW = 0;
		}
		ctx->sizeLo += 8;
		ctx->sizeHi += (ctx->sizeLo < 8);
	}
}

/**
 * @brief Compute the SHA1 sum
 *
 * @param [in]  ctx     Context
 * @param [out] hashout SHA1 hash buffer
 */
static void SHAFinal(struct SHA_CTX *ctx, uint8_t hashout[20])
{
	uint8_t pad0x80 = 0x80;
	uint8_t pad0x00 = 0x00;
	uint8_t padlen[8];
	uint16_t i;

	/* Pad with a binary 1 (e.g. 0x80), then zeroes, then length */
	padlen[0] = (uint8_t)((ctx->sizeHi >> 24) & 255);
	padlen[1] = (uint8_t)((ctx->sizeHi >> 16) & 255);
	padlen[2] = (uint8_t)((ctx->sizeHi >> 8) & 255);
	padlen[3] = (uint8_t)((ctx->sizeHi >> 0) & 255);
	padlen[4] = (uint8_t)((ctx->sizeLo >> 24) & 255);
	padlen[5] = (uint8_t)((ctx->sizeLo >> 16) & 255);
	padlen[6] = (uint8_t)((ctx->sizeLo >> 8) & 255);
	padlen[7] = (uint8_t)((ctx->sizeLo >> 0) & 255);
	SHAUpdate(ctx, &pad0x80, 1);
	while (ctx->lenW != 56)
		SHAUpdate(ctx, &pad0x00, 1);
	SHAUpdate(ctx, padlen, 8);

	/* Output hash */
	for (i=0;i<20;i++) {
		hashout[i] = (uint8_t)(ctx->H[i / 4] >> 24);
		ctx->H[i / 4] <<= 8;
	}
}

/* ===== end - public domain SHA1 implementation ===== */

/**
 * @brief FIXME
 *
 * @param [in]  key       FIXME
 * @param [in]  key_len   Key length
 * @param [in]  data      Data buffer
 * @param [in]  data_len  Data Length
 * @param [out] max       FIXME
 */
void hmac_sha1(const uint8_t *key, uint32_t key_len,
               const uint8_t *data, uint32_t data_len,
               uint8_t *mac)
{
	struct SHA_CTX context;
	uint8_t k_ipad[64]; /* inner padding - key XORd with ipad */
	uint8_t k_opad[64]; /* outer padding - key XORd with opad */
	uint8_t tk[20];
	int i;

	if(key_len > 64) {
		SHAInit(&context);
		SHAUpdate(&context, key, key_len);
		SHAFinal(&context, tk);

		key = tk;
		key_len = 20;
	}

	memset(k_ipad, 0, sizeof(k_ipad));
	memset(k_opad, 0, sizeof(k_opad));
	memcpy(k_ipad, key, key_len);
	memcpy(k_opad, key, key_len);

	/* XOR key with ipad and opad values */
	for(i=0;i<64;i++) {
		k_ipad[i] ^= 0x36;
		k_opad[i] ^= 0x5c;
	}

	/* perform inner SHA1 */
	SHAInit(&context);			/* init context for 1st pass */
	SHAUpdate(&context, k_ipad, 64);	/* start with inner pad */
	SHAUpdate(&context, data, data_len);	/* then text of datagram */
	SHAFinal(&context, mac);		/* finish up 1st pass */

	/* perform outer SHA1 */
	SHAInit(&context);			/* init context for 2nd pass */
	SHAUpdate(&context, k_opad, 64);	/* start with outer pad */
	SHAUpdate(&context, mac, 20);		/* then results of 1st hash */
	SHAFinal(&context, mac);		/* finish up 2nd pass */
}
