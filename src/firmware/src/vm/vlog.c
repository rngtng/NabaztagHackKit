/**
 * @file vlog.c
 * @author Sylvain Huet - 2006 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief VLISP Virtual Machine - Logging functions
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ml674061.h"
#include "common.h"

#include "utils/delay.h"

#include "usb/usbctrl.h"
#include "ml60842.h"
#include "usb/hcdmem.h"
#include "usb/hcd.h"
#include "usb/usbh.h"
#include "usb/rt2501usb.h"

#include "utils/debug.h"
#include "irq.h"
#include "hal/i2c.h"
#include "hal/spi.h"
#include "hal/uart.h"

#include "hal/audio.h"
#include "hal/led.h"
#include "utils/mem.h"
#include "hal/motor.h"
#include "hal/rfid.h"
#include "hal/uart.h"

#include "vm/vmem.h"
#include "vm/vloader.h"
#include "vm/vlog.h"

#ifndef HIDE_VM_OUTPUT
  #define consolestr(val)     putst_uart((uint8_t*)val)
  #define consolebin(val,len) putbin_uart(val,len)
  #define consoleint(val)     putint_uart(val)
  #define consolehx(val)      puthx_uart(val)
#else
  #define consolestr(val)     do {} while(0)
  #define consolebin(val,len) do {} while(0)
  #define consoleint(val)     do {} while(0)
  #define consolehx(val)      do {} while(0)
#endif

void logSecho(int32_t p,int32_t nl)
{
	if (p==NIL)
    consolestr("NIL");
	else
    consolebin((uint8_t*)VSTARTBIN(VALTOPNT(p)),VSIZEBIN(VALTOPNT(p)));
	if (nl)
    consolestr(EOL);
}

void logIecho(int32_t i,int32_t nl)
{
	if (i==NIL)
    consolestr("NIL");
	else
    consoleint(VALTOINT(i));
	if (nl)
    consolestr(EOL);
}

extern int32_t _currentop;
void logGC(void)
{
	consolestr("#GC : sp=");consoleint(-_vmem_stack);
	consolestr(" hp=");consoleint(_vmem_heapindex);
	consolestr(" used=");consoleint((_vmem_heapindex-_vmem_stack)*100/VMEM_LENGTH);
	consolestr("%"EOL);
  consolestr(" b:");consolehx((int)_vmem_heap);
  consolestr(" bc:");consolehx((int)_bytecode);
  consolestr(" st:");consolehx(_vmem_start);
  consolestr(" op:");consolehx(_currentop);
	consolestr(EOL);

}


// pour le firmware, le "fichier" ouvert est toujours l'eeprom
int32_t sysLoad(uint8_t *dst,int32_t i,int32_t ldst,uint8_t *filename,int32_t j,int32_t len)
{
  (void)filename;
	if ((j<0)||(i<0)||(len<=0))
    return 0;
	if (i+len>ldst)
    len=ldst-i;
	if (len<=0)
    return 0;
	if (j+len>4096)
    len=4096-j;
	if (len<=0)
    return 0;
  read_uc_flash(j,(uint8_t*)dst,len);
  return len;
}

static uint8_t  buffer_temp[4096];

// pour le firmware, le "fichier" ouvert est toujours l'eeprom
int32_t sysSave(uint8_t *dst,int32_t i,int32_t ldst,uint8_t *filename,int32_t j,int32_t len)
{
  (void)filename;
	if ((j<0)||(i<0)||(len<=0))
    return 0;
	if (i+len>ldst)
    len=ldst-i;
	if (len<=0)
    return 0;
	if (j+len>4096)
    len=4096-j;
	if (len<=0)
    return 0;
  __disable_interrupt();
  write_uc_flash(j,(uint8_t*)dst,len,buffer_temp);
  __enable_interrupt();
  return len;
}

int32_t sysTimems()
{
  return counter_timer;
}

int32_t sysTime()
{
	return counter_timer_s;
}

int32_t rndval;

// retourne une valeur aléatoire entre 0 et 65535
int32_t sysRand()
{
	rndval=rndval*0x1234567+11;
	return (rndval>>8)&0xffff;
}

void sysSrand(int32_t seed)
{
	rndval=seed;
}


void sysCpy(uint8_t *dst,int32_t i,int32_t ldst,uint8_t *src,int32_t j,int32_t lsrc,int32_t len)
{
	if ((i<0)||(j<0)||(len<=0))
    return;
  if (i+len>ldst)
    len=ldst-i;
  if (len<=0)
    return;
	if (j+len>lsrc) len=lsrc-j;
    if (len<=0)
      return;
	dst+=i;
	src+=j;
	while((len--)>0)
    *(dst++)=*(src++);
}

int32_t sysCmp(uint8_t *dst,int32_t i,int32_t ldst,uint8_t *src,int32_t j,int32_t lsrc,int32_t len)
{
	if ((i<0)||(j<0)||(len<=0))
    return 0;
	if ((i+len>ldst)&&(j+len>lsrc))
  {
    if (ldst-i>lsrc-j)
      len=ldst-i;
    else
      len=lsrc-j;
  }
	dst+=i;
	src+=j;
	while((len--)>0)
    if (((uint8_t)*dst)>((uint8_t)*src))
      return 1;
    else if (((uint8_t)*(dst++))<((uint8_t)*(src++)))
      return -1;
	return 0;
}

int32_t mystrcmp(uint8_t *dst,uint8_t *src,int32_t len)
{
	while((len--)>0)
    if ((*(dst++))!=(*(src++)))
      return 1;
	return 0;
}

void mystrcpy(uint8_t *dst,uint8_t *src,int32_t len)
{
	while((len--)>0)
    *(dst++)=*(src++);
}

int32_t sysFind(uint8_t *dst,int32_t i,int32_t ldst,uint8_t *src,int32_t j,int32_t lsrc,int32_t len)
{
	if ((j<0)||(j+len>lsrc))
    return NIL;
	src+=j;

	if (i<0) i=0;
	while(i+len<=ldst)
	{
		if (!mystrcmp(dst+i,src,len))
      return INTTOVAL(i);
		i++;
	}
	return NIL;
}

int32_t sysFindrev(uint8_t *dst,int32_t i,int32_t ldst,uint8_t *src,int32_t j,int32_t lsrc,int32_t len)
{
	if ((j<0)||(j+len>lsrc))
    return NIL;
	src+=j;
	if(i+len>ldst)
    i=ldst-len;
	while(i>=0)
	{
		if (!mystrcmp(dst+i,src,len))
      return INTTOVAL(i);
		i--;
	}
	return NIL;
}

int32_t sysStrgetword(uint8_t *src,int32_t len,int32_t ind)
{
  if ((ind<0)||(ind+2>len))
    return -1;
  return (int32_t)(src[ind]<<8)+src[ind+1];
}

void sysStrputword(uint8_t *src,int32_t len,int32_t ind,int32_t val)
{
  if ((ind<0)||(ind+2>len))
    return;
  src[ind+1]=val&0xFF;
  src[ind]=(val>>8)&0xFF;
}

// lecture d'une chaîne décimale (s'arrête au premier caractère incorrect)
int32_t sysAtoi(uint8_t* src)
{
  int32_t x,c,s;
  x=s=0;
  if ((*src)=='-') { s=1; src++; }
  while((c=*src++))
  {
    if ((c>='0')&&(c<='9'))
      x=(x*10)+c-'0';
    else
      return (s?(-x):x);
  }
  return (s?(-x):x);
}

// lecture d'une chaîne hexadécimale (s'arrête au premier caractère incorrect)
int32_t sysHtoi(uint8_t* src)
{
	int32_t x;
  uint8_t c;
	x=0;
	while((c=*src++))
	{
		if ((c>='0')&&(c<='9'))
      x=(x<<4)+c-'0';
		else if ((c>='A')&&(c<='F'))
      x=(x<<4)+c-'A'+10;
		else if ((c>='a')&&(c<='f'))
      x=(x<<4)+c-'a'+10;
		else
      return x;
	}
	return x;
}

void sysCtoa(int32_t c)
{
  uint8_t res[1];
  res[0]=c;
  VPUSH(PNTTOVAL(VMALLOCSTR(res,1)));
}

const int32_t itoarsc[10]={
  1000000000,100000000,10000000,
  1000000   ,100000   ,10000,
  1000      ,100      ,10,
  1
};

void sysItoa(int32_t v)
{
  uint8_t res[16];
  int32_t ires=0;
  if (v==0)
  {
    res[ires++]='0';
  }
  else
  {
    int32_t start=1;
    int32_t imul=0;
    if (v<0)
    {
      v=-v;
      res[ires++]='-';
    }
    while(imul<10)
    {
      int32_t k=0;
      while(v>=itoarsc[imul])
      {
        k++;
        v-=itoarsc[imul];
      }
      if ((k)||(!start))
      {
        start=0;
        res[ires++]='0'+k;
      }
      imul++;
    }
  }

  VPUSH(PNTTOVAL(VMALLOCSTR(res,ires)));

}

void sysItoh(int32_t v)
{
  uint8_t res[16];
  int32_t ires=0;
  if (v==0)
  {
    res[ires++]='0';
  }
  else
  {
    int32_t start=1;
    int32_t imul=28;
    while(imul>=0)
    {
      int32_t c=(v>>imul)&15;
      if ((c)||(!start))
      {
        start=0;
        res[ires++]=(c<10)?'0'+c:'a'+c-10;
      }
      imul-=4;
    }
  }
  VPUSH(PNTTOVAL(VMALLOCSTR(res,ires)));
}

void sysCtoh(int32_t c)
{
  uint8_t res[2];
  int32_t v=(c>>4)&15;
  res[0]=(v<10)?'0'+v:'a'+v-10;
  v=c&15;
  res[1]=(v<10)?'0'+v:'a'+v-10;
  VPUSH(PNTTOVAL(VMALLOCSTR(res,2)));
}

void sysItobin2(int32_t c)
{
  uint8_t res[2];
  res[1]=c;
  c>>=8;
  res[0]=c;
  VPUSH(PNTTOVAL(VMALLOCSTR(res,2)));
}

int32_t sysListswitch(int32_t p,int32_t key)
{
  while(p!=NIL)
  {
    int32_t q=VALTOPNT(VFETCH(p,0));
    if ((q!=NIL)&&(VFETCH(q,0)==key)) return VFETCH(q,1);
    p=VALTOPNT(VFETCH(p,1));
  }
  return NIL;
}

int32_t sysListswitchstr(int32_t p,uint8_t* key)
{
  while(p!=NIL)
  {
    int32_t q=VALTOPNT(VFETCH(p,0));
    if (q!=NIL)
    {
      int32_t r=VALTOPNT(VFETCH(q,0));
      if ((r!=NIL)&&(!strcmp((char*)VSTARTBIN(r),(char*)key))) return VFETCH(q,1);
    }
    p=VALTOPNT(VFETCH(p,1));
  }
  return NIL;
}

int32_t get_motor_val(int32_t i);
int32_t getButton();
int32_t get_button3();
uint8_t* get_rfid();
int32_t check_rfid_n();
uint8_t* get_nth_rfid(int32_t i);

void sysLed(int32_t led,int32_t col)
{
  set_led(led,col);
}

void sysMotorset(int32_t motor,int32_t sens)
{
  motor=1+(motor&1);
  if (sens==0)
    stop_motor(motor);
  else
    run_motor(motor,255,(sens>0)?REVERSE:FORWARD);
}

int32_t sysMotorget(int32_t motor)
{
  return get_motor_position(1+(motor&1));
}

extern uint8_t push_button_value(void);
int32_t sysButton2()
{
  return push_button_value();
}

int32_t sysButton3()
{
	return 255-get_adc_value();
}

uint8_t* sysRfidget()
{
  return get_rfid_first_device();
}

int32_t check_rfid_n();
uint8_t* get_nth_rfid(int32_t i);
int32_t rfid_read(uint8_t* id,int32_t bloc,uint8_t* data);
int32_t rfid_write(uint8_t* id,int32_t bloc,uint8_t* data);

void sysRfidgetList()
{
  int32_t n=0;
  n=check_rfid_n();
  if (n<=0)
  {
    VPUSH(NIL);
    return;
  }
  int32_t i;
  for(i=0;i<n;i++)
  {
    VPUSH(PNTTOVAL(VMALLOCSTR(get_nth_rfid(i),8)));
  }
  VPUSH(NIL);
  while(n--)
    VMKTAB(2);
}

void sysRfidread(uint8_t* id,int32_t bloc)
{
  uint8_t buf[4];
  int32_t k=rfid_read(id,bloc,buf);
  if (k)
  {
    VPUSH(NIL);
    return;
  }
  VPUSH(PNTTOVAL(VMALLOCSTR(buf,4)));
}

int32_t sysRfidwrite(uint8_t* id,int32_t bloc,uint8_t* data)
{
  return rfid_write(id,bloc,data);
}

void sysReboot()
{
  reset_uc();
}

void sysFlash(uint8_t* firmware,int32_t len)
{
  __disable_interrupt();
  flash_uc((uint8_t*)firmware,len,buffer_temp);
}

const uint8_t inv8[128]=
{
    1,171,205,183, 57,163,197,239,241, 27, 61,167, 41, 19, 53,223,
  225,139,173,151, 25,131,165,207,209,251, 29,135,  9,243, 21,191,
  193,107,141,119,249, 99,133,175,177,219,253,103,233,211,245,159,
  161, 75,109, 87,217, 67,101,143,145,187,221, 71,201,179,213,127,
  129, 43, 77, 55,185, 35, 69,111,113,155,189, 39,169,147,181, 95,
   97, 11, 45, 23,153,  3, 37, 79, 81,123,157,  7,137,115,149, 63,
   65,235, 13,247,121,227,  5, 47, 49, 91,125,231,105, 83,117, 31,
   33,203,237,215, 89,195,229, 15, 17, 59, 93,199, 73, 51, 85,255
};

int32_t decode8(uint8_t* src,int32_t len,uint8_t  key,uint8_t  alpha)
{
	while(len--)
	{
		uint8_t  v=((*src)-alpha)*key;
		*(src++)=v;
		key=v+v+1;
	}
	return key;
}

int32_t encode8(uint8_t* src,int32_t len,uint8_t  key,uint8_t  alpha)
{
	while(len--)
	{
		uint8_t  v=*src;
		*(src++)=alpha+(v*inv8[key>>1]);
		key=v+v+1;
	}
	return key;
}

int32_t sysCrypt(uint8_t* src,int32_t indexsrc,int32_t len,int32_t lensrc,uint32_t key,int32_t alpha)
{
  if ((indexsrc<0)||(indexsrc+len>lensrc)||(len<=0)) return -1;
  return encode8(src+indexsrc,len,key,alpha);
}
int32_t sysUncrypt(uint8_t* src,int32_t indexsrc,int32_t len,int32_t lensrc,uint32_t key,int32_t alpha)
{
  if ((indexsrc<0)||(indexsrc+len>lensrc)||(len<=0)) return -1;
  return decode8(src+indexsrc,len,key,alpha);
}

// Fonctions pour les périphérique I2C
// Arg 1 : Adresse du périphérique
// Arg 2 : Taille du buffer qu'on souhaite récupérer
int32_t sysI2cRead(uint8_t addr_i2c, int32_t bufsize)
{
  uint8_t* data=NULL;
  int32_t nmax=1000;
  // Tente la lecture jusqu'à ce qu'elle soit faite.
  while((nmax>0)&&(read_i2c(addr_i2c,data,bufsize)==FALSE)){ nmax--;__no_operation(); }
  // Retour pour le ByteCode
  VPUSH(PNTTOVAL(VMALLOCSTR((uint8_t*)data,bufsize)));
  return nmax;
}

// Fonction pour l'écriture sur le périphérique
// Arg1 : Adresse du périphérique
// Arg2 : Contenu à écrire
// Arg3 : Taille du contenu à écrire
int32_t sysI2cWrite(uint8_t addr_i2c, uint8_t *data, uint32_t bufsize)
{
  int32_t nmax=1000;
  // Tente l'écriture jusqu'à sa réussite
  while((nmax>0)&&(write_i2c(addr_i2c,data,bufsize)==FALSE)){ nmax--;__no_operation(); }
  return nmax;
}
