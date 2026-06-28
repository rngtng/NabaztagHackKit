/**
 * @file common.h
 * @author Oki Electric Industry Co., LTD. - 2004 - Initial version
 * @author Violet - 2006 - Custom changes
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief Common defs
 */
 #ifndef _COMMON_H_
#define _COMMON_H_

#include <stdint.h>
#define __no_operation()  asm volatile("nop")

extern void __disable_interrupt(void);
extern void __enable_interrupt(void);
/*****************************************************/
/*    internal I/O input/output macro                */
/*****************************************************/
#define get_value(n)    (*((volatile uint8_t *)(n)))          /* byte input */
#define put_value(n,c)  (*((volatile uint8_t *)(n)) = (c))    /* byte output */
#define get_hvalue(n)   (*((volatile uint16_t *)(n)))         /* half word input */
#define put_hvalue(n,c) (*((volatile uint16_t *)(n)) = (c))   /* half word output */
#define get_wvalue(n)   (*((volatile uint32_t *)(n)))          /* word input */
#define put_wvalue(n,c) (*((volatile uint32_t *)(n)) = (c))    /* word output */
#define set_bit(n,c)    (*((volatile uint8_t *)(n))|= (c))    /* byte bit set */
#define clr_bit(n,c)    (*((volatile uint8_t *)(n))&=~(c))    /* byte bit clear */
#define set_hbit(n,c)   (*((volatile uint16_t *)(n))|= (c))   /* half word bit set */
#define clr_hbit(n,c)   (*((volatile uint16_t *)(n))&=~(c))   /* half word bit clear */
#define set_wbit(n,c)   (*((volatile uint32_t *)(n))|= (c))    /* word bit set */
#define clr_wbit(n,c)   (*((volatile uint32_t *)(n))&=~(c))    /* word bit clear */

#define MAX_ADDR	  0x20000
#define UART_BUFFER_SIZE 135

//#define SINUS_TEST

#define TURN_ON_AUDIO_AMPLIFIER CS_AUDIO_AMP_SET
#define TURN_OFF_AUDIO_AMPLIFIER CS_AUDIO_AMP_CLEAR

#define PROTECTED      (1)
#define UNPROTECTED    (0)
#define YES            (1)
#define NO             (0)
#define PROTECT_CANCEL (1)
#define PROTECT_SET    (2)
#define ERROR   (-1)
#ifndef NULL
#define	NULL	0
#endif

#define	SET		1
#define	RESET	0

#define FALSE   0
#define TRUE    1

#define FORWARD 0
#define REVERSE 1

#define E_OK	OK
#define E_NG	NG

#define	OK (0)
#define	NG (-1)

/* bit field of BYTE register */
#define BIT_0   0x01
#define BIT_1   0x02
#define BIT_2   0x04
#define BIT_3   0x08
#define BIT_4   0x10
#define BIT_5   0x20
#define BIT_6   0x40
#define BIT_7   0x80


#ifndef _NAB_SIM
  #define CLR_WDT do {put_value(WDTCON,0xC3);put_value(WDTCON,0x3C);} while(0)
#else
  #define CLR_WDT
#endif

#define LLC2_2  0
#define LLC2_3  1
#define LLC2_4c  2
//#define PCB_RELEASE LLC2_2
//#define PCB_RELEASE LLC2_3
#define PCB_RELEASE LLC2_4c

//#define DRIVER_ST
#define MOTOR_SPEED_CONTROL

#if PCB_RELEASE == LLC2_4c

#define CS_AUDIO_AMP_BIT BIT_6
#define CS_AUDIO_AMP_AS_OUTPUT set_bit(PM4,CS_AUDIO_AMP_BIT)
#define CS_AUDIO_AMP_AS_INPUT clr_bit(PM4,CS_AUDIO_AMP_BIT)
#define CS_AUDIO_AMP_SET      set_bit(PO4,CS_AUDIO_AMP_BIT)
#define CS_AUDIO_AMP_CLEAR    clr_bit(PO4,CS_AUDIO_AMP_BIT)
#define CS_AUDIO_AMP_READ     get_value(PI4)

#define CS_LED_BIT BIT_5
#define CS_LED_AS_OUTPUT set_bit(PM4,CS_LED_BIT)
#define CS_LED_AS_INPUT clr_bit(PM4,CS_LED_BIT)
#define CS_LED_SET      set_bit(PO4,CS_LED_BIT)
#define CS_LED_CLEAR    clr_bit(PO4,CS_LED_BIT)
#define CS_LED_READ     get_value(P4)

#define MODE_LED_BIT BIT_7
#define MODE_LED_AS_OUTPUT set_bit(PM2,MODE_LED_BIT)
#define MODE_LED_AS_INPUT clr_bit(PM2,MODE_LED_BIT)
#define MODE_LED_SET      set_bit(PO2,MODE_LED_BIT)
#define MODE_LED_CLEAR    clr_bit(PO2,MODE_LED_BIT)
#define MODE_LED_READ     get_value(PI2)

#define CS_AUDIO_SCI_BIT BIT_3
#define CS_AUDIO_SCI_AS_OUTPUT set_bit(PM2,CS_AUDIO_SCI_BIT)
#define CS_AUDIO_SCI_AS_INPUT clr_bit(PM2,CS_AUDIO_SCI_BIT)
#define CS_AUDIO_SCI_SET      set_bit(PO2,CS_AUDIO_SCI_BIT)
#define CS_AUDIO_SCI_CLEAR    clr_bit(PO2,CS_AUDIO_SCI_BIT)
#define CS_AUDIO_SCI_READ     get_value(PI2)

#define INT_AUDIO_BIT BIT_2
#define INT_AUDIO_AS_OUTPUT set_bit(PM1,INT_AUDIO_BIT)
#define INT_AUDIO_AS_INPUT clr_bit(PM1,INT_AUDIO_BIT)
#define INT_AUDIO_SET      set_bit(PO1,INT_AUDIO_BIT)
#define INT_AUDIO_CLEAR    clr_bit(PO1,INT_AUDIO_BIT)
#define INT_AUDIO_READ     get_value(PI1)

#define WAIT_USB_BIT BIT_3
#define WAIT_USB_AS_OUTPUT set_bit(PM1,WAIT_USB_BIT)
#define WAIT_USB_AS_INPUT clr_bit(PM1,WAIT_USB_BIT)
#define WAIT_USB_SET      set_bit(PO1,WAIT_USB_BIT)
#define WAIT_USB_CLEAR    clr_bit(PO1,WAIT_USB_BIT)
#define WAIT_USB_READ     get_value(PI1)

#define RST_AUDIO_BIT BIT_7
#define RST_AUDIO_AS_OUTPUT set_bit(PM11,RST_AUDIO_BIT)
#define RST_AUDIO_AS_INPUT clr_bit(PM11,RST_AUDIO_BIT)
#define RST_AUDIO_SET      set_bit(PO11,RST_AUDIO_BIT)
#define RST_AUDIO_CLEAR    clr_bit(PO11,RST_AUDIO_BIT)
#define RST_AUDIO_READ     get_value(PI11)

#define INT_USB_BIT BIT_0
#define INT_USB_AS_OUTPUT set_bit(PM3,INT_USB_BIT)
#define INT_USB_AS_INPUT clr_bit(PM3,INT_USB_BIT)
#define INT_USB_SET      set_bit(PO3,INT_USB_BIT)
#define INT_USB_CLEAR    clr_bit(PO3,INT_USB_BIT)
#define INT_USB_READ     get_value(PI3)

#define INT_SWITCH_BIT BIT_1
#define INT_SWITCH_AS_OUTPUT set_bit(PM3,INT_SWITCH_BIT)
#define INT_SWITCH_AS_INPUT clr_bit(PM3,INT_SWITCH_BIT)
#define INT_SWITCH_SET      set_bit(PO3,INT_SWITCH_BIT)
#define INT_SWITCH_CLEAR    clr_bit(PO3,INT_SWITCH_BIT)
#define INT_SWITCH_READ     get_value(PI3)

#define CS_AUDIO_SDI_BIT BIT_0
#define CS_AUDIO_SDI_AS_OUTPUT set_bit(PM11,CS_AUDIO_SDI_BIT)
#define CS_AUDIO_SDI_AS_INPUT clr_bit(PM11,CS_AUDIO_SDI_BIT)
#define CS_AUDIO_SDI_SET      set_bit(PO11,CS_AUDIO_SDI_BIT)
#define CS_AUDIO_SDI_CLEAR    clr_bit(PO11,CS_AUDIO_SDI_BIT)
#define CS_AUDIO_SDI_READ     get_value(PI11)

#define PWM_MCC1_BIT BIT_2
#define PWM_MCC1_AS_OUTPUT set_bit(PM5,PWM_MCC1_BIT)
#define PWM_MCC1_AS_INPUT clr_bit(PM5,PWM_MCC1_BIT)
#define PWM_MCC1_SET      set_bit(PO5,PWM_MCC1_BIT)
#define PWM_MCC1_CLEAR    clr_bit(PO5,PWM_MCC1_BIT)
#define PWM_MCC1_READ     get_value(PI5)

#define PWM_MCC2_BIT BIT_3
#define PWM_MCC2_AS_OUTPUT set_bit(PM5,PWM_MCC2_BIT)
#define PWM_MCC2_AS_INPUT clr_bit(PM5,PWM_MCC2_BIT)
#define PWM_MCC2_SET      set_bit(PO5,PWM_MCC2_BIT)
#define PWM_MCC2_CLEAR    clr_bit(PO5,PWM_MCC2_BIT)
#define PWM_MCC2_READ     get_value(PI5)

#define PWM_MCC3_BIT BIT_4
#define PWM_MCC3_AS_OUTPUT set_bit(PM5,PWM_MCC3_BIT)
#define PWM_MCC3_AS_INPUT clr_bit(PM5,PWM_MCC3_BIT)
#define PWM_MCC3_SET      set_bit(PO5,PWM_MCC3_BIT)
#define PWM_MCC3_CLEAR    clr_bit(PO5,PWM_MCC3_BIT)
#define PWM_MCC3_READ     get_value(PI5)

#define PWM_MCC4_BIT BIT_5
#define PWM_MCC4_AS_OUTPUT set_bit(PM5,PWM_MCC4_BIT)
#define PWM_MCC4_AS_INPUT clr_bit(PM5,PWM_MCC4_BIT)
#define PWM_MCC4_SET      set_bit(PO5,PWM_MCC4_BIT)
#define PWM_MCC4_CLEAR    clr_bit(PO5,PWM_MCC4_BIT)
#define PWM_MCC4_READ     get_value(PI5)

#elif PCB_RELEASE == LLC2_3

#define CS_AUDIO_AMP_BIT BIT_6
#define CS_AUDIO_AMP_AS_OUTPUT set_bit(PM4,CS_AUDIO_AMP_BIT)
#define CS_AUDIO_AMP_AS_INPUT clr_bit(PM4,CS_AUDIO_AMP_BIT)
#define CS_AUDIO_AMP_SET      set_bit(PO4,CS_AUDIO_AMP_BIT)
#define CS_AUDIO_AMP_CLEAR    clr_bit(PO4,CS_AUDIO_AMP_BIT)
#define CS_AUDIO_AMP_READ     get_value(PI4)

#define CS_LED_BIT BIT_5
#define CS_LED_AS_OUTPUT set_bit(PM4,CS_LED_BIT)
#define CS_LED_AS_INPUT clr_bit(PM4,CS_LED_BIT)
#define CS_LED_SET      set_bit(PO4,CS_LED_BIT)
#define CS_LED_CLEAR    clr_bit(PO4,CS_LED_BIT)
#define CS_LED_READ     get_value(P4)

#define MODE_LED_BIT BIT_7
#define MODE_LED_AS_OUTPUT set_bit(PM2,MODE_LED_BIT)
#define MODE_LED_AS_INPUT clr_bit(PM2,MODE_LED_BIT)
#define MODE_LED_SET      set_bit(PO2,MODE_LED_BIT)
#define MODE_LED_CLEAR    clr_bit(PO2,MODE_LED_BIT)
#define MODE_LED_READ     get_value(PI2)

#define CS_AUDIO_SCI_BIT BIT_3
#define CS_AUDIO_SCI_AS_OUTPUT set_bit(PM2,CS_AUDIO_SCI_BIT)
#define CS_AUDIO_SCI_AS_INPUT clr_bit(PM2,CS_AUDIO_SCI_BIT)
#define CS_AUDIO_SCI_SET      set_bit(PO2,CS_AUDIO_SCI_BIT)
#define CS_AUDIO_SCI_CLEAR    clr_bit(PO2,CS_AUDIO_SCI_BIT)
#define CS_AUDIO_SCI_READ     get_value(PI2)

#define INT_AUDIO_BIT BIT_2
#define INT_AUDIO_AS_OUTPUT set_bit(PM1,INT_AUDIO_BIT)
#define INT_AUDIO_AS_INPUT clr_bit(PM1,INT_AUDIO_BIT)
#define INT_AUDIO_SET      set_bit(PO1,INT_AUDIO_BIT)
#define INT_AUDIO_CLEAR    clr_bit(PO1,INT_AUDIO_BIT)
#define INT_AUDIO_READ     get_value(PI1)

#define WAIT_USB_BIT BIT_3
#define WAIT_USB_AS_OUTPUT set_bit(PM1,WAIT_USB_BIT)
#define WAIT_USB_AS_INPUT clr_bit(PM1,WAIT_USB_BIT)
#define WAIT_USB_SET      set_bit(PO1,WAIT_USB_BIT)
#define WAIT_USB_CLEAR    clr_bit(PO1,WAIT_USB_BIT)
#define WAIT_USB_READ     get_value(PI1)

#define RST_AUDIO_BIT BIT_2
#define RST_AUDIO_AS_OUTPUT set_bit(PM4,RST_AUDIO_BIT)
#define RST_AUDIO_AS_INPUT clr_bit(PM4,RST_AUDIO_BIT)
#define RST_AUDIO_SET      set_bit(PO4,RST_AUDIO_BIT)
#define RST_AUDIO_CLEAR    clr_bit(PO4,RST_AUDIO_BIT)
#define RST_AUDIO_READ     get_value(PI4)

#define CS_FLASH_BIT BIT_1
#define CS_FLASH_AS_OUTPUT set_bit(PM4,CS_FLASH_BIT)
#define CS_FLASH_AS_INPUT clr_bit(PM4,CS_FLASH_BIT)
#define CS_FLASH_SET      set_bit(PO4,CS_FLASH_BIT)
#define CS_FLASH_CLEAR    clr_bit(PO4,CS_FLASH_BIT)
#define CS_FLASH_READ     get_value(PI4)

#define INT_USB_BIT BIT_0
#define INT_USB_AS_OUTPUT set_bit(PM3,INT_USB_BIT)
#define INT_USB_AS_INPUT clr_bit(PM3,INT_USB_BIT)
#define INT_USB_SET      set_bit(PO3,INT_USB_BIT)
#define INT_USB_CLEAR    clr_bit(PO3,INT_USB_BIT)
#define INT_USB_READ     get_value(PI3)

#define INT_SWITCH_BIT BIT_1
#define INT_SWITCH_AS_OUTPUT set_bit(PM3,INT_SWITCH_BIT)
#define INT_SWITCH_AS_INPUT clr_bit(PM3,INT_SWITCH_BIT)
#define INT_SWITCH_SET      set_bit(PO3,INT_SWITCH_BIT)
#define INT_SWITCH_CLEAR    clr_bit(PO3,INT_SWITCH_BIT)
#define INT_SWITCH_READ     get_value(PI3)

#define CS_AUDIO_SDI_BIT BIT_3
#define CS_AUDIO_SDI_AS_OUTPUT set_bit(PM3,CS_AUDIO_SDI_BIT)
#define CS_AUDIO_SDI_AS_INPUT clr_bit(PM3,CS_AUDIO_SDI_BIT)
#define CS_AUDIO_SDI_SET      set_bit(PO3,CS_AUDIO_SDI_BIT)
#define CS_AUDIO_SDI_CLEAR    clr_bit(PO3,CS_AUDIO_SDI_BIT)
#define CS_AUDIO_SDI_READ     get_value(PI3)

#define PWM_MCC1_BIT BIT_2
#define PWM_MCC1_AS_OUTPUT set_bit(PM5,PWM_MCC1_BIT)
#define PWM_MCC1_AS_INPUT clr_bit(PM5,PWM_MCC1_BIT)
#define PWM_MCC1_SET      set_bit(PO5,PWM_MCC1_BIT)
#define PWM_MCC1_CLEAR    clr_bit(PO5,PWM_MCC1_BIT)
#define PWM_MCC1_READ     get_value(PI5)

#define PWM_MCC2_BIT BIT_3
#define PWM_MCC2_AS_OUTPUT set_bit(PM5,PWM_MCC2_BIT)
#define PWM_MCC2_AS_INPUT clr_bit(PM5,PWM_MCC2_BIT)
#define PWM_MCC2_SET      set_bit(PO5,PWM_MCC2_BIT)
#define PWM_MCC2_CLEAR    clr_bit(PO5,PWM_MCC2_BIT)
#define PWM_MCC2_READ     get_value(PI5)

#define PHASE_MCC1_BIT BIT_4
#define PHASE_MCC1_AS_OUTPUT set_bit(PM5,PHASE_MCC1_BIT)
#define PHASE_MCC1_AS_INPUT clr_bit(PM5,PHASE_MCC1_BIT)
#define PHASE_MCC1_SET      set_bit(PO5,PHASE_MCC1_BIT)
#define PHASE_MCC1_CLEAR    clr_bit(PO5,PHASE_MCC1_BIT)
#define PHASE_MCC1_READ     get_value(PI5)

#define PHASE_MCC2_BIT BIT_5
#define PHASE_MCC2_AS_OUTPUT set_bit(PM5,PHASE_MCC2_BIT)
#define PHASE_MCC2_AS_INPUT clr_bit(PM5,PHASE_MCC2_BIT)
#define PHASE_MCC2_SET      set_bit(PO5,PHASE_MCC2_BIT)
#define PHASE_MCC2_CLEAR    clr_bit(PO5,PHASE_MCC2_BIT)
#define PHASE_MCC2_READ     get_value(PI5)

#define PWM_MCC3_BIT        PHASE_MCC1_BIT
#define PWM_MCC3_AS_OUTPUT  PHASE_MCC1_AS_OUTPUT
#define PWM_MCC3_AS_INPUT   PHASE_MCC1_AS_INPUT
#define PWM_MCC3_SET        PHASE_MCC1_SET
#define PWM_MCC3_CLEAR      PHASE_MCC1_CLEAR
#define PWM_MCC3_READ       PHASE_MCC1_READ

#define PWM_MCC4_BIT        PHASE_MCC2_BIT
#define PWM_MCC4_AS_OUTPUT  PHASE_MCC2_AS_OUTPUT
#define PWM_MCC4_AS_INPUT   PHASE_MCC2_AS_INPUT
#define PWM_MCC4_SET        PHASE_MCC2_SET
#define PWM_MCC4_CLEAR      PHASE_MCC2_CLEAR
#define PWM_MCC4_READ       PHASE_MCC2_READ

#elif PCB_RELEASE == LLC2_2

//GPIO as primary definitions
#define CS_LED_BIT BIT_6
#define CS_LED_AS_OUTPUT set_bit(PM2,CS_LED_BIT)
#define CS_LED_AS_INPUT clr_bit(PM2,CS_LED_BIT)
#define CS_LED_SET      set_bit(PO2,CS_LED_BIT)
#define CS_LED_CLEAR    clr_bit(PO2,CS_LED_BIT)
#define CS_LED_READ     get_value(PI2)

#define MODE_LED_BIT BIT_7
#define MODE_LED_AS_OUTPUT set_bit(PM2,MODE_LED_BIT)
#define MODE_LED_AS_INPUT clr_bit(PM2,MODE_LED_BIT)
#define MODE_LED_SET      set_bit(PO2,MODE_LED_BIT)
#define MODE_LED_CLEAR    clr_bit(PO2,MODE_LED_BIT)
#define MODE_LED_READ     get_value(PI2)

#define XD24_BIT BIT_0
#define XD24_AS_OUTPUT set_bit(PM12,XD24_BIT)
#define XD24_AS_INPUT clr_bit(PM12,XD24_BIT)
#define XD24_SET      set_bit(PO12,XD24_BIT)
#define XD24_CLEAR    clr_bit(PO12,XD24_BIT)
#define XD24_READ     get_value(PI12)

/*
#define CS_AUDIO_AMP_BIT BIT_0
#define CS_AUDIO_AMP_AS_OUTPUT set_bit(PM11,CS_AUDIO_AMP_BIT)
#define CS_AUDIO_AMP_AS_INPUT clr_bit(PM11,CS_AUDIO_AMP_BIT)
#define CS_AUDIO_AMP_SET      set_bit(PO11,CS_AUDIO_AMP_BIT)
#define CS_AUDIO_AMP_CLEAR    clr_bit(PO11,CS_AUDIO_AMP_BIT)
#define CS_AUDIO_AMP_READ     get_value(PI11)
*/

#define CS_AUDIO_AMP_BIT BIT_6
#define CS_AUDIO_AMP_AS_OUTPUT set_bit(PM4,CS_AUDIO_AMP_BIT)
#define CS_AUDIO_AMP_AS_INPUT clr_bit(PM4,CS_AUDIO_AMP_BIT)
#define CS_AUDIO_AMP_SET      set_bit(PO4,CS_AUDIO_AMP_BIT)
#define CS_AUDIO_AMP_CLEAR    clr_bit(PO4,CS_AUDIO_AMP_BIT)
#define CS_AUDIO_AMP_READ     get_value(PI4)

#define RST_AUDIO_BIT BIT_5
#define RST_AUDIO_AS_OUTPUT set_bit(PM2,RST_AUDIO_BIT)
#define RST_AUDIO_AS_INPUT clr_bit(PM2,RST_AUDIO_BIT)
#define RST_AUDIO_SET      set_bit(PO2,RST_AUDIO_BIT)
#define RST_AUDIO_CLEAR    clr_bit(PO2,RST_AUDIO_BIT)
#define RST_AUDIO_READ     get_value(PI2)

#define CS_AUDIO_SDI_BIT BIT_4
#define CS_AUDIO_SDI_AS_OUTPUT set_bit(PM2,CS_AUDIO_SDI_BIT)
#define CS_AUDIO_SDI_AS_INPUT clr_bit(PM2,CS_AUDIO_SDI_BIT)
#define CS_AUDIO_SDI_SET      set_bit(PO2,CS_AUDIO_SDI_BIT)
#define CS_AUDIO_SDI_CLEAR    clr_bit(PO2,CS_AUDIO_SDI_BIT)
#define CS_AUDIO_SDI_READ     get_value(PI2)

#define CS_AUDIO_SCI_BIT BIT_3
#define CS_AUDIO_SCI_AS_OUTPUT set_bit(PM2,CS_AUDIO_SCI_BIT)
#define CS_AUDIO_SCI_AS_INPUT clr_bit(PM2,CS_AUDIO_SCI_BIT)
#define CS_AUDIO_SCI_SET      set_bit(PO2,CS_AUDIO_SCI_BIT)
#define CS_AUDIO_SCI_CLEAR    clr_bit(PO2,CS_AUDIO_SCI_BIT)
#define CS_AUDIO_SCI_READ     get_value(PI2)

#define CMD_MCC0_BIT BIT_2
#define CMD_MCC0_AS_OUTPUT set_bit(PM5,CMD_MCC0_BIT)
#define CMD_MCC0_AS_INPUT clr_bit(PM5,CMD_MCC0_BIT)
#define CMD_MCC0_SET      set_bit(PO5,CMD_MCC0_BIT)
#define CMD_MCC0_CLEAR    clr_bit(PO5,CMD_MCC0_BIT)
#define CMD_MCC0_READ     get_value(PI5)

#define CMD_MCC1_BIT BIT_3
#define CMD_MCC1_AS_OUTPUT set_bit(PM5,CMD_MCC1_BIT)
#define CMD_MCC1_AS_INPUT clr_bit(PM5,CMD_MCC1_BIT)
#define CMD_MCC1_SET      set_bit(PO5,CMD_MCC1_BIT)
#define CMD_MCC1_CLEAR    clr_bit(PO5,CMD_MCC1_BIT)
#define CMD_MCC1_READ     get_value(PI5)

#define CMD_MCC2_BIT BIT_4
#define CMD_MCC2_AS_OUTPUT set_bit(PM5,CMD_MCC2_BIT)
#define CMD_MCC2_AS_INPUT clr_bit(PM5,CMD_MCC2_BIT)
#define CMD_MCC2_SET      set_bit(PO5,CMD_MCC2_BIT)
#define CMD_MCC2_CLEAR    clr_bit(PO5,CMD_MCC2_BIT)
#define CMD_MCC2_READ     get_value(PI5)

#define CMD_MCC3_BIT BIT_5
#define CMD_MCC3_AS_OUTPUT set_bit(PM5,CMD_MCC3_BIT)
#define CMD_MCC3_AS_INPUT clr_bit(PM5,CMD_MCC3_BIT)
#define CMD_MCC3_SET      set_bit(PO5,CMD_MCC3_BIT)
#define CMD_MCC3_CLEAR    clr_bit(PO5,CMD_MCC3_BIT)
#define CMD_MCC3_READ     get_value(PI5)

#define WAIT_USB_BIT BIT_4
#define WAIT_USB_AS_OUTPUT set_bit(PM4,WAIT_USB_BIT)
#define WAIT_USB_AS_INPUT clr_bit(PM4,WAIT_USB_BIT)
#define WAIT_USB_SET      set_bit(PO4,WAIT_USB_BIT)
#define WAIT_USB_CLEAR    clr_bit(PO4,WAIT_USB_BIT)
#define WAIT_USB_READ     get_value(PI4)

#define INT_USB_BIT BIT_0
#define INT_USB_AS_OUTPUT set_bit(PM3,INT_USB_BIT)
#define INT_USB_AS_INPUT clr_bit(PM3,INT_USB_BIT)
#define INT_USB_SET      set_bit(PO3,INT_USB_BIT)
#define INT_USB_CLEAR    clr_bit(PO3,INT_USB_BIT)
#define INT_USB_READ     get_value(PI3)

#define INT_AUDIO_BIT BIT_3
#define INT_AUDIO_AS_OUTPUT set_bit(PM1,INT_AUDIO_BIT)
#define INT_AUDIO_AS_INPUT clr_bit(PM1,INT_AUDIO_BIT)
#define INT_AUDIO_SET      set_bit(PO1,INT_AUDIO_BIT)
#define INT_AUDIO_CLEAR    clr_bit(PO1,INT_AUDIO_BIT)
#define INT_AUDIO_READ     get_value(PI1)

#define INT_SWITCH_BIT BIT_1
#define INT_SWITCH_AS_OUTPUT set_bit(PM3,INT_SWITCH_BIT)
#define INT_SWITCH_AS_INPUT clr_bit(PM3,INT_SWITCH_BIT)
#define INT_SWITCH_SET      set_bit(PO3,INT_SWITCH_BIT)
#define INT_SWITCH_CLEAR    clr_bit(PO3,INT_SWITCH_BIT)
#define INT_SWITCH_READ     get_value(PI3)

#define SCL_BIT BIT_4
#define SCL_AS_OUTPUT set_bit(PM1,SCL_BIT)
#define SCL_AS_INPUT clr_bit(PM1,SCL_BIT)
#define SCL_SET      set_bit(PO1,SCL_BIT)
#define SCL_CLEAR    clr_bit(PO1,SCL_BIT)
#define SCL_READ     get_value(PI1)

#define SDA_BIT BIT_5
#define SDA_AS_OUTPUT set_bit(PM1,SDA_BIT)
#define SDA_AS_INPUT clr_bit(PM1,SDA_BIT)
#define SDA_SET      set_bit(PO1,SDA_BIT)
#define SDA_CLEAR    clr_bit(PO1,SDA_BIT)
#define SDA_READ     get_value(PI1)

#endif

#endif
