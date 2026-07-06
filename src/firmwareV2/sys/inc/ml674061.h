/**
 * @file ml674061.h
 * @author Oki Electric Industry Co., LTD. - 2005 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief ML674061 Chip definitions
 */
#ifndef ML674061_H
#define ML674061_H

/*------------------------------ uPLAT-7B core -----------------------------------*/
/*****************************************************/
/*    interrupt control register                     */
/*****************************************************/
#define ICR_BASE    0x78000000      /* base address of interrupt control register */
#define IRQ         (ICR_BASE+0x00) /* IRQ register */
#define IRQS        (ICR_BASE+0x04) /* IRQ soft register */
#define FIQ         (ICR_BASE+0x08) /* FIQ register */
#define FIQRAW      (ICR_BASE+0x0C) /* FIQRAW status register */
#define FIQEN       (ICR_BASE+0x10) /* FIQ enable register */
#define IRN         (ICR_BASE+0x14) /* IRQ number register */
#define CIL         (ICR_BASE+0x18) /* current IRQ level register */
#define IRL         (ICR_BASE+0x1C) /* IRQ level register */
#define ILC0        (ICR_BASE+0x20) /* IRQ level control register 0 */
#define ILC1        (ICR_BASE+0x24) /* IRQ level control register 1 */
#define CILCL       (ICR_BASE+0x28) /* current IRQ level clear register */
#define CILE        (ICR_BASE+0x2C) /* current IRQ level encode register */

/* bit field of IRQ register */
#define IRQ_nIR0    0x00000001  /* nIR[0] */
#define IRQ_nIR1    0x00000002  /* nIR[1] */
#define IRQ_nIR2    0x00000004  /* nIR[2] */
#define IRQ_nIR3    0x00000008  /* nIR[3] */
#define IRQ_nIR4    0x00000010  /* nIR[4] */
#define IRQ_nIR5    0x00000020  /* nIR[5] */
#define IRQ_nIR6    0x00000040  /* nIR[6] */
#define IRQ_nIR7    0x00000080  /* nIR[7] */
#define IRQ_nIR8    0x00000100  /* nIR[8] */
#define IRQ_nIR9    0x00000200  /* nIR[9] */
#define IRQ_nIR10   0x00000400  /* nIR[10] */
#define IRQ_nIR11   0x00000800  /* nIR[11] */
#define IRQ_nIR12   0x00001000  /* nIR[12] */
#define IRQ_nIR13   0x00002000  /* nIR[13] */
#define IRQ_nIR14   0x00004000  /* nIR[14] */
#define IRQ_nIR15   0x00008000  /* nIR[15] */

/* bit field of CIL register */
#define CIL_INT_LV1 0x00000002  /* interrupt level 1 */
#define CIL_INT_LV2 0x00000004  /* interrupt level 2 */
#define CIL_INT_LV3 0x00000008  /* interrupt level 3 */
#define CIL_INT_LV4 0x00000010  /* interrupt level 4 */
#define CIL_INT_LV5 0x00000020  /* interrupt level 5 */
#define CIL_INT_LV6 0x00000040  /* interrupt level 6 */
#define CIL_INT_LV7 0x00000080  /* interrupt level 7 */

/* bit field of ILC0 register */
#define ILC0_INT_LV1    0x11111111  /* interrupt level 1 */
#define ILC0_INT_LV2    0x22222222  /* interrupt level 2 */
#define ILC0_INT_LV3    0x33333333  /* interrupt level 3 */
#define ILC0_INT_LV4    0x44444444  /* interrupt level 4 */
#define ILC0_INT_LV5    0x55555555  /* interrupt level 5 */
#define ILC0_INT_LV6    0x66666666  /* interrupt level 6 */
#define ILC0_INT_LV7    0x77777777  /* interrupt level 7 */
#define ILC0_ILR0       0x00000007  /* nIR[0] */
#define ILC0_ILR1       0x00000070  /* nIR[1],nIR[2],nIR[3] */
#define ILC0_ILR4       0x00070000  /* nIR[4],nIR[5] */
#define ILC0_ILR6       0x07000000  /* nIR[6],nIR[7] */

/* bit field of ILC1 register */
#define ILC1_INT_LV1    0x11111111  /* interrupt level 1 */
#define ILC1_INT_LV2    0x22222222  /* interrupt level 2 */
#define ILC1_INT_LV3    0x33333333  /* interrupt level 3 */
#define ILC1_INT_LV4    0x44444444  /* interrupt level 4 */
#define ILC1_INT_LV5    0x55555555  /* interrupt level 5 */
#define ILC1_INT_LV6    0x66666666  /* interrupt level 6 */
#define ILC1_INT_LV7    0x77777777  /* interrupt level 7 */
#define ILC1_ILR8       0x00000007  /* nIR[8] */
#define ILC1_ILR9       0x00000070  /* nIR[9] */
#define ILC1_ILR10      0x00000700  /* nIR[10] */
#define ILC1_ILR11      0x00007000  /* nIR[11] */
#define ILC1_ILR12      0x00070000  /* nIR[12] */
#define ILC1_ILR13      0x00700000  /* nIR[13] */
#define ILC1_ILR14      0x07000000  /* nIR[14] */
#define ILC1_ILR15      0x70000000  /* nIR[15] */

/* bit field of CILCL register */
#define CILCL_CLEAR     0x01        /* most significant '1' bit of CIL is cleared */

/* DMA] */
#define TRM_DREQ	0x00ul	/* DMA] DREQ è */
#define TRM_AUTO	0x01ul	/* DMA] I[gNGXg */

#define TSIZ_8		0x00ul	/* DMA] f[^TCY 8rbg */
#define TSIZ_16		0x02ul	/* DMA] f[^TCY 16rbg */
#define TSIZ_32		0x04ul	/* DMA] f[^TCY 32rbg */

#define SDP_CONT	0x00ul	/* DMA] \[XAhX Åè */
#define SDP_INC		0x08ul	/* DMA] \[XAhX CNg */

#define DDP_CONT	0x00ul	/* DMA] fBXel[VAhX Åè */
#define DDP_INC		0x10ul	/* DMA] fBXel[VAhX CNg */

#define BRQ_BURST	0x00ul	/* DMA] o[Xg[h */
#define BRQ_CYCLE	0x20ul	/* DMA] TCNX`[[h */

#define IMK_DIS		0x00ul	/* DMA] èÝ}XNð */
#define IMK_ENA		0x40ul	/* DMA] èÝ}XNÝè */

#define DMACSIZ_MAX	0x10000ul	/* DMA] Ååñ] */

#define DMASTA_STA0 	0x0000001ul	/* DMA] Xe[^X CH0 */
#define DMASTA_STA1 	0x0000002ul	/* DMA] Xe[^X CH1 */

#define DMAINT_ISTX0	0x00010100ul	/* DMA] I¹Xe[^X CH0 */
#define DMAINT_ISTX1	0x00020200ul	/* DMA] I¹Xe[^X CH1 */

/*****************************************************/
/*    external memory control register               */
/*****************************************************/
#define EMCR_BASE   0x78100000          /* base address */
#define BWC         (EMCR_BASE+0x00)    /* bus width control register */
#define ROMAC       (EMCR_BASE+0x04)    /* external ROM access control register */
#define RAMAC       (EMCR_BASE+0x08)    /* external SRAM access control register */
#define IO0AC       (EMCR_BASE+0x0C)    /* external IO0 access control register */
#define IO1AC       (EMCR_BASE+0x10)    /* external IO1 access control register */

/*****************************************************/
/*    system control register                        */
/*****************************************************/
#define SCR_BASE    0xB8000000      /* base address */
#define IDR         (SCR_BASE+0x00) /* ID register */
#define CLKSTP      (SCR_BASE+0x04) /* clock stop register */
#define RMPCON      (SCR_BASE+0x10) /* remap control register */

#define	SCR_BASE2	0xB7000000		/* base address */
#define PECLKCNT	(SCR_BASE2+0x00)	/* peripheral clock control register */
#define PERSTCNT	(SCR_BASE2+0x04)	/* peripheral reset control register */
#define PLL1		(SCR_BASE2+0x08)	/* PLL1 control register */
#define PLL2		(SCR_BASE2+0x0C)	/* PLL2 control register */
#define CLKCNT		(SCR_BASE2+0x10)	/* clock control register */
#define CLKSTPCNT	(SCR_BASE2+0x14)	/* clock stop control register */
#define DMARQCNT	(SCR_BASE2+0x1C)	/* DMA request select register */
#define LSICNT		(SCR_BASE2+0x20)	/* LSI control register */
#define	PORTSEL1	(SCR_BASE2+0x24)	/* port control register 1 */
#define	PORTSEL2	(SCR_BASE2+0x28)	/* port control register 2 */
#define	PORTSEL3	(SCR_BASE2+0x2C)	/* port control register 3 */
#define	PORTSEL4	(SCR_BASE2+0x30)	/* port control register 4 */
#define	PORTSEL5	(SCR_BASE2+0x34)	/* port control register 5 */
#define FTMSEL		(SCR_BASE2+0x40)	/* FTM control register */
#define CKWT        (SCR_BASE2+0x44) 	/* clock wait register */
#define FLACON		(SCR_BASE2+0x100)	/* Flash memory control register */

/* bit field of CLKSTP register */
#define CLKSTP_CPUG  0x04        /* CPU group HALT */
#define CLKSTP_TIC   0x02        /* TIC HALT */
#define CLKSTP_SIO0  0x01        /* SIO HALT */
#define CLKSTP_STOP  0x80        /* clock stop */

/* bit field of CLKSTP register */
#define CLKCNT_PLLSEL		0x00000003
#define CLKCNT_CLKDIVA		0x0000001C
#define CLKCNT_CLKDIVB		0x00000060
#define CLKCNT_SRCSEL		0x00000300
#define CLKCNT_APBDIV		0x00000C00
#define CLKCNT_AUDIOSEL		0x00003000
#define CLKCNT_PLLENA		0x00010000
#define CLKCNT_PLLENB		0x00020000
#define CLKCNT_RINGOSCEN	0x00040000
#define CLKCNT_SYSCLKEN		0x00080000
#define CLKCNT_RTCCLKEN		0x00100000

/* bit field of CLKSTP register */
#define LSICNT_I2SOUT	0x00000001
#define LSICNT_I2CNFON	0x00000010
#define LSICNT_JTAGE	0x00000100

/* bit field of PORTSEL1 register */
#define PORTSEL1_PA6	0x00003000
#define PORTSEL1_PB0	0x00030000
#define PORTSEL1_PB1	0x000C0000
#define PORTSEL1_PB2	0x00300000
#define PORTSEL1_PB3	0x00C00000
#define PORTSEL1_PB4	0x03000000
#define PORTSEL1_PB5	0x0C000000

/*****************************************************/
/*    system timer control register                  */
/*****************************************************/
#define STCR_BASE   0xB8001000          /* base address */
#define TMEN        (STCR_BASE+0x04)    /* timer enable register */
#define TMRLR       (STCR_BASE+0x08)    /* timer reload register */
#define TMOVF       (STCR_BASE+0x10)    /* overflow register */

/* bit field of TMEN register */
#define TMEN_TCEN   0x01        /* timer enabled */

/* bit field of TMOVF register */
#define TMOVF_OVF   0x01        /* overflow generated */


/*****************************************************/
/*    SIO control register                           */
/*****************************************************/
#define SC_BASE     0xB8002000      /* base address */
#define SIOBUF      (SC_BASE+0x00)  /* transmiting/receiving buffer register */
#define SIOSTA      (SC_BASE+0x04)  /* SIO status register */
#define SIOCON      (SC_BASE+0x08)  /* SIO control register */
#define SIOBCN      (SC_BASE+0x0C)  /* baud rate control register */
#define SIOBT       (SC_BASE+0x14)  /* baud rate timer register */
#define SIOTCN      (SC_BASE+0x18)  /* SIO test control register */

/* bit field of SIOSTA register */
#define SIOSTA_FERR     0x0001      /* framing error */
#define SIOSTA_OERR     0x0002      /* overrun error */
#define SIOSTA_PERR     0x0004      /* parity error */
#define SIOSTA_RVIRQ    0x0010      /* receive ready */
#define SIOSTA_TRIRQ    0x0020      /* transmit ready */

/* bit field of SIOCON register */
#define SIOCON_LN7      0x0001      /* data length : 7bit */
#define SIOCON_LN8      0x0000      /* data length : 8bit */
#define SIOCON_PEN      0x0002      /* parity enabled */
#define SIOCON_PDIS     0x0000      /* parity disabled */
#define SIOCON_EVN      0x0004      /* even parity */
#define SIOCON_ODD      0x0000      /* odd parity */
#define SIOCON_TSTB1    0x0008      /* stop bit : 1 */
#define SIOCON_TSTB2    0x0000      /* stop bit : 2 */

/* bit field of SIOBCN register */
#define SIOBCN_BGRUN    0x0010      /* count start */

/* bit field of SIOTCN register */
#define SIOTCN_MFERR    0x0001      /* generate framin error */
#define SIOTCN_MPERR    0x0002      /* generate parity error */
#define SIOTCN_LBTST    0x0080      /* loop back test */


/*---------------------------------- ML674051/61 ------------------------------------*/
/*****************************************************/
/*    timer control register                         */
/*****************************************************/
#define FTM_BASE	0xB7F00000  /* base address */
#define FTM0CON		(FTM_BASE+0x00) /* timer0 control register */
#define FTM0ST		(FTM_BASE+0x04)	/* timer0 status register */
#define FTM0C		(FTM_BASE+0x08) /* timer0 counter register */
#define FTM0R		(FTM_BASE+0x0C)	/* timer0 register */
#define FTM0GR		(FTM_BASE+0x10)	/* timer0 wide use register */
#define FTM0IOLV	(FTM_BASE+0x14)	/* timer0 I/O level register */
#define FTM0OUT		(FTM_BASE+0x18)	/* timer0 out register */
#define FTM0IER		(FTM_BASE+0x1C) /* timer0 interrupt enable register */
#define FTM1CON		(FTM_BASE+0x20) /* timer1 control register */
#define FTM1ST		(FTM_BASE+0x24)	/* timer1 status register */
#define FTM1C		(FTM_BASE+0x28) /* timer1 counter register */
#define FTM1R		(FTM_BASE+0x2C)	/* timer1 register */
#define FTM1GR		(FTM_BASE+0x30)	/* timer1 wide use register */
#define FTM1IOLV	(FTM_BASE+0x34)	/* timer1 I/O level register */
#define FTM1OUT		(FTM_BASE+0x38)	/* timer1 out register */
#define FTM1IER		(FTM_BASE+0x3C) /* timer1 interrupt enable register */
#define FTM2CON		(FTM_BASE+0x40) /* timer2 control register */
#define FTM2ST		(FTM_BASE+0x44)	/* timer2 status register */
#define FTM2C		(FTM_BASE+0x48) /* timer2 counter register */
#define FTM2R		(FTM_BASE+0x4C)	/* timer2 register */
#define FTM2GR		(FTM_BASE+0x50)	/* timer2 wide use register */
#define FTM2IOLV	(FTM_BASE+0x54)	/* timer2 I/O level register */
#define FTM2OUT		(FTM_BASE+0x58)	/* timer2 out register */
#define FTM2IER		(FTM_BASE+0x5C) /* timer2 interrupt enable register */
#define FTM3CON		(FTM_BASE+0x60) /* timer3 control register */
#define FTM3ST		(FTM_BASE+0x64)	/* timer3 status register */
#define FTM3C		(FTM_BASE+0x68) /* timer3 counter register */
#define FTM3R		(FTM_BASE+0x6C)	/* timer3 register */
#define FTM3GR		(FTM_BASE+0x70)	/* timer3 wide use register */
#define FTM3IOLV	(FTM_BASE+0x74)	/* timer3 I/O level register */
#define FTM3OUT		(FTM_BASE+0x78)	/* timer3 out register */
#define FTM3IER		(FTM_BASE+0x7C) /* timer3 interrupt enable register */
#define FTM4CON		(FTM_BASE+0x80) /* timer4 control register */
#define FTM4ST		(FTM_BASE+0x84)	/* timer4 status register */
#define FTM4C		(FTM_BASE+0x88) /* timer4 counter register */
#define FTM4R		(FTM_BASE+0x8C)	/* timer4 register */
#define FTM4GR		(FTM_BASE+0x90)	/* timer4 wide use register */
#define FTM4IOLV	(FTM_BASE+0x94)	/* timer4 I/O level register */
#define FTM4OUT		(FTM_BASE+0x98)	/* timer4 out register */
#define FTM4IER		(FTM_BASE+0x9C) /* timer4 interrupt enable register */
#define FTM5CON		(FTM_BASE+0xA0) /* timer5 control register */
#define FTM5ST		(FTM_BASE+0xA4)	/* timer5 status register */
#define FTM5C		(FTM_BASE+0xA8) /* timer5 counter register */
#define FTM5R		(FTM_BASE+0xAC)	/* timer5 register */
#define FTM5GR		(FTM_BASE+0xB0)	/* timer5 wide use register */
#define FTM5IOLV	(FTM_BASE+0xB4)	/* timer5 I/O level register */
#define FTM5OUT		(FTM_BASE+0xB8)	/* timer5 out register */
#define FTM5IER		(FTM_BASE+0xBC) /* timer5 interrupt enable register */
#define	FTMEN		(FTM_BASE+0xC0)	/* timer enable register */
#define FTMDIS		(FTM_BASE+0xC4)	/* timer disable register */

/* bit field of FTMnCON */
#define FTM0CON_FTMCLK	0x07
#define FTM1CON_FTMCLK	0x07
#define FTM2CON_FTMCLK	0x07
#define FTM3CON_FTMCLK	0x07
#define FTM4CON_FTMCLK	0x07
#define FTM5CON_FTMCLK	0x07

#define FTM0CON_MOD		0x18
#define FTM1CON_MOD		0x18
#define FTM2CON_MOD		0x18
#define FTM3CON_MOD		0x18
#define FTM4CON_MOD		0x18
#define FTM5CON_MOD		0x18

/* bit field of FTMnST */
#define FTM0ST_CM_CAPEV	0x1
#define FTM1ST_CM_CAPEV	0x1
#define FTM2ST_CM_CAPEV	0x1
#define FTM3ST_CM_CAPEV	0x1
#define FTM4ST_CM_CAPEV	0x1
#define FTM5ST_CM_CAPEV	0x1

#define FTM0ST_OVF		0x2
#define FTM1ST_OVF		0x2
#define FTM2ST_OVF		0x2
#define FTM3ST_OVF		0x2
#define FTM4ST_OVF		0x2
#define FTM5ST_OVF		0x2

/* bit field of FTMnIER */
#define FTM0IER_OVFIE	0x2
#define FTM1IER_OVFIE	0x2
#define FTM2IER_OVFIE	0x2
#define FTM3IER_OVFIE	0x2
#define FTM4IER_OVFIE	0x2
#define FTM5IER_OVFIE	0x2

/* bit field of FTMDIS */
#define FTMDIS_FTMDIS0	0x01	/* TIMER0 */
#define FTMDIS_FTMDIS1	0x02	/* TIMER1 */
#define FTMDIS_FTMDIS2	0x04	/* TIMER2 */
#define FTMDIS_FTMDIS3	0x08	/* TIMER3 */
#define FTMDIS_FTMDIS4	0x10	/* TIMER4 */
#define FTMDIS_FTMDIS5	0x20	/* TIMER5 */

/*****************************************************/
/*    Watch Dog Timer control register               */
/*****************************************************/
#define WDT_BASE    0xB7E00000  /* base address */
#define WDTCON      (WDT_BASE+0x00) /* Watch Dog Timer control register */
#define TBGCON     	(WDT_BASE+0x04) /* time base counter control register */
#define INTST		(WDT_BASE+0x08)	/* interrupt status register */
#define OVFAST		(WDT_BASE+0x0C)	/* WDTOVFN assert register */
#define WDTCNT		(WDT_BASE+0x10)	/* WDT counter register */

/* bit field of WDTCON */
#define WDTCON_0xC3 0xC3    /* 0xC3 */
#define WDTCON_0x3C 0x3C    /* 0x3C */

/* bit field of TBGCON */
#define TBGCON_WDHLT	0x80	/* HALT */
#define TBGCON_OFINT	0x40	/* reset by overflow */
#define TBGCON_WDCK32	0x00	/* APB_CLK/32 */
#define TBGCON_WDCK64	0x01	/* APB_CLK/64 */
#define TBGCON_WDCK128	0x02	/* APB_CLK/128 */
#define TBGCON_WDCK256	0x03	/* APB_CLK/256 */
#define TBGCON_WDCK512	0x04	/* APB_CLK/512 */
#define TBGCON_WDCK1024	0x05	/* APB_CLK/1024 */
#define TBGCON_WDCK2048	0x06	/* APB_CLK/2048 */
#define TBGCON_WDCK4096	0x07	/* APB_CLK/4096 */
#define TBGCON_0x5A		0x5A

/* bit field of INTST */
#define INTST_RSTSTATUS 0x01
#define INTST_WDTINT    0x10

/*****************************************************/
/*    UART control register                          */
/*****************************************************/
#define UCR_BASE     0xB7B00000  /* base address */
#define UARTRBR0     (UCR_BASE+0x00) /* receiver buffer register */
#define UARTTHR0     (UCR_BASE+0x00) /* transmitter buffer register */
#define UARTIER0     (UCR_BASE+0x04) /* interrupt enable register */
#define UARTIIR0     (UCR_BASE+0x08) /* interrupt identification */
#define UARTFCR0     (UCR_BASE+0x08) /* FIFO control register */
#define UARTLCR0     (UCR_BASE+0x0C) /* line control register */
#define UARTMCR0     (UCR_BASE+0x10) /* modem control register */
#define UARTLSR0     (UCR_BASE+0x14) /* line status register */
#define UARTMSR0     (UCR_BASE+0x18) /* modem status register */
#define UARTSCR0     (UCR_BASE+0x1C) /* scratchpad register */
#define UARTDLL0     (UCR_BASE+0x00) /* divisor latch(LSB) */
#define UARTDLM0     (UCR_BASE+0x04) /* divisor latch(MSB) */

#define UCR_BASE1    0xB7B01000  /* base address */
#define UARTRBR1     (UCR_BASE1+0x00) /* receiver buffer register */
#define UARTTHR1     (UCR_BASE1+0x00) /* transmitter buffer register */
#define UARTIER1     (UCR_BASE1+0x04) /* interrupt enable register */
#define UARTIIR1     (UCR_BASE1+0x08) /* interrupt identification */
#define UARTFCR1     (UCR_BASE1+0x08) /* FIFO control register */
#define UARTLCR1     (UCR_BASE1+0x0C) /* line control register */
#define UARTMCR1     (UCR_BASE1+0x10) /* modem control register */
#define UARTLSR1     (UCR_BASE1+0x14) /* line status register */
#define UARTMSR1     (UCR_BASE1+0x18) /* modem status register */
#define UARTSCR1     (UCR_BASE1+0x1C) /* scratchpad register */
#define UARTDLL1     (UCR_BASE1+0x00) /* divisor latch(LSB) */
#define UARTDLM1     (UCR_BASE1+0x04) /* divisor latch(MSB) */

/* bit field of UARTLCR register */
#define UARTLCR_LEN5    0x0000  /* data length : 5bit */
#define UARTLCR_LEN6    0x0001  /* data length : 6bit */
#define UARTLCR_LEN7    0x0002  /* data length : 7bit */
#define UARTLCR_LEN8    0x0003  /* data length : 8bit */
#define UARTLCR_STB1    0x0000  /* stop bit : 1 */
#define UARTLCR_STB2    0x0004  /* stop bit : 2(data length 6-8) */
#define UARTLCR_STB1_5  0x0004  /* stop bit : 1.5(data length 5) */
#define UARTLCR_PEN     0x0008  /* parity enabled */
#define UARTLCR_PDIS    0x0000  /* parity disabled */
#define UARTLCR_EVN     0x0010  /* even parity */
#define UARTLCR_ODD     0x0000  /* odd parity */
#define UARTLCR_SP      0x0020  /* stick parity */
#define UARTLCR_BRK     0x0040  /* break delivery */
#define UARTLCR_DLAB    0x0080  /* devisor latch access bit */

/* bit field of UARTLSR register */
#define UARTLSR_DR      0x0001  /* data ready */
#define UARTLSR_OE      0x0002  /* overrun error */
#define UARTLSR_PE      0x0004  /* parity error */
#define UARTLSR_FE      0x0008  /* framing error */
#define UARTLSR_BI      0x0010  /* break interrupt */
#define UARTLSR_THRE    0x0020  /* transmitter holding register empty */
#define UARTLSR_TEMT    0x0040  /* transmitter empty */
#define UARTLSR_ERF     0x0080  /* receiver FIFO error */

/* bit field of UARTFCR register */
#define UARTFCR_FE      0x0001  /* FIFO enable */
#define UARTFCR_FD      0x0000  /* FIFO disable */
#define UARTFCR_RFCLR   0x0002  /* receiver FIFO clear */
#define UARTFCR_TFCLR   0x0004  /* transmitter FIFO clear */
#define UARTFCR_OVR		0x0010	/* over run error mask */
#define UARTFCR_RFLV1   0x0000  /* RCVR FIFO interrupt trigger level : 1byte */
#define UARTFCR_RFLV4   0x0040  /* RCVR FIFO interrupt trigger level : 4byte */
#define UARTFCR_RFLV8   0x0080  /* RCVR FIFO interrupt trigger level : 8byte */
#define UARTFCR_RFLV14  0x00C0  /* RCVR FIFO interrupt trigger level : 14byte */

/* bit field of UARTMCR register */
#define UARTMCR_DTR     0x0001  /* data terminal ready */
#define UARTMCR_RTS     0x0002  /* request to send */
#define UARTMCR_LOOP    0x0010  /* loopback */

/* bit field of UARTMSR register */
#define UARTMSR_DCTS    0x0001  /* delta clear to send */
#define UARTMSR_DDSR    0x0002  /* delta data set ready */
#define UARTMSR_TERI    0x0004  /* trailing edge of ring endicator */
#define UARTMSR_DDCD    0x0008  /* delta data carrer detect */
#define UARTMSR_CTS     0x0010  /* clear to send */
#define UARTMSR_DSR     0x0020  /* data set ready */
#define UARTMSR_RI      0x0040  /* ring indicator */
#define UARTMSR_RLSD     0x0080  /* data carrer detect */

/* bit field of UARTIIR register */
#define UARTIIR_TP      0x0001  /* interrupt generated */
#define UARTIIR_MODEMI  0x0000  /* modem status interrupt */
#define UARTIIR_TRA     0x0002  /* transmitter interrupt */
#define UARTIIR_RCV     0x0004  /* receiver interrupt */
#define UARTIIR_LINE	0x0006	/* receive line error interrupt */
#define UARTIIR_TO      0x000C  /* time out interrupt */
#define UARTIIR_FM      0x00C0  /* FIFO mode */

/* bit field of UARTIER register */
#define UARTIER_ERBF    0x0001  /* enable received data available interrupt */
#define UARTIER_ETBEF   0x0002  /* enable transmitter holding register empty interrupt */
#define UARTIER_ELSI    0x0004  /* enable receiver line status interrupt */
#define UARTIER_EDSSI   0x0008  /* enable modem status interrupt */

/*****************************************************/
/*    port control register                           */
/*****************************************************/
#define PCR_BASE0	0xB7A00000  	/* base address */
#define PO0			(PCR_BASE0+0x00) 	/* port0(PA) output register */
#define PI0			(PCR_BASE0+0x04) 	/* port0(PA) input register */
#define PM0			(PCR_BASE0+0x08) 	/* port0(PA) Mode register */
#define IE0			(PCR_BASE0+0x0C) 	/* port0(PA) interrupt enable */
#define IM0			(PCR_BASE0+0x10) 	/* port0(PA) interrupt Mode register */
#define IS0			(PCR_BASE0+0x18) 	/* port0(PA) interrupt Status register*/

#define PCR_BASE1	0xB7A01000  	/* base address */
#define PO1			(PCR_BASE1+0x00) 	/* port1(PB) output register */
#define PI1			(PCR_BASE1+0x04) 	/* port1(PB) input register */
#define PM1			(PCR_BASE1+0x08) 	/* port1(PB) Mode register */
#define IE1			(PCR_BASE1+0x0C) 	/* port1(PB) interrupt enable */
#define IM1			(PCR_BASE1+0x10) 	/* port1(PB) interrupt Mode register */
#define IS1			(PCR_BASE1+0x18) 	/* port1(PB) interrupt Status register*/

#define PCR_BASE2	0xB7A02000  	/* base address */
#define PO2			(PCR_BASE2+0x00) 	/* port2(PC) output register */
#define PI2			(PCR_BASE2+0x04) 	/* port2(PC) input register */
#define PM2			(PCR_BASE2+0x08) 	/* port2(PC) Mode register */
#define IE2			(PCR_BASE2+0x0C) 	/* port2(PC) interrupt enable */
#define IM2			(PCR_BASE2+0x10) 	/* port2(PC) interrupt Mode register */
#define IS2			(PCR_BASE2+0x18) 	/* port2(PC) interrupt Status register*/

#define PCR_BASE3	0xB7A03000  	/* base address */
#define PO3			(PCR_BASE3+0x00) 	/* port3(PD) output register */
#define PI3			(PCR_BASE3+0x04) 	/* port3(PD) input register */
#define PM3			(PCR_BASE3+0x08) 	/* port3(PD) Mode register */
#define IE3			(PCR_BASE3+0x0C) 	/* port3(PD) interrupt enable */
#define IM3			(PCR_BASE3+0x10) 	/* port3(PD) interrupt Mode register */
#define IS3			(PCR_BASE3+0x18) 	/* port3(PD) interrupt Status register*/

#define PCR_BASE4	0xB7A04000  	/* base address */
#define PO4			(PCR_BASE4+0x00) 	/* port4(PE) output register */
#define PI4			(PCR_BASE4+0x04) 	/* port4(PE) input register */
#define PM4			(PCR_BASE4+0x08) 	/* port4(PE) Mode register */
#define IE4			(PCR_BASE4+0x0C) 	/* port4(PE) interrupt enable */
#define IM4			(PCR_BASE4+0x10) 	/* port4(PE) interrupt Mode register */
#define IS4			(PCR_BASE4+0x18) 	/* port4(PE) interrupt Status register*/

#define PCR_BASE5	0xB7A05000  	/* base address */
#define PO5			(PCR_BASE5+0x00) 	/* port5(PF) output register */
#define PI5			(PCR_BASE5+0x04) 	/* port5(PF) input register */
#define PM5			(PCR_BASE5+0x08) 	/* port5(PF) Mode register */
#define IE5			(PCR_BASE5+0x0C) 	/* port5(PF) interrupt enable */
#define IM5			(PCR_BASE5+0x10) 	/* port5(PF) interrupt Mode register */
#define IS5			(PCR_BASE5+0x18) 	/* port5(PF) interrupt Status register*/

#define PCR_BASE6	0xB7A06000  	/* base address */
#define PO6			(PCR_BASE6+0x00) 	/* port6(PG) output register */
#define PI6			(PCR_BASE6+0x04) 	/* port6(PG) input register */
#define PM6			(PCR_BASE6+0x08) 	/* port6(PG) Mode register */
#define IE6			(PCR_BASE6+0x0C) 	/* port6(PG) interrupt enable */
#define IM6			(PCR_BASE6+0x10) 	/* port6(PG) interrupt Mode register */
#define IS6			(PCR_BASE6+0x18) 	/* port6(PG) interrupt Status register*/

#define PCR_BASE7	0xB7A07000  	/* base address */
#define PO7			(PCR_BASE7+0x00) 	/* port7(PH) output register */
#define PI7			(PCR_BASE7+0x04) 	/* port7(PH) input register */
#define PM7			(PCR_BASE7+0x08) 	/* port7(PH) Mode register */
#define IE7			(PCR_BASE7+0x0C) 	/* port7(PH) interrupt enable */
#define IM7			(PCR_BASE7+0x10) 	/* port7(PH) interrupt Mode register */
#define IS7			(PCR_BASE7+0x18) 	/* port7(PH) interrupt Status register*/

#define PCR_BASE8	0xB7A08000  	/* base address */
#define PO8			(PCR_BASE8+0x00) 	/* port8(PI) output register */
#define PI8			(PCR_BASE8+0x04) 	/* port8(PI) input register */
#define PM8			(PCR_BASE8+0x08) 	/* port8(PI) Mode register */
#define IE8			(PCR_BASE8+0x0C) 	/* port8(PI) interrupt enable */
#define IM8			(PCR_BASE8+0x10) 	/* port8(PI) interrupt Mode register */
#define IS8			(PCR_BASE8+0x18) 	/* port8(PI) interrupt Status register*/

#define PCR_BASE9	0xB7A09000  	/* base address */
#define PO9			(PCR_BASE9+0x00) 	/* port9(PJ) output register */
#define PI9			(PCR_BASE9+0x04) 	/* port9(PJ) input register */
#define PM9			(PCR_BASE9+0x08) 	/* port9(PJ) Mode register */
#define IE9			(PCR_BASE9+0x0C) 	/* port9(PJ) interrupt enable */
#define IM9			(PCR_BASE9+0x10) 	/* port9(PJ) interrupt Mode register */
#define IS9			(PCR_BASE9+0x18) 	/* port9(PJ) interrupt Status register*/

#define PCR_BASE10	0xB7A0A000  	/* base address */
#define PO10		(PCR_BASE10+0x00) 	/* port10(PK) output register */
#define PI10		(PCR_BASE10+0x04) 	/* port10(PK) input register */
#define PM10		(PCR_BASE10+0x08) 	/* port10(PK) Mode register */
#define IE10		(PCR_BASE10+0x0C) 	/* port10(PK) interrupt enable */
#define IM10		(PCR_BASE10+0x10) 	/* port10(PK) interrupt Mode register */
#define IS10		(PCR_BASE10+0x18) 	/* port10(PK) interrupt Status register*/

#define PCR_BASE11	0xB7A0B000  	/* base address */
#define PO11		(PCR_BASE11+0x00) 	/* port11(PL) output register */
#define PI11		(PCR_BASE11+0x04) 	/* port11(PL) input register */
#define PM11		(PCR_BASE11+0x08) 	/* port11(PL) Mode register */
#define IE11		(PCR_BASE11+0x0C) 	/* port11(PL) interrupt enable */
#define IM11		(PCR_BASE11+0x10) 	/* port11(PL) interrupt Mode register */
#define IS11		(PCR_BASE11+0x18) 	/* port11(PL) interrupt Status register*/

#define PCR_BASE12	0xB7A0C000  	/* base address */
#define PO12		(PCR_BASE12+0x00) 	/* port12(PM) output register */
#define PI12		(PCR_BASE12+0x04) 	/* port12(PM) input register */
#define PM12		(PCR_BASE12+0x08) 	/* port12(PM) Mode register */
#define IE12		(PCR_BASE12+0x0C) 	/* port12(PM) interrupt enable */
#define IM12		(PCR_BASE12+0x10) 	/* port12(PM) interrupt Mode register */
#define IS12		(PCR_BASE12+0x18) 	/* port12(PM) interrupt Status register*/

#define PCR_BASE13	0xB7A0D000  	/* base address */
#define PO13		(PCR_BASE13+0x00) 	/* port13(PN) output register */
#define PI13		(PCR_BASE13+0x04) 	/* port13(PN) input register */
#define PM13		(PCR_BASE13+0x08) 	/* port13(PN) Mode register */
#define IE13		(PCR_BASE13+0x0C) 	/* port13(PN) interrupt enable */
#define IM13		(PCR_BASE13+0x10) 	/* port13(PN) interrupt Mode register */
#define IS13		(PCR_BASE13+0x18) 	/* port13(PN) interrupt Status register*/

#define PCR_BASE14	0xB7A0E000  	/* base address */
#define PO14		(PCR_BASE14+0x00) 	/* port14(PO) output register */
#define PI14		(PCR_BASE14+0x04) 	/* port14(PO) input register */
#define PM14		(PCR_BASE14+0x08) 	/* port14(PO) Mode register */
#define IE14		(PCR_BASE14+0x0C) 	/* port14(PO) interrupt enable */
#define IM14		(PCR_BASE14+0x10) 	/* port14(PO) interrupt Mode register */
#define IS14		(PCR_BASE14+0x18) 	/* port14(PO) interrupt Status register*/

/*****************************************************/
/*    ADC control register                           */
/*****************************************************/
#define ADC_BASE    0xB6000000  /* base address */
#define ADCON0      (ADC_BASE+0x00) /* ADC control 0 register */
#define ADCON1      (ADC_BASE+0x04) /* ADC control 1 register */
#define ADCON2      (ADC_BASE+0x08) /* ADC control 2 register */
#define ADINT       (ADC_BASE+0x0C) /* AD interrupt control register */
#define ADFINT      (ADC_BASE+0x10) /* AD Forced interrupt register */
#define ADR0        (ADC_BASE+0x14) /* AD Result 0 register */
#define ADR1        (ADC_BASE+0x18) /* AD Result 1 register */
#define ADR2        (ADC_BASE+0x1C) /* AD Result 2 register */
#define ADR3        (ADC_BASE+0x20) /* AD Result 3 register */


/* bit field of ADCON0 register */
#define ADCON0_CH0_3    0x0000  /* CH0-CH3 */
#define ADCON0_CH1_3    0x0001  /* CH1-CH3 */
#define ADCON0_CH2_3    0x0002  /* CH2-CH3 */
#define ADCON0_CH3	    0x0003  /* CH3 */
#define ADCON0_ADRUN    0x0010  /* AD conversion start */
#define ADCON0_SCNC     0x0040  /* Stop after a round */

/* bit field of ADCON1 register */
#define ADCON1_CH0      0x0000  /* CH0 */
#define ADCON1_CH1      0x0001  /* CH1 */
#define ADCON1_CH2      0x0002  /* CH2 */
#define ADCON1_CH3      0x0003  /* CH3 */
#define ADCON1_STS      0x0010  /* AD conversion start */

/* bit field of ADCON2 register */
#define ADCON2_CLK2     0x0000  /* CPUCLK/2 */
#define ADCON2_CLK4     0x0001  /* CPUCLK/4 */
#define ADCON2_CLK8     0x0002  /* CPUCLK/8 */
#define ADCON2_CLK16    0x0003  /* CPUCLK/16 */
#define ADCON2_CLK32    0x0004  /* CPUCLK/32 */
#define ADCON2_CLK64    0x0005  /* CPUCLK/64 */

/* bit field of ADINT register */
#define ADINT_INTSN     0x0001  /* AD conversion of ch7 finished (scan mode) */
#define ADINT_INTST     0x0002  /* AD conversion finished (select mode) */
#define ADINT_ADSNIE    0x0004  /* enable interrupt (scan mode) */
#define ADINT_ADSTIE    0x0008  /* enable interrupt (select mode) */

/* bit field of ADFINT register */
#define ADFINT_ADFAS    0x0001  /* Assert interrupt signal */

/*****************************************************/
/*    DMA control register                           */
/*****************************************************/
#define DMA_BASE    0x7BE00000  /* base address */
#define DMAMOD      (DMA_BASE+0x0000)   /* DMA Mode register */
#define DMASTA      (DMA_BASE+0x0004)   /* DMA Status register */
#define DMAINT      (DMA_BASE+0x0008)   /* DMA interrupt Status register */
#define DMACMSK0    (DMA_BASE+0x0100)   /* Channel 0 Mask register */
#define DMACTMOD0   (DMA_BASE+0x0104)   /* Channel 0 Transfer Mode register */
#define DMACSAD0    (DMA_BASE+0x0108)   /* Channel 0 Source Address register */
#define DMACDAD0    (DMA_BASE+0x010C)   /* Channel 0 Destination Address register */
#define DMACSIZ0    (DMA_BASE+0x0110)   /* Channel 0 Transfer Size register */
#define DMACCINT0   (DMA_BASE+0x0114)   /* Channel 0 interrupt Clear register */
#define DMACMSK1    (DMA_BASE+0x0200)   /* Channel 1 Mask register */
#define DMACTMOD1   (DMA_BASE+0x0204)   /* Channel 1 Transfer Mode register */
#define DMACSAD1    (DMA_BASE+0x0208)   /* Channel 1 Source Address register */
#define DMACDAD1    (DMA_BASE+0x020C)   /* Channel 1 Destination Address register */
#define DMACSIZ1    (DMA_BASE+0x0210)   /* Channel 1 Transfer Size register */
#define DMACCINT1   (DMA_BASE+0x0214)   /* Channel 1 interrupt Clear register */

/* bit field of DMAMOD register */
#define DMAMOD_FIX  0x0000  /* Priority of DMA channel : CH0 > CH1 */
#define DMAMOD_RR   0x0001  /* Priority of DMA channel : Round robin */

/* bit field of DMASTA register */
//#define DMASTA_STA0 0x0001  /* Non-transmitted data is in CH0 */
//#define DMASTA_STA1 0x0002  /* Non-transmitted data is in CH1 */

/* bit field of DMAINT register */
#define DMAINT_IREQ0    0x00000001  /* CH0 interrupt */
#define DMAINT_IREQ1    0x00000002  /* CH1 interrupt */
#define DMAINT_ISTA0    0x00000100  /* CH0 abnormal end */
#define DMAINT_ISTA1    0x00000200  /* CH1 abnormal end */
#define DMAINT_ISTP0    0x00010000  /* CH0 abnormal end situation */
#define DMAINT_ISTP1    0x00020000  /* CH1 abnormal end situation */

/* bit field of DMAMSK0,1 register */
#define DMACMSK_MSK  0x00000001  /* Mask */

/* bit field of DMATMOD0,1 register */
#define DMACTMOD_ARQ    0x00000001  /* Auto request */
#define DMACTMOD_ERQ    0x00000000  /* External request */
#define DMACTMOD_BYTE   0x00000000  /* Byte transmission */
#define DMACTMOD_HWORD  0x00000002  /* Half word transmission */
#define DMACTMOD_WORD   0x00000004  /* Word transmission */
#define DMACTMOD_SFA    0x00000000  /* Source data type(fixed address device) */
#define DMACTMOD_SIA    0x00000008  /* Source data type(incremental address device) */
#define DMACTMOD_DFA    0x00000000  /* Destination data type(fixed address device) */
#define DMACTMOD_DIA    0x00000010  /* Destination data type(incremental address device) */
#define DMACTMOD_BM     0x00000000  /* Bus request mode(burst mode) */
#define DMACTMOD_CSM    0x00000020  /* Bus request mode(cycle steal mode) */
#define DMACTMOD_IMK    0x00000040  /* interrupt mask */


/*****************************************************/
/*    extended interrupt control register                     */
/*****************************************************/
#define EIC_BASE	0x7BF00000  /* base address */
#define EXIRS		(EIC_BASE+0x00) /* Extended interrupt Size register */
#define EXIRCL		(EIC_BASE+0x04) /* Extended interrupt Clear register */
#define EXIRQA		(EIC_BASE+0x10)	/* Extended IRQ register A */
#define EXIRQB		(EIC_BASE+0x20)	/* Extended IRQ register B */
#define EXIRQC		(EIC_BASE+0x30)	/* Extended IRQ register C */
#define EXILCA		(EIC_BASE+0x18)	/* Extended IRQ Level contorol register A */
#define EXILCB		(EIC_BASE+0x28)	/* Extended IRQ Level contorol register B */
#define EXILCC		(EIC_BASE+0x38)	/* Extended IRQ Level contorol register C */
#define EXIDM		(EIC_BASE+0x24)	/* Extended IRQ edge mode set register */
#define EXIRQSA		(EIC_BASE+0x1C)	/* Extended interrupt IRQ status register A */
#define EXIRQSB		(EIC_BASE+0x2C)	/* Extended interrupt IRQ status register B */
#define EXFIQ		(EIC_BASE+0x80)	/* Extended FIQ register */
#define EXFIDM		(EIC_BASE+0x84)	/* Extended FIQ mode register */

/* bit field of IRQA register */
#define IRQA_IRQ16  0x00000001  /* IRQ16 */
#define IRQA_IRQ17  0x00000002  /* IRQ17 */
#define IRQA_IRQ18  0x00000004  /* IRQ18 */
#define IRQA_IRQ19  0x00000008  /* IRQ19 */
#define IRQA_IRQ20  0x00000010  /* IRQ20 */
#define IRQA_IRQ21  0x00000020  /* IRQ21 */
#define IRQA_IRQ22  0x00000040  /* IRQ22 */
#define IRQA_IRQ23  0x00000080  /* IRQ23 */
#define IRQA_IRQ24  0x00000100  /* IRQ24 */
#define IRQA_IRQ25  0x00000200  /* IRQ25 */
#define IRQA_IRQ26  0x00000400  /* IRQ26 */
#define IRQA_IRQ27  0x00000800  /* IRQ27 */
#define IRQA_IRQ28  0x00001000  /* IRQ28 */
#define IRQA_IRQ29  0x00002000  /* IRQ29 */
#define IRQA_IRQ30  0x00004000  /* IRQ30 */
#define IRQA_IRQ31  0x00008000  /* IRQ31 */

#define IRQB_IRQ32  0x00000001  /* IRQ32 */
#define IRQB_IRQ33  0x00000002  /* IRQ33 */
#define IRQB_IRQ34  0x00000004  /* IRQ34 */
#define IRQB_IRQ35  0x00000008  /* IRQ35 */
#define IRQB_IRQ36  0x00000010  /* IRQ36 */
#define IRQB_IRQ37  0x00000020  /* IRQ37 */
#define IRQB_IRQ38  0x00000040  /* IRQ38 */
#define IRQB_IRQ39  0x00000080  /* IRQ39 */
#define IRQB_IRQ40  0x00000100  /* IRQ40 */
#define IRQB_IRQ41  0x00000200  /* IRQ41 */
#define IRQB_IRQ42  0x00000400  /* IRQ42 */
#define IRQB_IRQ43  0x00000800  /* IRQ43 */
#define IRQB_IRQ44  0x00001000  /* IRQ44 */
#define IRQB_IRQ45  0x00002000  /* IRQ45 */
#define IRQB_IRQ46  0x00004000  /* IRQ46 */
#define IRQB_IRQ47  0x00008000  /* IRQ47 */

#define IRQC_IRQ48  0x00000001  /* IRQ48 */
#define IRQC_IRQ49  0x00000002  /* IRQ49 */
#define IRQC_IRQ50  0x00000004  /* IRQ50 */
#define IRQC_IRQ51  0x00000008  /* IRQ51 */
#define IRQC_IRQ52  0x00000010  /* IRQ52 */
#define IRQC_IRQ53  0x00000020  /* IRQ53 */
#define IRQC_IRQ54  0x00000040  /* IRQ54 */
#define IRQC_IRQ55  0x00000080  /* IRQ55 */
#define IRQC_IRQ56  0x00000100  /* IRQ56 */
#define IRQC_IRQ57  0x00000200  /* IRQ57 */
#define IRQC_IRQ58  0x00000400  /* IRQ58 */
#define IRQC_IRQ59  0x00000800  /* IRQ59 */
#define IRQC_IRQ60  0x00001000  /* IRQ60 */
#define IRQC_IRQ61  0x00002000  /* IRQ61 */
#define IRQC_IRQ62  0x00004000  /* IRQ62 */
#define IRQC_IRQ63  0x00008000  /* IRQ63 */

/* bit field of IDM register */
#define IDM_INT_L_L 0x00000000  /* level sensing, interrupt occurs when 'L' */
#define IDM_INT_L_H 0x0000AAAA  /* level sensing, interrupt occurs when 'H' */
#define IDM_INT_E_F 0x00005555  /* edge sensing, interrupt occurs when falling edge */
#define IDM_INT_E_R 0x0000FFFF  /* edge sensing, interrupt occurs when rising edge */

#define IDM_IDM34   0x00000004  /* IRQ34, IRQ35 */
#define IDM_IDM36   0x00000010  /* IRQ36, IRQ37 */
#define IDM_IDM38   0x00000040  /* IRQ38, IRQ39 */
#define IDM_IDM40   0x00000100  /* IRQ40, IRQ41 */
#define IDM_IDM42   0x00000400  /* IRQ42, IRQ43 */
#define IDM_IDMP34  0x00000008  /* IRQ34, IRQ35 */
#define IDM_IDMP36  0x00000020  /* IRQ36, IRQ37 */
#define IDM_IDMP38  0x00000080  /* IRQ38, IRQ39 */
#define IDM_IDMP40  0x00000200  /* IRQ40, IRQ41 */
#define IDM_IDMP42  0x00000800  /* IRQ42, IRQ43 */

#define IDM_IRQ33   0x00000003	/* IRQ33 */
#define IDM_IRQ34   0x0000000C	/* IRQ34 */
#define IDM_IRQ35   0x0000000C	/* IRQ35 */
#define IDM_IRQ36   0x00000030	/* IRQ36 */
#define IDM_IRQ37   0x00000030	/* IRQ37 */
#define IDM_IRQ38   0x000000C0	/* IRQ38 */
#define IDM_IRQ39   0x000000C0	/* IRQ39 */
#define IDM_IRQ40   0x00000300	/* IRQ40 */
#define IDM_IRQ41   0x00000300	/* IRQ41 */
#define IDM_IRQ42   0x00000C00	/* IRQ42 */
#define IDM_IRQ43   0x00000C00	/* IRQ43 */

/* bit field of EXILC register */
#define ILC_INT_LV1 0x11111111  /* interrupt level 1 */
#define ILC_INT_LV2 0x22222222  /* interrupt level 2 */
#define ILC_INT_LV3 0x33333333  /* interrupt level 3 */
#define ILC_INT_LV4 0x44444444  /* interrupt level 4 */
#define ILC_INT_LV5 0x55555555  /* interrupt level 5 */
#define ILC_INT_LV6 0x66666666  /* interrupt level 6 */
#define ILC_INT_LV7 0x77777777  /* interrupt level 7 */
#define ILC_ILC16   0x00000007  /* IRQ16, IRQ17 */
#define ILC_ILC18   0x00000070  /* IRQ18, IRQ19 */
#define ILC_ILC20   0x00000700  /* IRQ20, IRQ21 */
#define ILC_ILC36   0x00000700  /* IRQ20, IRQ21 */
#define ILC_ILC22   0x00007000  /* IRQ22, IRQ23 */
#define ILC_ILC38   0x00007000  /* IRQ22, IRQ23 */
#define ILC_ILC24   0x00070000  /* IRQ24, IRQ25 */
#define ILC_ILC26   0x00700000  /* IRQ26, IRQ27 */
#define ILC_ILC28   0x07000000  /* IRQ28, IRQ29 */
#define ILC_ILC30   0x70000000  /* IRQ30, IRQ31 */
#define ILC_ILC32   0x00000007  /* IRQ32, IRQ33 */
#define ILC_ILC34   0x00000070  /* IRQ34, IRQ35 */

/*****************************************************/
/*    interrupt number                               */
/*****************************************************/
#define INT_SYSTEM_TIMER    0
#define INT_WDT             1
#define INT_IVT             2
#define INT_GPIOA           4
#define INT_GPIOB           5
#define INT_GPIOC           6
#define INT_GPIOD           7
#define INT_SOFTIRQ         8
#define INT_UART0			9
#define INT_SIO0            10
#define INT_AD              11
#define INT_UART1			12
#define INT_SPI0            13
#define INT_TIMER0          16
#define INT_TIMER1          17
#define INT_TIMER2          18
#define INT_TIMER3          19
#define INT_TIMER4          20
#define INT_TIMER5          21
#define INT_DMA0            24
#define INT_DMA1            25
#define INT_GPIOE           28
#define INT_GPIOF           29
#define INT_EXINT1			34
#define INT_EXINT2			36
#define INT_EXINT3			38
#define INT_EXINT4			40

/****************************************************/
/*		I2C control register						*/
/****************************************************/
#define I2C_BASE	0xB7800C00
#define	I2CSADR		(I2C_BASE+0x00)	/* I2C slave address register */
#define I2CCTL		(I2C_BASE+0x04)	/* I2C control register */
#define I2CSR		(I2C_BASE+0x08)	/* I2C status register */
#define I2CDR		(I2C_BASE+0x0C)	/* I2C deta register */
#define I2CMON		(I2C_BASE+0x10)	/* I2C bus monitor register */
#define I2CBC		(I2C_BASE+0x14)	/* I2C bus trans speed set counter */

/* bit field of I2CCTL register */
#define I2CCTL_I2CMD		0x00000003
#define I2CCTL_I2CRSTA		0x00000004
#define I2CCTL_I2CTXAK		0x00000008
#define I2CCTL_I2CMTX		0x00000010
#define I2CCTL_I2CMSTA		0x00000020
#define I2CCTL_I2CAASIE		0x00000040
#define I2CCTL_I2CMEN		0x00000080
#define I2CCTL_I2CALIE		0x00000100
#define I2CCTL_I2CCFIE		0x00000200
#define I2CCTL_I2CSTPIE		0x00000800
#define I2CCTL_I2CDR_LDEN	0x00001000
#define I2CCTL_I2CCS		0x00002000

/* bit field of I2CSR register */
#define I2CSR_I2CRXAK	0x00000001
#define I2CSR_I2CMIF	0x00000002
#define I2CSR_I2CSRW	0x00000004
#define I2CSR_I2CDR_LD	0x00000008
#define I2CSR_I2CMAL	0x00000010
#define I2CSR_I2CMBB	0x00000020
#define I2CSR_I2CMAAS	0x00000040
#define I2CSR_I2CMCF	0x00000080
#define I2CSR_I2CSTP	0x00000200

/****************************************************/
/*		I2S control register						*/
/****************************************************/
#define I2S_BASE	0xB7900000
#define I2SFIFOO	(I2S_BASE+0x00)	/* I2S FIFO Output register */
#define I2SCONO		(I2S_BASE+0x04)	/* I2S control output */
#define I2SCLKO		(I2S_BASE+0x08)	/* I2S clk control output */
#define I2SAFRO		(I2S_BASE+0x0C)	/* I2S almost full register output */
#define I2SAERO		(I2S_BASE+0x10)	/* I2S almost empty register output */
#define I2SIMRO		(I2S_BASE+0x14)	/* I2S interrupt mask register output */
#define I2SISTO		(I2S_BASE+0x18)	/* I2S interrupt status register output */
#define I2SLRSO		(I2S_BASE+0x1C)	/* I2S left/right status & FIFO level register output */
#define I2SFIFOI	(I2S_BASE+0x20)	/* I2S FIFO input register */
#define I2SCONI		(I2S_BASE+0x24)	/* I2S control input */
#define I2SCLKI		(I2S_BASE+0x28)	/* I2S clk control input */
#define I2SAFRI		(I2S_BASE+0x2C)	/* I2S almost full register input */
#define I2SAERI		(I2S_BASE+0x30)	/* I2S almost empty register input */
#define I2SIMRI		(I2S_BASE+0x34)	/* I2S interrupt mask register input */
#define I2SISTI		(I2S_BASE+0x38)	/* I2S interrupt status register input */
#define I2SLRSI		(I2S_BASE+0x3C)	/* I2S left/right status & FIFO level register input */

/* bit field of I2SCONO register */
#define	I2SCONO_TSET_ISSFSO	0x00000003
#define	I2SCONO_TSET_ISMSB	0x00000004
#define	I2SCONO_TSET_ISWSL	0x00000008
#define	I2SCONO_TSET_ISDLY	0x00000010
#define	I2SCONO_TSET_ISRUN	0x00000020
#define	I2SCONO_TSET_ISSFO	0x000000C0
#define	I2SCONO_TSET_ISSCKF	0x00000100
#define	I2SCONO_TSET_ISBIT	0x00000600
#define	I2SCONO_TSET_AFO	0x00001800
#define	I2SCONO_TSET_STOP	0x00002000

/* bit field of I2SCONI register */
#define	I2SCONI_RSET_ISSFSO	0x00000003
#define	I2SCONI_RSET_ISMSB	0x00000004
#define	I2SCONI_RSET_ISWSL	0x00000008
#define	I2SCONI_RSET_ISDLY	0x00000010
#define	I2SCONI_RSET_ISRUN	0x00000020
#define	I2SCONI_RSET_ISSFO	0x000000C0
#define	I2SCONI_RSET_ISSCKF	0x00000100
#define	I2SCONI_RSET_ISBIT	0x00000600
#define	I2SCONI_RSET_AFO	0x00001800
#define	I2SCONI_RSET_STOP	0x00002000

/* bit field of I2SCLKO register */
#define	I2SCLKO_TSET_ISCLR	0x00000001
#define	I2SCLKO_TSET_ISMST	0x00000004

/* bit field of I2SCLKI register */
#define	I2SCLKI_RSET_ISCLR	0x00000001
#define	I2SCLKI_RSET_ISMST	0x00000004

/* bit field of I2SLRSO register */
#define	I2SLRSO_TMON_LR		0x00000001
#define	I2SLRSO_TFIFOLVL	0x0001FF00

/* bit field of I2SLRSI register */
#define	I2SLRSI_RMON_LR		0x00000001
#define	I2SLRSI_RFIFOLVL	0x0001FF00

/****************************************************/
/*		SPI control register						*/
/****************************************************/
#define SPI0_BASE	0xB7B02000
#define	SPCR0		(SPI0_BASE+0x00) /* SPI control register 0 */
#define SPBRR0		(SPI0_BASE+0x04) /* SPI baudrate register 0 */
#define SPSR0		(SPI0_BASE+0x08) /* SPI status register 0 */
#define SPDWR0		(SPI0_BASE+0x0C) /* SPI write data register 0 */
#define SPDRR0		(SPI0_BASE+0x10) /* SPI read data register 0 */
#define TEST0		(SPI0_BASE+0x14) /* for test 0 */

/* bit field of SPCR0 register */
#define SPCR0_SPE		0x00000001
#define SPCR0_MSTR		0x00000002
#define SPCR0_MODFEN	0x00000008
#define SPCR0_LSBF		0x00000010
#define SPCR0_CPHA		0x00000020
#define SPCR0_CPOL		0x00000040
#define SPCR0_TFIE		0x00000100
#define SPCR0_RFIE		0x00000200
#define SPCR0_FIE		0x00000400
#define SPCR0_ORIE		0x00000800
#define SPCR0_MDFIE		0x00001000
#define SPCR0_TFIC		0x000F0000
#define SPCR0_RFIC		0x00F00000
#define SPCR0_FICLR		0x01000000
#define SPCR0_SSZ		0x02000000
#define SPCR0_SOZ		0x04000000
#define SPCR0_MOZ		0x08000000

/* bit field of SPSR0 register */
#define SPSR0_TFI		0x00000001
#define SPSR0_RFI		0x00000002
#define SPSR0_FI		0x00000004
#define SPSR0_ORF		0x00000008
#define SPSR0_MDF		0x00000010
#define SPSR0_SPIF		0x00000020
#define SPSR0_TFD		0x000007C0
#define SPSR0_RFD		0x0000F800
#define SPSR0_WOF		0x00010000
#define SPSR0_TFF		0x00020000
#define SPSR0_TFE		0x00040000
#define SPSR0_RFF		0x00080000
#define SPSR0_RFE		0x00100000

/* bit field of SPBRR0 register */
#define SPBRR0_SPBR		0x000003FF
#define SPBRR0_SIZE		0x00000400
#define SPBRR0_LEAD		0x00003000
#define SPBRR0_LAG		0x0000C000
#define SPBRR0_DTL		0x01FF0000

#define SPI1_BASE	0xB7B03000
#define	SPCR1		(SPI1_BASE+0x00) /* SPI control register 1 */
#define SPBRR1		(SPI1_BASE+0x04) /* SPI baudrate register 1 */
#define SPSR1		(SPI1_BASE+0x08) /* SPI status register 1 */
#define SPDWR1		(SPI1_BASE+0x0C) /* SPI write data register 1 */
#define SPDRR1		(SPI1_BASE+0x10) /* SPI read data register 1 */
#define TEST1		(SPI1_BASE+0x14) /* for test 1 */

/* bit field of SPCR1 register */
#define SPCR1_SPE		0x00000001
#define SPCR1_MSTR		0x00000002
#define SPCR1_MODFEN	0x00000008
#define SPCR1_LSBF		0x00000010
#define SPCR1_CPHA		0x00000020
#define SPCR1_CPOL		0x00000040
#define SPCR1_TFIE		0x00000100
#define SPCR1_RFIE		0x00000200
#define SPCR1_FIE		0x00000400
#define SPCR1_ORIE		0x00000800
#define SPCR1_MDFIE		0x00001000
#define SPCR1_TFIC		0x000F0000
#define SPCR1_RFIC		0x00F00000
#define SPCR1_FICLR		0x01000000
#define SPCR1_SSZ		0x02000000
#define SPCR1_SOZ		0x04000000
#define SPCR1_MOZ		0x08000000

/* bit field of SPSR0 register */
#define SPSR1_TFI		0x00000001
#define SPSR1_RFI		0x00000002
#define SPSR1_FI		0x00000004
#define SPSR1_ORF		0x00000008
#define SPSR1_MDF		0x00000010
#define SPSR1_SPIF		0x00000020
#define SPSR1_TFD		0x000007C0
#define SPSR1_RFD		0x0000F800
#define SPSR1_WOF		0x00010000
#define SPSR1_TFF		0x00020000
#define SPSR1_TFE		0x00040000
#define SPSR1_RFF		0x00080000
#define SPSR1_RFE		0x00100000

/* bit field of SPBRR1 register */
#define SPBRR1_SPBR		0x000003FF
#define SPBRR1_SIZE		0x00000400
#define SPBRR1_LEAD		0x00003000
#define SPBRR1_LAG		0x0000C000
#define SPBRR1_DTL		0x01FF0000

/****************************************************/
/*		RTC control register						*/
/****************************************************/
#define RTC_BASE	0xB7C00000
#define	S1			(RTC_BASE+0x00) /* 1 second register */
#define	S10			(RTC_BASE+0x04) /* 10 second register */
#define	M1			(RTC_BASE+0x08) /* 1 minute register */
#define	M10			(RTC_BASE+0x0C) /* 10 minute register */
#define	H1			(RTC_BASE+0x10) /* 1 hour register */
#define	H10			(RTC_BASE+0x14) /* 10 hour register */
#define	D1			(RTC_BASE+0x18) /* 1 day register */
#define	D10			(RTC_BASE+0x1C) /* 10 day register */
#define	MO1			(RTC_BASE+0x20) /* 1 month register */
#define	MO10		(RTC_BASE+0x24) /* 10 month register */
#define	Y1			(RTC_BASE+0x28) /* 1 year register */
#define	Y10			(RTC_BASE+0x2C) /* 10 yaer register */
#define	W			(RTC_BASE+0x30) /* week register */
#define	CD			(RTC_BASE+0x34) /* control register D */
#define	CE			(RTC_BASE+0x38) /* control register E */
#define	CF			(RTC_BASE+0x3C) /* control register F */

/* bit field of CF register */
#define CF_RESET_ON		0x01
#define CF_STOP_ON		0x02
#define CF_TEST			0x00

#endif  /* End of ML674061.h */
