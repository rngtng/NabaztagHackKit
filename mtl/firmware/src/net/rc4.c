/**
 * @file rc4.c
 * @author Sebastien Bourdeauducq - 2006 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief RT2501 Wifi/Network driver
 */
#include "net/rc4.h"

void rc4_init(struct rc4_context *rc4, const unsigned char *key, unsigned int length)
{
	unsigned char t, u;
	unsigned int keyindex;
	unsigned int stateindex;
	unsigned char *state;
	unsigned int counter;
    
	state = rc4->state;
	rc4->x = 0;
	rc4->y = 0;
	for(counter=0;counter<256;counter++)
		state[counter] = (unsigned char)counter;
	keyindex = 0;
	stateindex = 0;
	for(counter=0;counter<256;counter++) {
		t = state[counter];
		stateindex = (stateindex + key[keyindex] + t) & 0xff;
		u = state[stateindex];
		state[stateindex] = t;
		state[counter] = u;
		if(++keyindex >= length) keyindex = 0;
	}
}

unsigned char rc4_byte(struct rc4_context *rc4)
{
	unsigned int x;
	unsigned int y;
	unsigned char sx, sy;
	unsigned char *state;
  
	state = rc4->state;
	x = (rc4->x + 1) & 0xff;
	sx = state[x];
	y = (sx + rc4->y) & 0xff;
	sy = state[y];
	rc4->x = x;
	rc4->y = y;
	state[y] = sx;
	state[x] = sy;

	return(state[(sx + sy) & 0xff]);
}

void rc4_cipher(struct rc4_context *rc4, unsigned char *out, 
		const unsigned char *in, unsigned int length)
{
	unsigned int i;
	
	for(i=0;i<length;i++)
		out[i] = in[i] ^ rc4_byte(rc4);
}

