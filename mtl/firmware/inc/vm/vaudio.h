/**
 * @file vaudio.h
 * @author Sylvain Huet - 2006 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief VLISP Virtual Machine - Audio functions
 */
#ifndef _VAUDIO_
#define _VAUDIO_

#define AUDIO_FIFOPLAY	4096
extern uint8_t audioFifoPlay[AUDIO_FIFOPLAY];

void audioInit();

void audioPlayStart(int32_t freq,int32_t bps,int32_t stereo,int32_t trytofeed);
int32_t audioPlayFeed(uint8_t *src,int32_t len);
void audioPlayStop();
int32_t audioPlayTryFeed(int32_t ask);
int32_t audioPlayFetchByte();
int32_t audioPlayFetch(uint8_t* dst,int32_t ask);

void audioVol(int32_t vol);
int32_t audioPlayTime();

int32_t audioRecStart(int32_t freq,int32_t gain);
int32_t audioRecStop();
int32_t audioRecVol(uint8_t* src,int32_t len,int32_t start);
uint8_t* audioRecFeed_begin(int32_t size);
void audioRecFeed_end();

void adpcmdecode(uint8_t* src,uint8_t *dstc);
void adpcmencode(int16_t* src,uint8_t *dst);

void AudioAdp2wav(uint8_t* dst,int32_t idst,int32_t ldst,uint8_t *src,int32_t isrc,int32_t lsrc,int32_t len);
void AudioWav2adp(uint8_t* dst,int32_t idst,int32_t ldst,uint8_t *src,int32_t isrc,int32_t lsrc,int32_t len);
void AudioWav2alaw(uint8_t* dst,int32_t idst,int32_t ldst,uint8_t *src,int32_t isrc,int32_t lsrc,int32_t len,int32_t mu);
void AudioAlaw2wav(uint8_t* dst,int32_t idst,int32_t ldst,uint8_t *src,int32_t isrc,int32_t lsrc,int32_t len,int32_t mu);

void audioWrite(int32_t reg,int32_t val);
int32_t audioRead(int32_t reg);
int32_t audioFeed(uint8_t *src,int32_t len);
void audioRefresh();
void audioAmpli(int32_t on);

#endif

