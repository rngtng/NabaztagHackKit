/**
 * @file usbctrl.h
 * @author Oki Electric Industry Co.,Ltd. - 2003 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief USB Stack - Control
 */
#ifndef _USBCTRL_H_
#define _USBCTRL_H_

#define STATE_IDLE  		1
#define STATE_PERI_ACTV		2
#define STATE_HOST_ACTV		3
#define STATE_OTG_SRP		4

#define USB_SE0             0
#define USB_K_STATE         1
#define USB_J_STATE         2
#define USB_SE1             3

int32_t usbctrl_init(int32_t);

void usbctrl_peri_driver_set(int32_t (*initialize)(void), void (*interrupt)(void));
void usbctrl_host_driver_set(int32_t (*initialize)(void), void (*interrupt)(void));

int32_t usbctrl_mode_set(int32_t);
  #define USB_PERIPHERAL	0
  #define USB_HOST		1
  #define USB_OTG           	2
  #define USB_IDLE		3
  #define USB_OTG_SRP       	4

void usbctrl_vbus_thress(int32_t mode);
  #define VBUS_RISE         0x00
  #define VBUS_FALL         0x01
  #define VBUS_SESS         0x02
  #define VBUS_NC           0x03

void usbctrl_vbus_set(int32_t mode);
  #define VBUS_ON		    0x00
  #define VBUS_OFF		    0x01
  #define VBUS_CHARGE	    0x02
  #define VBUS_DISCHARGE    0x03
  #define VBUS_HOSTSET      0x04

  #define V_ON              (F_VBUSMODE_DRIVE | B_DRVVBUS)	/*0x06*/
  #define V_OFF             (F_VBUSMODE_DRIVE)			/*0x02*/
  #define V_CHRG            (F_VBUSMODE_PULSE | B_CHRGVBUS)	/*0x0B*/
  #define V_DISCHRG         (F_VBUSMODE_PULSE | B_DISCHRGVBUS)	/*0x13*/
  #define V_HOST            (F_VBUSMODE_ROOTHUB)		/*0x00*/

void usbctrl_resistance_set(int32_t);
  #define HIGH_Z            0
  #define PULLUP            1
  #define PULLDOWN          2

void usbctrl_se0_det_int(int32_t);
  #define ENABLE	1
  #define DISABLE	0

int32_t usbctrl_id_check(void);

int32_t usbctrl_vbus_status(void);

void usbctrl_interrupt(void);

extern uint32_t usbctrl_state;

extern void usbhost_interrupt(void);

#endif /* _USBCTRL_H_ */
