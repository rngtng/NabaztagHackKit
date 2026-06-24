/**
 * @file uart.c
 * @author Oki Electric Industry Co.,Ltd. - 2005 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/14
 * @brief Low level UART functions
 *
 * UART operation set for 115,200 Baud, no parity, 8 data,
 * 1 stop and no flow control
 */
#include <stdio.h>  // sprintf

#include "ml674061.h"
#include "common.h"
#include "irq.h"

#include "utils/delay.h"

#include "hal/uart.h"

uint8_t UART_BUFFER[UART_BUFFER_SIZE];  /**< @brief RX buffer */
volatile uint8_t uart_buffer_pointer;   /**< @brief RX write index */

/**
 * @brief Write a 8bits character to the serial port
 * @param [in]  c Character to send
 */
void putch_uart(uint8_t c)
{
  /* loop till transmit FIFO becomes empty */
  while ((get_value(UARTLSR0) & UARTLSR_THRE) != UARTLSR_THRE);
  /* write characters to UART Transmitter Holding register */
  put_value(UARTTHR0,c);
}

/**
 * @brief Write a string to the serial port
 *
 * @param [in]  str String to send terminated by Null character (0x00)
 */
void putst_uart(uint8_t *str)
{
  while(*str)
  {
    putch_uart(*str++);
  }
}

/**
 * @brief Send a binary buffer to the serial port
 *
 * @param [in]  str Buffer
 * @param [in]  len Length
 */
void putbin_uart(uint8_t *str,uint32_t len)
{
  while(len--)
  {
    putch_uart(*str++);
  }
}

/**
 * @brief Write an Integer to the serial port, in decimal form
 *
 * @param [in]  v   Value
 */
void putint_uart(int32_t v)
{
  uint8_t buf[32];
  sprintf((char*)buf,"%ld",v);
  putst_uart(buf);
}

/**
 * @brief Write an Integer to the serial port, in hex
 *
 * @param [in]  v   Value
 */
void puthx_uart(uint32_t v)
{
  uint8_t buf[12];
  sprintf((char*)buf,"0x%lx",v);
  putst_uart(buf);
}

/**
 * @brief Initialize UART
 */
void init_uart(void)
{
    set_wbit(PORTSEL1,0x50000);           /* select secondary function (PB0-1)*/

    put_value(UARTLCR0, UARTLCR_DLAB);
    put_value(UARTDLL0, DLL_BAUD);        /* baud rate */
    put_value(UARTDLM0, DLM_BAUD);
                                 /* data length:8bit, stop bit:1, parity:none */
    put_value(UARTLCR0, UARTLCR_LEN8|UARTLCR_STB1|UARTLCR_PDIS);
          /*FIFO:enable, trigger level:1byte, receiver/transmitter FIFO clear */
    put_value(UARTFCR0, UARTFCR_FE|UARTFCR_RFLV1|UARTFCR_RFCLR|UARTFCR_TFCLR);
    clr_bit(UARTFCR0, 0xC0);              /* FIFO trigger level:1byte */

    put_value(UARTIER0,UARTIER_ERBF);   /* Interrupt only when receiving data */

    put_value(UARTMCR0, 0);
}

/**
 * @brief Receive a file in Xmodem mode, 128bytes per frame, CRC 8bits
 *
 * @param [in]  addr_mem Address where the file must be written
 * @return Number of bytes of the file
 */
uint64_t xmodem_recv(uint8_t *addr_mem)
{
  uint8_t cmpt_char=0;
  uint64_t data_width_received=0;

  //wait for a new character to be received
  uart_buffer_pointer=0;
  while(uart_buffer_pointer<1)
  {
    putch_uart(NAK);
    DelayMs(500);
  }

  while(1)
  {
    //wait for the frame to be received 3 + 128 + 1
    while(uart_buffer_pointer<132)
    {
      if(UART_BUFFER[0]==EOT)
      {
        putch_uart(_ACK);
        return data_width_received;
      }
    }

    for(cmpt_char=0; cmpt_char<128; cmpt_char++){
      *(addr_mem++) = UART_BUFFER[cmpt_char+3];
      data_width_received++;
    }
    uart_buffer_pointer=0;
    putch_uart(_ACK);
  }
}

/**
 * @brief Send a file in Xmodem mode, 128bytes per frame, CRC 16bits
 *
 * @param [in]  addr_mem Address where the file is saved
 * @param [in]  nb_bytes_to_send  Length in bytes of the file
 */
void xmodem_send(uint8_t *addr_mem, uint32_t nb_bytes_to_send)
{
  uint8_t cmpt_frame=0;
  uint8_t cmpt_char=0;
  ushort crc;

  //wait for a NAK
  uart_buffer_pointer=0;
  while(1){
    if( (!uart_buffer_pointer) && (UART_BUFFER[0]==C_CHAR) )
      break;
    else
      uart_buffer_pointer=0;
  }

  while(nb_bytes_to_send)
  {
    uint8_t DUMMY_TAB[128];
    if(cmpt_frame++==0)
      cmpt_frame=1;

    //Calc buffer data
    if(nb_bytes_to_send>=128)
    {
      for(cmpt_char=0; cmpt_char<128; cmpt_char++)
        DUMMY_TAB[cmpt_char]=*(addr_mem++);
      nb_bytes_to_send-=128;
    }
    else
    {
      for(cmpt_char=0; cmpt_char<(uint8_t)nb_bytes_to_send; cmpt_char++)
        DUMMY_TAB[cmpt_char]=*(addr_mem++);
      for(cmpt_char=(uint8_t)nb_bytes_to_send; cmpt_char<128; cmpt_char++)
        DUMMY_TAB[cmpt_char]=0x00;
      nb_bytes_to_send=0;
    }
    crc=calcrc((uint8_t*)DUMMY_TAB, 128);

    UART_BUFFER[0]=NAK;
    while(UART_BUFFER[0]==NAK)
    {
      //Send Start of header
      putch_uart(SOH);
      //Send Number of frame
      putch_uart(cmpt_frame);
      putch_uart(cmpt_frame^0xFF);

      //Send data
      for(cmpt_char=0; cmpt_char<128; cmpt_char++)
        putch_uart(DUMMY_TAB[cmpt_char]);

      //Send crc
      putch_uart((uint8_t)(crc>>8));
      putch_uart((uint8_t)crc);

      //wait for answer
      uart_buffer_pointer=0;
      while(1){
        while(!uart_buffer_pointer);
        if( (UART_BUFFER[0]==_ACK) ||(UART_BUFFER[0]==NAK) )
          break;
        else
          uart_buffer_pointer=0;
      }
    }
  }

  //Send End of text
  putch_uart(EOT);
  //wait for a _ACK
  uart_buffer_pointer=0;
  while(1){
    if( (!uart_buffer_pointer) && (UART_BUFFER[0]==_ACK) )
      break;
    else
      uart_buffer_pointer=0;
  }
}

/**
 * @brief CRC CCITT routine multiply by X^16, divide by X^16+X^12+X^5+1
 *
 * @param [in]  *ptr  Pointer to the data string
 * @param [in]  count  Number of bytes concerned by the CRC
 *
 * @return CRC result in 16bits
 */
int16_t calcrc(uint8_t *ptr, uint16_t count)
{
  int16_t crc;
  uint8_t i;

  crc = 0;
  while(--count) {
  crc = crc ^ (int16_t)*ptr++ << 8;
  for(i = 0; i < 8; ++i)
    if(crc & 0x8000)
      crc = crc << 1 ^ 0x1021;
    else
      crc = crc << 1;
  }
  return (crc & 0xFFFF);
}

/**
 * @brief Process of the UART0 interrupt
 */
void uart0_interrupt(void)
{
    //Save received byte in the UART buffer
    uint8_t c=get_value(UARTRBR0);
  if(uart_buffer_pointer < UART_BUFFER_SIZE)
    UART_BUFFER[uart_buffer_pointer++] = c;
  else
    uart_buffer_pointer = 0;
}
