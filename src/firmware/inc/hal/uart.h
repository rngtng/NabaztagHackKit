/**
 * @file uart.h
 * @author Oki Electric Industry Co.,Ltd. - 2005 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/14
 * @brief Low level UART functions
 *
 * UART operation set for 115,200 Baud, no parity, 8 data,
 * 1 stop and no flow control
 */
#ifndef _UART_H
#define _UART_H

/* Constants */
#define XON     17      /**< @brief XMODEM ON */
#define XOFF    19      /**< @brief XMODEM OFF */

#define C_CHAR  0x43    /**< @brief XMODEM C */
#define SOH     0x01    /**< @brief XMODEM SOH */
#define STX     0x02    /**< @brief XMODEM STX */
#define EOT     0x04    /**< @brief XMODEM EOT */
#define _ACK    0x06    /**< @brief XMODEM ACK */
#define NAK     0x15    /**< @brief XMODEM NAK */
#define CAN     0x18    /**< @brief XMODEM CAN */

// UARTDL = Fclk / (baudrate x 16)
//#define    DLM_BAUD             0x00  //115.2kbps @32.768MHz
//#define    DLL_BAUD             0x12  //115.2kbps @32.768MHz
/**
 * @brief DLM value for 115200kpbs @32MHz
 */
#define    DLM_BAUD             0x00  //115.2kbps @32MHz
/**
 * @brief DLL value for 115200kpbs @32MHz
 */
#define    DLL_BAUD             0x11  //115.2kbps @32MHz
//#define    DLM_BAUD             0x00  //57.6kbps @32MHz
//#define    DLL_BAUD             0x23  //57.6kbps @32MHz

/* === setting baud rate value === */
/*     baud  | DLM_BAUD | DLL_BAUD */
/*     1,200 |   0x06   |   0xB7   */
/*     2,400 |   0x03   |   0x5B   */
/*     4,800 |   0x01   |   0xAE   */
/*     9,600 |   0x00   |   0xD7   */
/*    19,200 |   0x00   |   0x6B   */
/*    38,400 |   0x00   |   0x36   */
/*    57,600 |   0x00   |   0x24   */
/*   115,200 |   0x00   |   0x12   */

/*************/
/* Functions */
/*************/
void    init_uart(void);
void    write_uart(uint8_t *);
void    putch_uart(uint8_t c);
void    putst_uart(uint8_t *str);
void    putint_uart(int32_t v);
void    puthx_uart(uint32_t v);
void    putbin_uart(uint8_t *str,uint32_t len);

uint64_t xmodem_recv(uint8_t *addr_mem);
void xmodem_send(uint8_t *addr_mem, uint32_t nb_bytes_to_send);
int16_t calcrc(uint8_t *ptr, uint16_t count);

void uart0_interrupt(void);
#endif
