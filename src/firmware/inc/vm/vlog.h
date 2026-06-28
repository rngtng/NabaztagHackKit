/**
 * @file vlog.h
 * @author Sylvain Huet - 2006 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief VLISP Virtual Machine - Logging functions
 */
#ifndef _LOGGER_
#define _LOGGER_

#include "hal/uart.h"
#define consolestr(val) putst_uart((uint8_t*)val)
#define consolebin(val,len) putbin_uart(val,len)
#define consoleint(val) putint_uart(val)
#define consolehx(val) puthx_uart(val)

#define EOL "\n\r"

void logSecho(int32_t p,int32_t nl);
void logIecho(int32_t i,int32_t nl);
void logGC();

void dump(uint8_t *src,int32_t len);

int32_t sysLoad(uint8_t *dst,int32_t i,int32_t ldst,uint8_t *filename,int32_t j,int32_t len);
int32_t sysSave(uint8_t *dst,int32_t i,int32_t ldst,uint8_t *filename,int32_t j,int32_t len);

int32_t sysTimems();
int32_t sysTime();
int32_t sysRand();
void sysSrand(int32_t seed);
void sysReboot();
void sysFlash(uint8_t *firmware,int32_t len);

void mystrcpy(uint8_t *dst,uint8_t *src,int32_t len);

void sysCpy(uint8_t *dst,int32_t i,int32_t ldst,uint8_t *src,int32_t j,int32_t lsrc,int32_t len);
int32_t sysCmp(uint8_t *dst,int32_t i,int32_t ldst,uint8_t *src,int32_t j,int32_t lsrc,int32_t len);
int32_t sysFind(uint8_t *dst,int32_t i,int32_t ldst,uint8_t *src,int32_t j,int32_t lsrc,int32_t len);
int32_t sysFindrev(uint8_t *dst,int32_t i,int32_t ldst,uint8_t *src,int32_t j,int32_t lsrc,int32_t len);
int32_t sysStrgetword(uint8_t *src,int32_t len,int32_t ind);
void sysStrputword(uint8_t *src,int32_t len,int32_t ind,int32_t val);
int32_t sysAtoi(uint8_t *src);
int32_t sysHtoi(uint8_t *src);
void sysCtoa(int32_t c);
void sysItoa(int32_t v);
void sysItoh(int32_t v);
void sysCtoh(int32_t c);
void sysItobin2(int32_t c);
int32_t sysListswitch(int32_t p,int32_t key);
int32_t sysListswitchstr(int32_t p,uint8_t *key);


void sysLed(int32_t led,int32_t col);
void sysMotorset(int32_t motor,int32_t sens);
int32_t sysMotorget(int32_t motor);
int32_t sysButton2();
int32_t sysButton3();

uint8_t *sysRfidget();
void sysRfidgetList();
void sysRfidread(uint8_t *id,int32_t bloc);
int32_t sysRfidwrite(uint8_t *id,int32_t bloc,uint8_t *data);

int32_t sysCrypt(uint8_t *src,int32_t indexsrc,int32_t len,int32_t lensrc,uint32_t key,int32_t alpha);
int32_t sysUncrypt(uint8_t *src,int32_t indexsrc,int32_t len,int32_t lensrc,uint32_t key,int32_t alpha);

// Prototype des fonction pour l'I2C
int32_t sysI2cRead(uint8_t addr_i2c, int32_t bufsize);
int32_t sysI2cWrite(uint8_t addr_i2c, uint8_t *data, uint32_t bufsize);


#endif

