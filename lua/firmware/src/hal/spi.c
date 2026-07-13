/**
 * @file spi.c
 * @author Violet - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief SPI bus low level access
 */
#include "ml674061.h"
#include "common.h"

#include "hal/spi.h"

/**
 * @brief Initialization of the SPI peripheral
 */
void init_spi(void)
{
  //Set secondary function for SPI0
  set_wbit(PORTSEL2,0x00000015);
  //MSB First
  set_wbit(SPCR0,SPCR0_LSBF);
  //SPI0 mode, read and write on rising edge, clk default to low level
  clr_wbit(SPCR0,SPCR0_CPOL);
  clr_wbit(SPCR0,SPCR0_CPHA);
  //SPI0 baudrate
  put_wvalue(SPBRR0,0x00000000);
    //8bits mode
    //baudrate = APB_CLK/2 => 16MHz @ 32MHz
    //DTL=0clk
    //LEAD=0.5clk
    //AG=0.5clk

  //SPIO Master Mode
  set_wbit(SPCR0,SPCR0_MSTR);
  //SPIO FIFO Clear
  set_wbit(SPCR0,SPCR0_FICLR);
  clr_wbit(SPCR0,SPCR0_FICLR);

  //Clear SPI completion flag
  get_wvalue(SPSR0);
  //enable SPI transfer
  set_wbit(SPCR0,SPCR0_SPE);

#if (PCB_RELEASE == LLC2_3) || (PCB_RELEASE == LLC2_4c)

  //Set secondary function for SPI1
  set_wbit(PORTSEL2,0x00001500);
  //MSB First
  set_wbit(SPCR1,SPCR1_LSBF);
  //SPI0 mode, read and write on rising edge, clk default to low level
  clr_wbit(SPCR1,SPCR1_CPOL);
  clr_wbit(SPCR1,SPCR1_CPHA);
  //SPI0 baudrate
  put_wvalue(SPBRR1,0x00000000);
    //8bits mode
    //baudrate = APB_CLK/2 => 16MHz @ 32MHz
    //DTL=0clk
    //LEAD=0.5clk
    //AG=0.5clk

  //SPIO Master Mode
  set_wbit(SPCR1,SPCR1_MSTR);
  //SPIO FIFO Clear
  set_wbit(SPCR1,SPCR1_FICLR);
  clr_wbit(SPCR1,SPCR1_FICLR);

  //Clear SPI completion flag
  get_wvalue(SPSR1);
  //enable SPI transfer
  set_wbit(SPCR1,SPCR1_SPE);

  //Clear overflow
  set_wbit(SPSR1,SPSR1_ORF);

#endif
}

/**
 * @brief Write a byte to the SPI0 bus
 *
 * @param [in] data_out Byte to send
 */
void WriteSPI(uint8_t data_out)
{
  //write data
  put_value(SPDWR0,data_out);
  //wait for transfer to be completed
  while(!(get_wvalue(SPSR0)&SPSR0_SPIF));
//  while( !(get_wvalue(SPSR0) & SPSR0_TFE) );
  return ;
}

/**
 * @brief Read a byte from the SPI0 bus
 *
 * @return Received byte
 */
uint8_t ReadSPI(void)
{
  uint8_t read_char;

  //write data
  put_value(SPDWR0,0x00);
  //wait for receiving to be completed
  while(!(get_wvalue(SPSR0)&SPSR0_SPIF));
//  while( get_wvalue(SPSR0) & SPSR0_RFE );
  //read data
  read_char=get_value(SPDRR0);
  //Return success
  return read_char;
}

/**
 * @brief Write a byte to the SPI1 bus
 *
 * @param [in] data_out  Byte to send
 */
void WriteSPI_1(uint8_t data_out)
{
  //write data
  put_value(SPDWR1,data_out);
  //wait for transfer to be completed
  while(!(get_wvalue(SPSR1)&SPSR1_SPIF));
//  while( !(get_wvalue(SPSR0) & SPSR0_TFE) );
  return ;
}

/**
 * @brief Read a byte from the SPI1 bus
 *
 * @return Received byte
 */
uint8_t ReadSPI_1(void)
{
  uint8_t read_char;

  //write data
  put_value(SPDWR1,0x00);
  //wait for receiving to be completed
  while(!(get_wvalue(SPSR1)&SPSR1_SPIF));
  //read data
  read_char=get_value(SPDRR1);
  //Return success
  return read_char;
}
