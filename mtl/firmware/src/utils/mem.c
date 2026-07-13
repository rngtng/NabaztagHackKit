/**
 * @file mem.c
 * @author Oki Electric Industry Co., LTD. - 2005 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief Memory Flash related functions
 */
#include "ml674061.h"
#include "common.h"

#include "utils/mem.h"

#include "usb/hcdmem.h"

#include "vm/vlog.h"

/*******************************************************************************/
/*  Init the internal flash memory                                             */
/*  Function : init_uc_flash                                                   */
/*      Parameters                                                             */
/*          Input   :   Nothing                                                */
/*          Output  :   Nothing                                                */
/*******************************************************************************/
__attribute__ ((section(".ramfunc")))
void init_uc_flash(void)
{
  //Flash ROM programming is prohibited
  clr_bit(FLACON,0x01);
  //Dummy instructions
  __no_operation();
  __no_operation();
  __no_operation();
}

__attribute__ ((section(".ramfunc")))
void write_uc_flash_sec(uint32_t address, uint8_t *data,
                        uint32_t nb_byte, uint8_t *temp)
{

  uint16_t cmpt_int=0x42;
  //~ uint8_t write_ok;

  //Stop watchdog clock
  set_bit(TBGCON,0x80);
  uint8_t *read_char;

  read_char=(uint8_t*)(address);

  if(data && nb_byte)
  {
    //----------------------------------
    // Save data present on flash sector
    //----------------------------------
    for(cmpt_int=0; cmpt_int<4096; cmpt_int++)
      temp[cmpt_int]=read_char[cmpt_int];

    //----------------------------------
    // Update data into temporary buffer
    //----------------------------------
    for(cmpt_int=0; cmpt_int<nb_byte; cmpt_int++)
      temp[cmpt_int]=data[cmpt_int];
  }
  //~ dump(temp,0xe0);
  //--------------
  // Erase sector
  //--------------
  //Flash ROM programming is permitted
  put_value(FLACON,0x03);
  __no_operation();
  __no_operation();
  __no_operation();

  //SPD Command to erase sector 31 on ML67Q4051 ( 4kbyte from 0x1F000 to 0x1FFFF )
  put_value(0x08000000+0x15554,0xAA);
  put_value(0x08000000+0x0AAA8,0x55);
  put_value(0x08000000+0x15554,0x80);
  put_value(0x08000000+0x15554,0xAA);
  put_value(0x08000000+0x0AAA8,0x55);
  if(address < 0x08000000)
    address += 0x08000000;
  put_value(address,0x30);
  __no_operation();
  __no_operation();
  __no_operation();
  //Wait for the completion of the command
  while((get_value(FLACON) & (0x04|0x8|0x2))!= 0x2);
//-----------------------------
// Loop to write 1024 x 4 bytes
//-----------------------------

  for(cmpt_int=0; cmpt_int<4096; cmpt_int+=4)
  {
    if(temp[cmpt_int] == 0xFF && temp[cmpt_int+1] == 0xFF &&
       temp[cmpt_int+2] == 0xFF && temp[cmpt_int+3] == 0xFF
    ) continue;
    //~ Reset the status flag of the completed command in SPD format
    put_value(FLACON,0x03);
    __no_operation();
    __no_operation();
    __no_operation();

    //SPD Command to write 4 bytes
    put_value(0x08000000+0x15554,0xAA);
    put_value(0x08000000+0x0AAA8,0x55);
    put_value(0x08000000+0x15554,0xA0);
    put_value(address+cmpt_int,temp[cmpt_int]);
    put_value(address+1+cmpt_int,temp[cmpt_int+1]);
    put_value(address+2+cmpt_int,temp[cmpt_int+2]);
    put_value(address+3+cmpt_int,temp[cmpt_int+3]);
    __no_operation();
    __no_operation();
    __no_operation();
	  //Wait for the completion of the command
	  while((get_value(FLACON) & (0x04|0x8|0x2))!= 0x2);
  }

  //Reset the status flag of the completed command in SPD format
    put_value(FLACON,0x02);
    __no_operation();
    __no_operation();
    __no_operation();
    __no_operation();
  //Start watchdog clock
  clr_bit(TBGCON,0x80);
}

/*******************************************************************************/
/*  Write non volatile parameters in the last sector of internal uc flash      */
/*  memory, 4096 bytes in the last sector from 0x1F000 to 0x1FFFF              */
/*  63ms duration and we must disable interrupts...                            */
/*  Function : write_uc_flash                                                  */
/*      Parameters                                                             */
/*          Input   :   index address from 0 to 4096 where we want to write    */
/*          Input   :   pointer to the buffer to write                         */
/*          Input   :   number of bytes to write                               */
/*          Input   :   pointer to a temporary buffer of 4096bytes needed for  */
/*                      writing                                                */
/*          Output  :   Nothing                                                */
/*******************************************************************************/
__attribute__ ((section(".ramfunc"))) void write_uc_flash(uint32_t address, uint8_t *data, uint32_t nb_byte, uint8_t *temp)
{
  uint16_t cmpt_int;

  //----------------------------------
  // Save data present on flash sector
  //----------------------------------
  read_uc_flash(0,temp,4096);

  //----------------------------------
  // Update data into temporary buffer
  //----------------------------------
  for(cmpt_int=0; cmpt_int<nb_byte; cmpt_int++)
    temp[address+cmpt_int]=data[cmpt_int];

  write_uc_flash_sec(0x1F000,NULL,0,temp);
}

__attribute__ ((section(".ramfunc"))) void flash_uc(uint8_t *data, int32_t nb_byte, uint8_t *temp)
{
  int32_t address=0;
  while(nb_byte>0)
  {
    write_uc_flash_sec(address,data,(nb_byte<4096)?nb_byte:4096,temp);
    nb_byte-=4096;
    data+=4096;
    address+=4096;
  }
  put_value(INTST,0x3c);  //release write protection
  put_value(INTST,0x00);  //Clear status bit

  put_value(TBGCON,0x5A);  //release write protection
  put_value(TBGCON,0x40);  //Watchdog clock = APB_CLK/32 => 1us @32MHz => overflow each 65536us
                           //generates reset when overflow ; Turn ON counter

  put_value(OVFAST,0x63);  //assert time to be ouput from the RSTOUT_N pin of 100us

  put_value(WDTCON,0x3C);  //Start watchdog

  //Clear watchdog
//  put_value(WDTCON,0xC3);
//  put_value(WDTCON,0x3C);

  put_hvalue(WDTCNT,0x5A5A);  //release write protection
  put_hvalue(WDTCNT,0xFF00);  //rewrite counter register to overflow => overflow in 256us
  while(1);

}


/*******************************************************************************/
/*  Read non volatile parameters in the last sector of internal uc flash       */
/*  memory, 4096 bytes in the last sector from 0x1F000 to 0x1FFFF              */
/*  Function : read_uc_flash                                                   */
/*      Parameters                                                             */
/*          Input   :   index address from 0 to 4096 where we want to write    */
/*          Input   :   number of bytes to write                               */
/*          Output  :   pointer to the buffer for returning bytes              */
/*******************************************************************************/
__attribute__ ((section(".ramfunc")))
void read_uc_flash(uint32_t address, uint8_t *data, uint32_t nb_byte)
{
  uint16_t cmpt_int;

  for(cmpt_int=0; cmpt_int<nb_byte; cmpt_int++)
    *(data++)=*((uint8_t*)(0x1F000+address+cmpt_int));
}

void setup_ext_sram_rom(void)
{
    /* setup of bus width control register (BWC@0x7810_0000) */
    put_wvalue(BWC, 0xA0);  /* setup of bus width
                               ROM  : nothing
                               SRAM : 16bits
                              IO0  : 16 bits
                              IO1  : nothing */

    /* setup of SRAM access control register (RAMAC@0x7810_0008) */
    put_wvalue(RAMAC, 0x2); /* OE/WE pulse width:3, read off time:2 */

    /* setup of IO0 access control register (IO0AC@0x7810_000C) */
    put_wvalue(IO0AC, 0x1); /* Address Setting up:1, OE/WE pulse width:4, read off time:2 */
}

void setup_malloc(void)
{
//	hcd_malloc_init(INTRAM_ADDR, INTRAM_SIZE, 16, INTRAM);
	hcd_malloc_init(EXTRAM_ADDR, EXTRAM_SIZE, 16, EXTRAM);
}
