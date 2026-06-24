/**
 * @file vnet.h
 * @author Sylvain Huet - 2006 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief VLISP Virtual Machine - Network functions
 */
#ifndef _NET_
#define _NET_

int32_t netState();
int32_t netSend(uint8_t * src,int32_t indexsrc,int32_t lentosend,int32_t lensrc,uint8_t * macdst,int32_t inddst,int32_t lendst,int32_t speed);
int32_t netCb(uint8_t * src,uint32_t lensrc,uint8_t * macsrc);
uint8_t * netMac();
int32_t netChk(uint8_t * src,int32_t indexsrc,int32_t lentosend,int32_t lensrc,uint32_t val);
void netSetmode(int32_t mode,uint8_t * ssid,int32_t chn);
void netScan(uint8_t * ssid);
void netAuth(uint8_t * ssid,uint8_t * mac,uint8_t * bssid,int32_t chn,int32_t rate,int32_t authmode,int32_t encrypt,uint8_t * key);
void netSeqAdd(uint8_t * seq,int32_t n);
void netPmk(uint8_t * ssid,uint8_t * key,uint8_t * buf);
int32_t netRssi();
#endif

