/**
 * @file i2c.h
 * @brief OKI ML67Q4051 I2C peripheral - low-level access.
 *
 * Verbatim port of src/firmware/src/hal/i2c.c's API (Violet / RedoX GCC port).
 * Register defs (I2CCTL/I2CSR/I2CDR/...) already live in sys/inc/ml674061.h -
 * this header only declares the driver entry points. CRX14-specific constants
 * (slave address, registers) live in hal/rfid.h, the one consumer.
 */
#ifndef _I2C_H_
#define _I2C_H_

/* Read/write direction bit ORed into the 7-bit slave address (I2CDR's low bit). */
#define I2C_WRITE_INSTR 0x00
#define I2C_READ_INSTR  0x01

void init_i2c(void);
uint8_t write_i2c(uint8_t addr_i2c, uint8_t *data, uint8_t nb_byte);
uint8_t read_i2c(uint8_t addr_i2c, uint8_t *data, uint8_t nb_byte);

#endif
