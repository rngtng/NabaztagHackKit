/**
 * @file i2c.c
 * @brief OKI ML67Q4051 I2C peripheral - low-level access.
 *
 * Verbatim port of mtl/firmware/src/hal/i2c.c (Violet / RedoX GCC port) - the
 * bus bring-up + polled master read/write that hal/rfid.c (CRX14 coupler)
 * builds on. No firmwareV2-specific changes: the register set (I2CCTL/I2CSR/
 * I2CDR/...) already lives in sys/inc/ml674061.h, and the polling protocol is
 * unchanged.
 */
#include "ml674061.h"
#include "common.h"

#include "irq.h"

#include "hal/i2c.h"

/**
 * @brief Wait for CMBB bit to be set, or timeout
 *
 * @return Remaining timeout ticks
 */
/* Spin budget for one bus wait (#253). Iteration-bounded rather than
 * time-bounded on purpose: these loops run with interrupts masked, so
 * counter_timer cannot advance inside them and a tick-based timeout would
 * never expire. 20,000 volatile APB reads is on the order of milliseconds at
 * the core clock - well over a hundred byte-times at 100 kHz, so it
 * cannot clip a healthy transfer - where the former 1,000,000 was most of a
 * second, and multiplied by the caller's retries put a wedged bus into a
 * minutes-long system freeze. */
#define I2C_SPIN_MAX 20000

static int32_t waiti2cmbb(void)
{
  int32_t nmax=I2C_SPIN_MAX;
  while((get_hvalue(I2CSR)&I2CSR_I2CMBB)&&(nmax>0)) nmax--;
  return nmax;
}

/**
 * @brief Wait for CMBB bit to be set, or timeout
 *
 * @return Remaining timeout ticks
 */
static int32_t waiti2cmcf(void)
{
  int32_t nmax=I2C_SPIN_MAX;
  while((!(get_hvalue(I2CSR)&I2CSR_I2CMCF))&&(nmax>0)) nmax--;
  return nmax;
}

/* Run a bus wait with interrupts as the caller left them, then re-fence the
 * register work that follows (#252).
 *
 * The polls are the long part of a transfer - and the part a slow or wedged
 * bus stretches - so holding the CPSR I-bit across them stalls the 1 ms tick
 * and everything built on it: nab.time(), nab.wait(), the LED fade engine, the
 * wifi stack's 200 ms rt2501_timer cadence. Nothing on this device drives I2C
 * from an ISR (event.h: the event core is strictly single-context), so a tick
 * landing mid-poll is harmless - the poll only reads a status flag.
 *
 * irq_restore/irq_disable_save round-trip rather than a bare enable/disable, so
 * a caller that was ALREADY masked stays masked throughout (#246): *irq comes
 * back 0x80 and the restore is a no-op.
 */
static int32_t i2c_wait_mbb(uint32_t *irq)
{
  irq_restore(*irq);
  int32_t r = waiti2cmbb();
  *irq = irq_disable_save();
  return r;
}

static int32_t i2c_wait_mcf(uint32_t *irq)
{
  irq_restore(*irq);
  int32_t r = waiti2cmcf();
  *irq = irq_disable_save();
  return r;
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
  /* Save/restore, not a bare disable/enable (#246): an unconditional
   * __enable_interrupt() on every exit path would re-enable interrupts a
   * caller had deliberately masked, mid-critical-section. */
  uint32_t irq = irq_disable_save();

  /* Set Master TX */
  set_hbit(I2CCTL,I2CCTL_I2CMTX);

  /* Wait for I2C bus to be ready */
  if (!i2c_wait_mbb(&irq))
  {
    /* Error, exit */
    irq_restore(irq);
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
    irq_restore(irq);
    return FALSE;
  }

  /* Wait for transfer to be completed */
  if (!i2c_wait_mcf(&irq))
  {
    /* Error, exit */
    irq_restore(irq);
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
    irq_restore(irq);
    return FALSE;
  }

  /* Send data */
  put_hvalue(I2CSR,0x0000);
  while(nb_byte--)
  {
    /* Write data byte */
    put_hvalue(I2CDR,*(data++));
    /* Wait for transfer to be completed */
    if (!i2c_wait_mcf(&irq))
    {
      /* Error/Timeout */
      irq_restore(irq);
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
      irq_restore(irq);
      return FALSE;
    }
    /* Clear  completion bit */
    clr_hbit(I2CSR,I2CSR_I2CMCF);
  }

  /* Clear status */
  put_hvalue(I2CSR,0x0000);
  /* Send I2C Stop condition */
  clr_hbit(I2CCTL,I2CCTL_I2CMSTA);
  irq_restore(irq);
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
  /* Save/restore, not a bare disable/enable (#246): an unconditional
   * __enable_interrupt() on every exit path would re-enable interrupts a
   * caller had deliberately masked, mid-critical-section. */
  uint32_t irq = irq_disable_save();

  /* Master TX */
  clr_hbit(I2CCTL,I2CCTL_I2CMTX);
  /* Wait for I2C bus to be ready */
  if (!i2c_wait_mbb(&irq))
  {
    irq_restore(irq);
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
    irq_restore(irq);
    return FALSE;
  }
  /* Wait for transfer to be completed */
  if (!i2c_wait_mcf(&irq))
  {
  irq_restore(irq);
    return FALSE;
  }
  /* Look at acknowledge */
  if((get_hvalue(I2CSR)&I2CSR_I2CRXAK))
  {
    /* NAck received, send Stop condition */
    clr_hbit(I2CCTL,I2CCTL_I2CMSTA);
    /* Clear status */
    put_hvalue(I2CSR,0x0000);
    irq_restore(irq);
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
    if (!i2c_wait_mcf(&irq))
    {
      irq_restore(irq);
      return FALSE;
    }
    /* Read data */
    *(data++)=get_value(I2CDR);
  } while(nb_byte--);

  /* Send NAck */
  set_hbit(I2CCTL,I2CCTL_I2CTXAK);
  /* Wait for transfer to be completed */
  if (!i2c_wait_mcf(&irq))
  {
    irq_restore(irq);
    return FALSE;
  }
  /* Read last byte of data */
  *(data++)=get_value(I2CDR);
  /* Clear status */
  put_hvalue(I2CSR,0x0000);

  /* Send Stop condition */
  clr_hbit(I2CCTL,I2CCTL_I2CMSTA);
  clr_hbit(I2CCTL,I2CCTL_I2CTXAK);
  irq_restore(irq);
  return TRUE;
}
