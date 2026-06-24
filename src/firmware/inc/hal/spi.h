/**
 * @file spi.h
 * @author Violet - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief SPI bus low level access
 */
#ifndef _SPI_H_
#define _SPI_H_

/*************/
/* Functions */
/*************/
void init_spi(void);

void WriteSPI(uint8_t data_out);
uint8_t ReadSPI(void);

void WriteSPI_1(uint8_t data_out);
uint8_t ReadSPI_1(void);

#endif
