/**
 * @file usbctrl.c
 * @author Oki Electric Industry Co.,Ltd. - 2003 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief USB Stack - Control
 */
#include "ml674061.h"
#include "ml60842.h"
#include "common.h"

#include "utils/debug.h"

#include "usb/usbctrl.h"


uint32_t usbctrl_state = 0;

struct _usbctrl_driver_table {
	int32_t (*usb_peri_init)(void);
	void (*usb_peri_interrupt)(void);
	int32_t (*usb_host_init)(void);
	void (*usb_host_interrupt)(void);
} usbctrl_driver_table;

int32_t usbctrl_mode_set(int32_t mode)
{
	int32_t loop;
	int32_t ret;

	usbctrl_resistance_set(PULLDOWN);

	//Set Host function
	put_value(HostPeriSel,B_HOST_SEL);

	//wait for chip to be ready
	loop=0;
	while(get_wvalue(HostPeriSel) & B_OPERATION)
	{
	      if(loop++>100) return E_NG;
	}


	if( usbctrl_driver_table.usb_host_init != NULL )
	{
	      ret = usbctrl_driver_table.usb_host_init();
	      if( ret != 0 )
	      {
		      return E_NG;
	      }
	}
	usbctrl_state = STATE_HOST_ACTV;
	return E_OK;
}

int32_t usbctrl_init(int32_t mode)
{
    int32_t loop;

    //Reset clock control register init
    put_wvalue(RstClkCtl,B_XRUN);
    //Reset the chip
    put_value(RstClkCtl,B_CRST);

    //wait for chip to be ready
    loop=0;
    while(get_wvalue(HostPeriSel) & B_OPERATION)
    {
            if(loop++>100) return E_NG;
    }

    //Set Little Endian mode
    put_value(Endian,B_LITTLEENDIAN);

    put_wvalue(SttTrnsCnt, get_wvalue(SttTrnsCnt));

    //OTG interrupt mask register
    put_wvalue(OTGIntMask,0x00000000);
      //no use of this register

    //Init peripheral interrupts
    put_hvalue(INTENBL,0x0000);

    //Host Control register
    put_value(HostCtl, B_DMAIRQ_MASK|B_OHCIIRQ_MASK);
//    put_value(USB_BASE+HOST_CTL,0x09);
      //PIO transfer, enable OHCI interrupt, disable DMA interrupt

      usbctrl_mode_set( USB_HOST );
      usbctrl_vbus_thress( VBUS_NC );
      usbctrl_vbus_set( VBUS_HOSTSET );

	return E_OK;
}

void usbctrl_host_driver_set(int32_t (*initialize)(void), void (*interrupt)(void))
{
	usbctrl_driver_table.usb_host_init = initialize;
	usbctrl_driver_table.usb_host_interrupt = interrupt;

	return;
}

void usbctrl_resistance_set(int32_t mode)
{
	uint32_t    otg_ctl;

	otg_ctl = (get_wvalue(OTGCtl) & ~(B_PDCTLDP | B_PDCTLDM | B_PUCTLDP));

	if( mode == PULLUP )
	{
		otg_ctl |= B_PUCTLDP;
	}
	else if( mode == PULLDOWN )
	{
		otg_ctl |= (B_PDCTLDM | B_PDCTLDP);
	}
	else if( mode == HIGH_Z )
	{
	}

	put_wvalue(OTGCtl, otg_ctl);

	return;
}

void usbctrl_vbus_set(int32_t mode)
{
	uint32_t    ctl;

	ctl = get_wvalue(OTGCtl);
	ctl &= ~(B_DISCHRGVBUS | B_CHRGVBUS | B_DRVVBUS | M_VBUSMODE);

	switch(mode)
	{
	case VBUS_OFF:
		ctl |= V_OFF;
		break;

	case VBUS_ON:
		ctl |= V_ON;
		break;

	case VBUS_CHARGE:
		ctl |= V_CHRG;
		break;

	case VBUS_DISCHARGE:
		ctl |= V_DISCHRG;
		break;

	case VBUS_HOSTSET:
		ctl |= V_HOST;
		break;

	default:
		return;
	}

	put_wvalue(OTGCtl, ctl);
}

void usbctrl_vbus_thress(int32_t mode)
{
	uint32_t msk;
	uint32_t ctl;

	msk = get_wvalue( OTGIntMask );
	ctl = (get_wvalue(OTGCtl) & ~(M_SELSV));

	if( mode == VBUS_RISE )
	{
		put_wvalue( OTGCtl, (ctl | B_AVBUSVLDENB | B_ABSESSVLDENB) );
		put_wvalue( OTGIntMask, ((msk | B_AVBUSVLDCHGINT) & ~B_ABSESSVLDCHGINT) );
	}
	else if( mode == VBUS_FALL )
	{
		ctl |= 0x00000500;
		put_wvalue( OTGCtl, (ctl | B_AVBUSVLDENB | B_ABSESSVLDENB) );
		put_wvalue( OTGIntMask, ((msk | B_AVBUSVLDCHGINT) & ~B_ABSESSVLDCHGINT) );
	}
	else if( mode == VBUS_SESS )
	{
		put_wvalue( OTGCtl, (ctl | B_AVBUSVLDENB | B_ABSESSVLDENB) );
		put_wvalue( OTGIntMask, ((msk & ~B_AVBUSVLDCHGINT) | B_ABSESSVLDCHGINT) );
	}
	else if( mode == VBUS_NC )
	{
		put_wvalue( OTGCtl, (ctl & ~(B_AVBUSVLDENB| B_ABSESSVLDENB)) );
		put_wvalue( OTGIntMask, (msk & ~(B_AVBUSVLDCHGINT | B_ABSESSVLDCHGINT)) );
	}

	return;
}

void usbctrl_interrupt(void)
{
	uint32_t status;
	int32_t   loop;

        //Clear EXINT2 interrupt
        set_hbit(EXIRQB,IRQB_IRQ36);

	DBG_USB("ML60842 interrupt"EOL);
	status = get_wvalue(OTGIntStt);
	status &= get_wvalue(OTGIntMask);

	if(status & (B_AVBUSVLDCHGINT | B_ABSESSVLDCHGINT))
	{
       		DBG_USB(" VBUS status changed"EOL);

//		put_wvalue(OTGIntStt, (B_AVBUSVLDCHG | B_ABSESSVLDCHG));

//		usbctrl_vbus_changed();

		return;
	}
	else if(status & B_IDSELCHGINT)
	{
		DBG_USB(" Host/Periphral ID changed"EOL);

//		usbctrl_id_changed();

		return;
	}
	else if(status & B_BSE0SRPDETSTINT)
	{
		DBG_USB(" Host/Periphral ID changed"EOL);

//		usbctrl_se0_det();

		return;
	}

	if(usbctrl_state == STATE_HOST_ACTV)
	{
		DBG_USB("usb_host_interrupt"EOL);
		usbctrl_driver_table.usb_host_interrupt();
	}
	else if(usbctrl_state == STATE_PERI_ACTV)
	{
		DBG_USB("usb_peri_interrupt"EOL);
		usbctrl_driver_table.usb_peri_interrupt();
	}
	else
	{
		DBG_USB("unknown interrupt"EOL);
		loop=0;
		while(get_wvalue(HostPeriSel) & B_OPERATION)
		{
			if(loop++>100) return;
		}
		put_wvalue(RstClkCtl, B_XRUN);
		put_wvalue(RstClkCtl, (B_PRST | B_HRST));
	}
}
