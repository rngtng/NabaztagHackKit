/**
 * @file i2c.h
 * @author Violet - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief Manage the I2C low-level access
 */
#ifndef _I2C_H_
#define _I2C_H_

/* I2C Slave address */
#define CRX14_ADDR		0xA0

/* CRX14 settings */
#define I2C_WRITE_INSTR		0x00
#define I2C_READ_INSTR		0x01

#define CRX14_PARAMETER_REGISTER      0x00
#define CRX14_IO_FRAME_REGISTER       0x01
#define CRX14_AUTHENTICATE_REGISTER   0x02
#define CRX14_SLOT_MARKER_REGISTER    0x03

void init_i2c(void);
uint8_t  write_i2c(uint8_t  addr_i2c, uint8_t  *data, uint8_t  nb_byte);
uint8_t  read_i2c(uint8_t  addr_i2c, uint8_t  *data, uint8_t  nb_byte);

#endif
