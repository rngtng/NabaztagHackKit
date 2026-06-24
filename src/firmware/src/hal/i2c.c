/**
 * @file i2c.c
 * @author Violet - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief Manage the I2C low-level access
 */
#include "ml674061.h"
#include "common.h"

#include "hal/i2c.h"

/**
 * @brief Wait for CMBB bit to be set, or timeout
 *
 * @return Remaining timeout ticks
 */
int32_t waiti2cmbb()
{
  int32_t nmax=1000000;
  while((get_hvalue(I2CSR)&I2CSR_I2CMBB)&&(nmax>0)) nmax--;
  return nmax;
}

/**
 * @brief Wait for CMBB bit to be set, or timeout
 *
 * @return Remaining timeout ticks
 */
int32_t waiti2cmcf()
{
  int32_t nmax=1000000;
  while((!(get_hvalue(I2CSR)&I2CSR_I2CMCF))&&(nmax>0)) nmax--;
  return nmax;
}

/**
 * @brief  Initialization of the I2C peripheral
 */
void init_i2c(void)
{
  /* Set secondary function for PB4 and PB5  => I2C bus : SCL + SDA */
  set_wbit(PORTSEL1,0x05000000);

  /* I2C peripheral select */
  set_hbit(I2CCTL,I2CCTL_I2CMEN);

  /* I2C slave address */
  put_hvalue(I2CSADR,0x0000);

  /* I2C bus speed 100kHz @32MHz, I2CBC = (APB_CLK)/( I2C bus speed x 8) */
  put_hvalue(I2CBC,0x0028);

  /* I2C control register, enable I2C module, selects standard mode 100kHz */
  put_hvalue(I2CCTL,0x0080);
}


/**
 * @brief  Write to an I2C peripheral
 *
 * @param [in] addr_i2c Slave address of the I2C peripheral
 * @param [in] data Pointer to the buffer to send
 * @param [in] nb_byte Number of bytes to send
 *
 * @retval false on error
 * @retval true on success
 */
uint8_t write_i2c(uint8_t addr_i2c, uint8_t *data, uint8_t nb_byte)
{
  __disable_interrupt();

  /* Set Master TX */
  set_hbit(I2CCTL,I2CCTL_I2CMTX);
  
  /* Wait for I2C bus to be ready */
  if (!waiti2cmbb())
  {
    /* Error, exit */
    __enable_interrupt();
    return FALSE;
  }


  /* Write slave address */
  put_hvalue(I2CDR,(addr_i2c|I2C_WRITE_INSTR));
  /* Start condition */
  set_hbit(I2CCTL,I2CCTL_I2CMSTA);
  /* Look at arbitration */
  if((get_hvalue(I2CSR)&I2CSR_I2CMAL))
  {
    /* Arbitration lost, Clear status */
    put_hvalue(I2CSR,0x0000);
    __enable_interrupt();
    return FALSE;
  }

  /* Wait for transfer to be completed */
  if (!waiti2cmcf())
  {
    /* Error, exit */
    __enable_interrupt();
    return FALSE;
  }
  /* Look at acknowledge */
  if((get_hvalue(I2CSR)&I2CSR_I2CRXAK))
  {
    /* NAck received, send Stop condition */
    clr_hbit(I2CCTL,I2CCTL_I2CMSTA);
    /* Clear status */
    put_hvalue(I2CSR,0x0000);
    /* Exit */
    __enable_interrupt();
    return FALSE;
  }

  /* Send data */
  put_hvalue(I2CSR,0x0000);
  while(nb_byte--)
  {
    /* Write data byte */
    put_hvalue(I2CDR,*(data++));
    /* Wait for transfer to be completed */
    if (!waiti2cmcf())
    {
      /* Error/Timeout */
      __enable_interrupt();
      return FALSE;
    }
    /* Look at acknowledge */
    if((get_hvalue(I2CSR)&I2CSR_I2CRXAK))
    {
      /* NAck received, send Stop condition */
      clr_hbit(I2CCTL,I2CCTL_I2CMSTA);
      /* Clear status */
      put_hvalue(I2CSR,0x0000);
      /* Exit */
      __enable_interrupt();
      return FALSE;
    }
    /* Clear  completion bit */
    clr_hbit(I2CSR,I2CSR_I2CMCF);
  }

  /* Clear status */
  put_hvalue(I2CSR,0x0000);
  /* Send I2C Stop condition */
  clr_hbit(I2CCTL,I2CCTL_I2CMSTA);
  __enable_interrupt();
  return TRUE;
}

/**
 * @brief Read from an I2C peripheral
 *
 * @param [in] addr_i2c Slave address of the I2C peripheral
 * @param [in] data Pointer to the buffer of reception
 * @param [in] nb_byte Number of bytes to receive
 *
 * @retval false on error
 * @retval true on success
 */

uint8_t read_i2c(uint8_t addr_i2c, uint8_t *data, uint8_t nb_byte)
{
  __disable_interrupt();

  /* Master TX */
  clr_hbit(I2CCTL,I2CCTL_I2CMTX);
  /* Wait for I2C bus to be ready */
  if (!waiti2cmbb())
  {
    __enable_interrupt();
    return FALSE;
  }

  /* Write slave address */
  put_hvalue(I2CDR,(addr_i2c|I2C_READ_INSTR));
  /* Start condition */
  set_hbit(I2CCTL,I2CCTL_I2CMSTA);
  /* Look at arbitration */
  if((get_hvalue(I2CSR)&I2CSR_I2CMAL))
  {
    /* Clear status */
    put_hvalue(I2CSR,0x0000);
    __enable_interrupt();
    return FALSE;
  }
  /* Wait for transfer to be completed */
  if (!waiti2cmcf())
  {
  __enable_interrupt();
    return FALSE;
  }
  /* Look at acknowledge */
  if((get_hvalue(I2CSR)&I2CSR_I2CRXAK))
  {
    /* NAck received, send Stop condition */
    clr_hbit(I2CCTL,I2CCTL_I2CMSTA);
    /* Clear status */
    put_hvalue(I2CSR,0x0000);
    __enable_interrupt();
    return FALSE;
  }

  /* Receive data */
  do {
    /* Clear status */
    put_hvalue(I2CSR,0x0000);
    /* Check if last byte */
    if((nb_byte-1)==0)
      break;
    /* Wait for transfer to be completed */
    if (!waiti2cmcf())
    {
      __enable_interrupt();
      return FALSE;
    }
    /* Read data */
    *(data++)=get_value(I2CDR);
  } while(nb_byte--);

  /* Send NAck */
  set_hbit(I2CCTL,I2CCTL_I2CTXAK);
  /* Wait for transfer to be completed */
  if (!waiti2cmcf())
  {
    __enable_interrupt();
    return FALSE;
  }
  /* Read last byte of data */
  *(data++)=get_value(I2CDR);
  /* Clear status */
  put_hvalue(I2CSR,0x0000);

  /* Send Stop condition */
  clr_hbit(I2CCTL,I2CCTL_I2CMSTA);
  clr_hbit(I2CCTL,I2CCTL_I2CTXAK);
  __enable_interrupt();
  return TRUE;
}
