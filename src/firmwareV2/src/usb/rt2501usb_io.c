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
	do {
		csr.word = rt2501_read(dev, RT2501_PHY_CSR3);
	} while(csr.field.Busy);

	/* Write register address */
	csr.word = 0;
	csr.field.fRead = 1;
	csr.field.RegNum = reg;
	csr.field.Busy = 1;
	rt2501_write(dev, RT2501_PHY_CSR3, csr.word);

	/* Wait until busy flag clears, then we have the read value */
	do {
		csr.word = rt2501_read(dev, RT2501_PHY_CSR3);
	} while(csr.field.Busy);

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
	do {
		csr.word = rt2501_read(dev, RT2501_PHY_CSR3);
	} while(csr.field.Busy);

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
	PHY_CSR4_STRUC	csr;

	/* Wait until busy flag clears */
	do {
		csr.word = rt2501_read(dev, RT2501_PHY_CSR4);
	} while(csr.field.Busy);

	/* Write value */
	return rt2501_write(dev, RT2501_PHY_CSR4, val);
}
