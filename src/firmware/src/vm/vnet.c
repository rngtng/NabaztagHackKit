/**
 * @file vnet.c
 * @author Sylvain Huet - 2006 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief VLISP Virtual Machine - Network functions
 */
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "usb/usbh.h"
#include "usb/rt2501usb.h"

#include "vm/vinterp.h"
#include "vm/vlog.h"
#include "vm/vloader.h"
#include "vm/vmem.h"
#include "vm/vnet.h"

/**
 * @brief Get network's current state
 *
 * @return Network state
 */
int32_t netState()
{
  return rt2501_state();
}

int32_t netSend(uint8_t *src,int32_t indexsrc,int32_t lentosend,int32_t lensrc,uint8_t *macdst,int32_t inddst,int32_t lendst,int32_t speed)
{
  if (indexsrc<0) return -1;
  if (indexsrc+lentosend>lensrc) lentosend=lensrc-indexsrc;
  if (lentosend<=0) return -1;
  if (inddst<0) return -1;
  if (inddst+6>lendst) return -1;
  return rt2501_send((src+indexsrc),lentosend,(macdst+inddst),speed,1);

}

int32_t netCb(uint8_t *src,uint32_t lensrc,uint8_t *macsrc)
{
  VPUSH(PNTTOVAL(VMALLOCSTR(src,lensrc)));
  VPUSH(PNTTOVAL(VMALLOCSTR(macsrc,6)));
  VPUSH(VCALLSTACKGET(_sys_start,SYS_CBTCP));
  if (VSTACKGET(0)!=NIL)
    interpGo();
  else {
    (void)VPULL();
    (void)VPULL();
  }
  (void)VPULL();
  return 0;
}

extern uint8_t rt2501_mac[6];

uint8_t *netMac()
{
  return rt2501_mac;
}

int32_t netChk(uint8_t *src,int32_t indexsrc,int32_t lentosend,int32_t lensrc,uint32_t val)
{
  uint16_t* p;

  if (indexsrc<0) return val;
  if (indexsrc+lentosend>lensrc) lentosend=lensrc-indexsrc;
  if (lentosend<=0) return val;

  src+=indexsrc;
  p=(uint16_t*)src;

  val=((val<<8)&0xff00)+((val>>8)&0xff);
  while(lentosend>1)
  {
	  val+=*(p++);
	  lentosend-=2;
  }
  if (lentosend) val+=*(uint8_t*)p;

  val=(val>>16)+(val&0xffff);
  val=(val>>16)+(val&0xffff);
  val=((val<<8)&0xff00)+((val>>8)&0xff);
  return val;
}

void netSetmode(int32_t mode,uint8_t *ssid,int32_t chn)
{
  rt2501_setmode(mode,ssid,chn);
}


int32_t nscan;

void netScan_(struct rt2501_scan_result *scan_result, void *userparam)
{
  (void)userparam; // Not implemented, function is called with NULL
#ifdef DEBUG_VM
  sprintf(dbg_buffer, ">>> %s %d %d %d %d"EOL,scan_result->ssid,scan_result->rssi,scan_result->channel,scan_result->rateset,scan_result->encryption);
  DBG_VM(dbg_buffer);
#endif
  VPUSH(PNTTOVAL(VMALLOCSTR(scan_result->ssid,strlen((char*)scan_result->ssid))));
  VPUSH(PNTTOVAL(VMALLOCSTR((uint8_t*)scan_result->mac,6)));
  VPUSH(PNTTOVAL(VMALLOCSTR((uint8_t*)scan_result->bssid,6)));
  VPUSH(INTTOVAL(scan_result->rssi));
  VPUSH(INTTOVAL(scan_result->channel));
  VPUSH(INTTOVAL(scan_result->rateset));
  VPUSH(INTTOVAL(scan_result->encryption));
  VMKTAB(7);
  nscan++;
}


void netScan(uint8_t *ssid)
{
  nscan=0;
  rt2501_scan(ssid, netScan_, NULL);
  VPUSH(NIL);
  while(nscan--) VMKTAB(2);
}

void netAuth(uint8_t *ssid,uint8_t *mac,uint8_t *bssid,int32_t chn,int32_t rate,int32_t authmode,int32_t encrypt,uint8_t *key)
{
  rt2501_auth(ssid,mac,bssid,chn,rate,authmode,encrypt,key);
}

void netSeqAdd(uint8_t *seq,int32_t n)
{
  uint8_t res[4];
  uint32_t val;
  val=(seq[0]<<24)+(seq[1]<<16)+(seq[2]<<8)+seq[3];
  val+=n;
  res[3]=val; val>>=8;
  res[2]=val; val>>=8;
  res[1]=val; val>>=8;
  res[0]=val;
  VPUSH(PNTTOVAL(VMALLOCSTR(res,4)));
}

void mypassword_to_pmk(const uint8_t *password, uint8_t *ssid, int32_t ssidlength, uint8_t *pmk);

void netPmk(uint8_t *ssid,uint8_t *key,uint8_t *buf)
{
  mypassword_to_pmk(key,ssid,strlen((char*)ssid),buf);
}

int32_t netRssi()
{
	return rt2501_rssi_average();
}

