/**
 * @file rt2501usb_io.c
 * @author Sebastien Bourdeauducq - 2006 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief RT2501 Low level I/O access
 *
 * @note check RT2501 datasheet p40/44 for USB commands description
 */
#include "common.h"

#include "usb/usbh.h"
#include "usb/rt2501usb_hw.h"
#include "usb/rt2501usb_io.h"

/*
 * Retry budget for the PHY CSR serial-access busy-bit poll. The reference
 * driver bounds equivalent polling at REGISTER_BUSY_COUNT (100) retries and
 * treats exhaustion as a hard failure (rt2x00usb_regbusy_read in Linux's
 * rt73usb.c) instead of spinning forever - backported here. Every poll
 * below is already a full USB control transfer (~ms), so the retry count is the
 * meaningful bound and no extra per-iteration delay is needed; on a healthy boot
 * the bit clears within an iteration or two.
 */
#define RT2501_REGBUSY_COUNT 100

/* The Busy bit sits at a different position in PHY_CSR3 (BBP access) than in
 * PHY_CSR4 (RF serial control) - see the *_STRUC definitions in the hw header -
 * so the poll helper takes an explicit mask, derived from the struct so it stays
 * correct if the layout ever changes. */
#define RT2501_PHY_CSR3_BUSY (((PHY_CSR3_STRUC){ .field.Busy = 1 }).word)
#define RT2501_PHY_CSR4_BUSY (((PHY_CSR4_STRUC){ .field.Busy = 1 }).word)

/**
 * @brief Bounded wait for a PHY CSR serial-access busy bit to clear.
 *
 * A stuck busy bit (marginal power-up or a wedged ASIC) would
 * otherwise hang this single-threaded firmware in an unbounded poll with no
 * recovery path.
 *
 * @param [in]  dev        USB device
 * @param [in]  reg        CSR to poll (RT2501_PHY_CSR3 / RT2501_PHY_CSR4)
 * @param [in]  busy_mask  the Busy bit within that CSR
 *
 * @retval  1 the busy bit cleared
 * @retval  0 timed out (caller should abort the register access)
 */
static uint8_t rt2501_wait_csr_ready(PDEVINFO dev, uint16_t reg, uint32_t busy_mask)
{
	uint16_t i;

	for(i = 0; i < RT2501_REGBUSY_COUNT; i++) {
		if(!(rt2501_read(dev, reg) & busy_mask))
			return 1;
	}
	DBG_WIFI("RT2501: CSR busy bit stuck, aborting register access"EOL);
	return 0;
}

/**
 * @brief RT2501 USB Multiple read
 *
 * @param [in]  dev   USB device
 * @param [in]  reg   RT2501 register
 *
 * @retval  0 on read error
 * @return  Register value
 */
uint32_t rt2501_read(PDEVINFO dev, uint16_t reg)
{
	uint32_t val;
	uint32_t ret;

	ret = usbh_control_transfer(dev,
				 0,				                    /* pipe */
				 RT2501_READMULTIMAC,		      /* bRequest */
				 USB_TYPE_VENDOR|USB_DIR_IN,	/* bmRequestType */
				 0,				                    /* wValue */
				 reg,				                  /* wIndex */
				 sizeof(val),			            /* wLength */
				 &val);
	if(ret != sizeof(val))
    return 0;
	return val;
}

/**
 * @brief RT2501 USB Multiple write
 *
 * @param [in]  dev   USB device
 * @param [in]  reg   RT2501 register
 * @param [in]  val   Value to write
 *
 * @retval  0 on write error
 * @retval  1 on success
 */
uint8_t rt2501_write(PDEVINFO dev, uint16_t reg, uint32_t val)
{
	uint32_t ret;

	ret = usbh_control_transfer(dev,
				 0,				                    /* pipe */
				 RT2501_WRITEMULTIMAC,		    /* bRequest */
				 USB_TYPE_VENDOR|USB_DIR_OUT,	/* bmRequestType */
				 0,				                    /* wValue */
				 reg,				                  /* wIndex */
				 sizeof(val),			            /* wLength */
				 &val);
	return (ret == sizeof(val));
}

/**
 * @brief RT2501 USB EEPROM Read
 *
 * @param [in]  dev       USB device
 * @param [in]  address   RT2501 register
 * @param [out] *buf      Output buffer
 * @param [in]  len       Number of bytes to read
 *
 * @retval  0 on read error
 * @retval  1 on read success
 */
uint8_t rt2501_read_eeprom(PDEVINFO dev, uint16_t address,
                           void *buf, uint16_t len)
{
	uint32_t ret;

	ret = usbh_control_transfer(dev,
				 0,				/* pipe */
				 RT2501_READEEPROM,		/* bRequest */
				 USB_TYPE_VENDOR|USB_DIR_IN,	/* bmRequestType */
				 0,				/* wValue */
				 address,			/* wIndex */
				 len,				/* wLength */
				 buf);
	return (ret == len);
}

/**
 * @brief RT2501 Read BBP Serial Control register
 *
 * @param [in]  dev   USB device
 * @param [in]  reg   RT2501 register
 *
 * @return Register value
 */
uint8_t rt2501_read_bbp(PDEVINFO dev, uint8_t reg)
{
	PHY_CSR3_STRUC csr;

	/* Wait until busy flag clears */
	if(!rt2501_wait_csr_ready(dev, RT2501_PHY_CSR3, RT2501_PHY_CSR3_BUSY))
		return 0; /* read failed - value is invalid */

	/* Write register address */
	csr.word = 0;
	csr.field.fRead = 1;
	csr.field.RegNum = reg;
	csr.field.Busy = 1;
	rt2501_write(dev, RT2501_PHY_CSR3, csr.word);

	/* Wait until busy flag clears, then we have the read value */
	if(!rt2501_wait_csr_ready(dev, RT2501_PHY_CSR3, RT2501_PHY_CSR3_BUSY))
		return 0; /* read failed - value is invalid */

	csr.word = rt2501_read(dev, RT2501_PHY_CSR3);

	return (csr.field.Value);
}


/**
 * @brief RT2501 Write BBP Serial Control register
 *
 * @param [in]  dev   USB device
 * @param [in]  reg   RT2501 register
 * @param [in]  val   Value to write
 *
 * @retval  0 on error
 * @retval  1 on success
 */
uint8_t rt2501_write_bbp(PDEVINFO dev, uint8_t reg, uint8_t val)
{
	PHY_CSR3_STRUC csr;

	/* Wait until busy flag clears */
	if(!rt2501_wait_csr_ready(dev, RT2501_PHY_CSR3, RT2501_PHY_CSR3_BUSY))
		return 0; /* write aborted */

	/* Write address and data */
	csr.word = 0;
	csr.field.RegNum = reg;
	csr.field.Value = val;
	csr.field.Busy = 1;
	return rt2501_write(dev, RT2501_PHY_CSR3, csr.word);
}

/**
 * @brief RT2501 Write RF Serial Control register
 *
 * @param [in]  dev   USB device
 * @param [in]  val   Value to write
 *
 * @retval  0 on error
 * @retval  1 on success
 */
uint8_t rt2501_write_rf(PDEVINFO dev, uint32_t val)
{
	/* Wait until busy flag clears */
	if(!rt2501_wait_csr_ready(dev, RT2501_PHY_CSR4, RT2501_PHY_CSR4_BUSY))
		return 0; /* write aborted */

	/* Write value */
	return rt2501_write(dev, RT2501_PHY_CSR4, val);
}
