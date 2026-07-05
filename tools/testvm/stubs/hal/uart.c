#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include "hal/uart.h"

/**
 * @brief Write a 8bits character to the serial port
 * @param [in]  c Character to send
 */
void putch_uart(uint8_t c)
{
  putchar(c);
}

/**
 * @brief Write a string to the serial port
 *
 * @param [in]  str String to send terminated by Null character (0x00)
 */
void putst_uart(uint8_t *str)
{
  printf("%s",(char*)str);
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
    printf("0x%2X",*str++);
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

}

/**
 * @brief Receive a file in Xmodem mode, 128bytes per frame, CRC 8bits
 *
 * @param [in]  addr_mem Address where the file must be written
 * @return Number of bytes of the file
 */
uint64_t xmodem_recv(uint8_t *addr_mem)
{

}

/**
 * @brief Send a file in Xmodem mode, 128bytes per frame, CRC 16bits
 *
 * @param [in]  addr_mem Address where the file is saved
 * @param [in]  nb_bytes_to_send  Length in bytes of the file
 */
void xmodem_send(uint8_t *addr_mem, uint32_t nb_bytes_to_send)
{

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

}
