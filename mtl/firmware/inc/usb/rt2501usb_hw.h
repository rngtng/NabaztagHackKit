/**
 * @file rt2501usb_hw.h
 * @author Sebastien Bourdeauducq - 2006 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief RT2501 registers
 */
#ifndef _RT2501_HW_H_
#define _RT2501_HW_H_

//#pragma pack(1)

/* Sitecom dongle */
#define RT2501_VENDORID1		0x0df6
#define RT2501_PRODUCTID1		0x9712

/* MSI module */
#define RT2501_VENDORID2		0x148f
#define RT2501_PRODUCTID2		0x2573

/* MSI module, strange version */
#define RT2501_VENDORID3		0x0db0
#define RT2501_PRODUCTID3		0x6877

/* RT2501 USB requests */
#define RT2501_READMULTIMAC		0x07 //0x03
#define RT2501_WRITEMULTIMAC	0x06
#define RT2501_READEEPROM		  0x09

/* 8051 firmware image */
#define RT2501_FIRMWARE_IMAGE_BASE     0x800
#define RT2501_MAX_FIRMWARE_IMAGE_SIZE 2048

#define RT2501_RFIC_5226		1
#define RT2501_RFIC_2528		2
#define RT2501_RFIC_5225		3
#define RT2501_RFIC_2527		4

#define RT2501_SOFTWARE_DIVERSITY	0
#define RT2501_ANTENNA_A		1
#define RT2501_ANTENNA_B		2
#define RT2501_HARDWARE_DIVERSITY	3

#define	RT2501_RATE_1			0
#define	RT2501_RATE_2			1
#define	RT2501_RATE_5_5			2
#define	RT2501_RATE_11			3
/* OFDM rates */
#define RT2501_RATE_6			4
#define RT2501_RATE_9			5
#define RT2501_RATE_12			6
#define RT2501_RATE_18			7
#define RT2501_RATE_24			8
#define RT2501_RATE_36			9
#define RT2501_RATE_48			10
#define RT2501_RATE_54			11

/*
   On-chip BEACON frame space - base address 0x2400
*/
#define RT2501_HW_BEACON_BASE0         0x2400
#define RT2501_HW_BEACON_BASE1         0x2500
#define RT2501_HW_BEACON_BASE2         0x2600
#define RT2501_HW_BEACON_BASE3         0x2700

/*
   Security key table memory, base address = 0x1000
*/
#define RT2501_SHARED_KEY_TABLE_BASE       0x1000 /* 32-byte * 16-entry = 512-byte */
#define RT2501_PAIRWISE_KEY_TABLE_BASE     0x1200 /* 32-byte * 64-entry = 2048-byte */
#define RT2501_PAIRWISE_TA_TABLE_BASE      0x1a00 /* 8-byte * 64-entry = 512-byte */

/*
   MAC Control Registers - base address 0x3000
*/
#define RT2501_MAC_CSR0            0x3000
#define RT2501_MAC_CSR1            0x3004
#define RT2501_MAC_CSR2            0x3008
#define RT2501_MAC_CSR3            0x300c
#define RT2501_MAC_CSR4            0x3010
#define RT2501_MAC_CSR5            0x3014
#define RT2501_MAC_CSR6            0x3018
#define RT2501_MAC_CSR7            0x301c
#define RT2501_MAC_CSR8            0x3020  /* SIFS/EIFS */
#define RT2501_MAC_CSR9            0x3024
#define RT2501_MAC_CSR10           0x3028  /* power state configuration */
#define RT2501_MAC_CSR11           0x302c  /* Power state transition time */
#define RT2501_MAC_CSR12           0x3030  /* power state */
#define RT2501_MAC_CSR13           0x3034  /* GPIO */
#define RT2501_MAC_CSR14           0x3038  /* LED control */
#define RT2501_MAC_CSR15           0x303c  /* NAV control */


#define	RT2501_NUM_EEPROM_BBP_PARMS		19
#define	RT2501_NUM_EEPROM_TX_G_PARMS		7
#define	RT2501_NUM_EEPROM_BBP_TUNING_PARMS	7

#define RT2501_EEPROM_VERSION_OFFSET       	0x2
#define	RT2501_EEPROM_MAC_ADDRESS_BASE_OFFSET	0x4
#define	RT2501_EEPROM_BBP_BASE_OFFSET		0x20
#define	RT2501_EEPROM_G_TX_PWR_OFFSET		0x46
#define	RT2501_EEPROM_FREQ_OFFSET		0x5e
#define RT2501_EEPROM_LED_OFFSET		0x60
#define RT2501_EEPROM_A_TX_PWR_OFFSET      	0x62
#define	RT2501_EEPROM_TSSI_REF_OFFSET		0x4A
#define	RT2501_EEPROM_TSSI_DELTA_OFFSET		0x1A
#define	RT2501_EEPROM_MAC_STATUS_OFFSET		0x7E
#define	RT2501_EEPROM_RSSI_BG_OFFSET		0x9a
#define	RT2501_EEPROM_RSSI_A_OFFSET		0x9c
#define	RT2501_EEPROM_BG_TSSI_CALIBRAION	0x54
#define	RT2501_EEPROM_A_TSSI_CALIBRAION		0x90
#define RT2501_EEPROM_TXPOWER_DELTA_OFFSET 	0x9e

/*
   TXRX control registers - base address 0x3000
*/
#define RT2501_TXRX_CSR0           0x3040
#define RT2501_TXRX_CSR1           0x3044
#define RT2501_TXRX_CSR2           0x3048
#define RT2501_TXRX_CSR3           0x304c
#define RT2501_TXRX_CSR4           0x3050
#define RT2501_TXRX_CSR5           0x3054
#define RT2501_TXRX_CSR6           0x3058  /* ACK/CTS payload consumed time */
#define RT2501_TXRX_CSR7           0x305c  /* ACK/CTS payload consumed time */
#define RT2501_TXRX_CSR8           0x3060  /* ACK/CTS payload consumed time */
#define RT2501_TXRX_CSR9           0x3064  /* BEACON SYNC */
#define RT2501_TXRX_CSR10          0x3068  /* BEACON alignment */
#define RT2501_TXRX_CSR11          0x306c  /* AES mask */
#define RT2501_TXRX_CSR12          0x3070  /* TSF low 32 */
#define RT2501_TXRX_CSR13          0x3074  /* TSF high 32 */
#define RT2501_TXRX_CSR14          0x3078  /* TBTT timer */
#define RT2501_TXRX_CSR15          0x307c  /* TKIP MIC priority byte "AND" mask */

/*
   PHY control registers - base address 0x3000
*/
#define RT2501_PHY_CSR0            0x3080  /* RF/PS control */
#define RT2501_PHY_CSR1            0x3084
#define RT2501_PHY_CSR2            0x3088  /* pre-TX BBP control */
#define RT2501_PHY_CSR3            0x308c  /* BBP access */
#define RT2501_PHY_CSR4            0x3090  /* RF serial control */
#define RT2501_PHY_CSR5            0x3094  /* RX to TX signal switch timing control */
#define RT2501_PHY_CSR6            0x3098  /* TX to RX signal timing control */
#define RT2501_PHY_CSR7            0x309c  /* TX DAC switching timing control */

/*
   Security control register - base address 0x3000
*/
#define RT2501_SEC_CSR0            0x30a0  /* shared key table control */
#define RT2501_SEC_CSR1            0x30a4  /* shared key table security mode */
#define RT2501_SEC_CSR2            0x30a8  /* pairwise key table valid bitmap 0 */
#define RT2501_SEC_CSR3            0x30ac  /* pairwise key table valid bitmap 1 */
#define RT2501_SEC_CSR4            0x30b0  /* pairwise key table lookup control */
#define RT2501_SEC_CSR5            0x30b4  /* shared key table security mode */

/*
   STA control registers - base address 0x3000
*/
#define STA_CSR0            0x30c0  /* CRC/PLCP error counter */
#define STA_CSR1            0x30c4  /* Long/False-CCA error counter */
#define STA_CSR2            0x30c8  /* RX FIFO overflow counter */
#define STA_CSR3            0x30cc  /* TX Beacon counter */
#define STA_CSR4            0x30d0  /* TX Retry (1) Counters */
#define STA_CSR5            0x30d4  /* TX Retry (2) Counters */

/*
   QOS control registers - base address 0x3000
*/
#define QOS_CSR0            0x30e0  /* TXOP holder MAC address 0 */
#define QOS_CSR1            0x30e4  /* TXOP holder MAC address 1 */
#define QOS_CSR2            0x30e8  /* TXOP holder timeout register */
#define QOS_CSR3            0x30ec  /* RX QOS-CFPOLL MAC address 0 */
#define QOS_CSR4            0x30f0  /* RX QOS-CFPOLL MAC address 1 */
#define QOS_CSR5            0x30f4  /* "QosControl" field of the RX QOS-CFPOLL */

/*
   WMM Scheduler Register
*/

#define RT2501_AIFSN_CSR               	0x0400
#define RT2501_CWMIN_CSR		0x0404
#define RT2501_CWMAX_CSR           	0x0408
#define RT2501_AC_TXOP_CSR0        	0x040c
#define RT2501_AC_TXOP_CSR1        	0x0410

/*
   BBP & RF definitions
*/

#define	RT2501_BBP_R0			   0
#define	RT2501_BBP_R1			   1
#define	RT2501_BBP_R2          		   2
#define RT2501_BBP_R3                      3
#define RT2501_BBP_R4                      4
#define RT2501_BBP_R5                      5
#define RT2501_BBP_R6                      6
#define	RT2501_BBP_R14			   14
#define RT2501_BBP_R16                     16
#define RT2501_BBP_R17                     17
#define RT2501_BBP_R18                     18
#define RT2501_BBP_R21                     21
#define RT2501_BBP_R22                     22
#define RT2501_BBP_R32                     32
#define RT2501_BBP_R62                     62
#define RT2501_BBP_R64                     64
#define RT2501_BBP_R66                     66
#define RT2501_BBP_R70                     70
#define RT2501_BBP_R77                     77
#define RT2501_BBP_R82                     82
#define RT2501_BBP_R83                     83
#define RT2501_BBP_R84                     84
#define RT2501_BBP_R94                     94

#define BBPR94_DEFAULT                     0x06

#define RT2501_MICS_OFFSET		   16
#define RT2501_KEY_ENTRY_SIZE              32

typedef	union	_MAC_CSR2_STRUC	{
	struct	{
		uint8_t		Byte0;
		uint8_t		Byte1;
		uint8_t		Byte2;
		uint8_t		Byte3;
	}	field;
	uint32_t			word;
}	MAC_CSR2_STRUC, *PMAC_CSR2_STRUC;

typedef	union	_MAC_CSR3_STRUC	{
	struct	{
		uint8_t		Byte4;
		uint8_t		Byte5;
		uint8_t		U2MeMask;
		uint8_t		Rsvd1;
	}	field;
	uint32_t			word;
}	MAC_CSR3_STRUC, *PMAC_CSR3_STRUC;

typedef	union	_MAC_CSR4_STRUC	{
	struct	{
		uint8_t		Byte0;		// BSSID byte 0
		uint8_t		Byte1;		// BSSID byte 1
		uint8_t		Byte2;		// BSSID byte 2
		uint8_t		Byte3;		// BSSID byte 3
	}	field;
	uint32_t			word;
}	MAC_CSR4_STRUC, *PMAC_CSR4_STRUC;

typedef	union	_MAC_CSR5_STRUC	{
	struct	{
		uint8_t		Byte4;		 // BSSID byte 4
		uint8_t		Byte5;		 // BSSID byte 5
		uint16_t  BssIdMask:2; // 11: one BSSID, 00: 4 BSSID, 10 or 01: 2 BSSID
		uint16_t	Rsvd:14;
	}	field;
	uint32_t			word;
}	MAC_CSR5_STRUC, *PMAC_CSR5_STRUC;

typedef	union	_MAC_CSR12_STRUC	{
	struct	{
		uint32_t		CurrentPowerState:1; /* 0:sleep, 1:awake */
		uint32_t    PutToSleep:1;
		uint32_t    ForceWakeup:1;/* ForceWake has higher privilege than PutToSleep when both set */
		uint32_t		BbpRfStatus:1; /* 0: not ready, 1:ready */
		uint32_t		:28;
	}	field;
	uint32_t			word;
}	MAC_CSR12_STRUC, *PMAC_CSR12_STRUC;

typedef	union	_TXRX_CSR9_STRUC	{
	struct	{
		uint32_t    BeaconInterval:16; /* in unit of 1/16 TU */
		uint32_t		bTsfTicking:1; /* Enable TSF auto counting */
		uint32_t		TsfSyncMode:2; /* Enable TSF sync, 00: disable, 01: infra mode, 10: ad-hoc mode */
		uint32_t    bTBTTEnable:1;
		uint32_t		bBeaconGen:1; /* Enable beacon generator */
		uint32_t    :3;
		uint32_t		TxTimestampCompensate:8;
	}	field;
	uint32_t			word;
}	TXRX_CSR9_STRUC, *PTXRX_CSR9_STRUC;

typedef	union	_PHY_CSR3_STRUC	{
	struct	{
		uint32_t		Value:8; /* Register value to program into BBP */
		uint32_t		RegNum:7; /* Selected BBP register */
		uint32_t		fRead:1; /* 0: Write BBP, 1: Read BBP */
		uint32_t		Busy:1; /* 1: ASIC is busy doing BBP programming. */
		uint32_t		:15;
	}	field;
	uint32_t			word;
}	PHY_CSR3_STRUC, *PPHY_CSR3_STRUC;

typedef	union	_PHY_CSR4_STRUC	{
	struct	{
		uint32_t		RFRegValue:24; /* Register value (include register id) serial out to RF/IF chip. */
		uint32_t		NumberOfBits:5; /* Number of bits used in RFRegValue (I:20, RFMD:22) */
		uint32_t		IFSelect:1; /* 1: select IF to program, 0: select RF to program */
		uint32_t		PLL_LD:1; /* RF PLL_LD status */
		uint32_t		Busy:1; /* 1: ASIC is busy executing RF programming. */
	}	field;
	uint32_t			word;
}	PHY_CSR4_STRUC, *PPHY_CSR4_STRUC;

/*
   E2PROM data layout
*/

typedef	union	_EEPROM_ANTENNA_STRUC	{
	struct	{
		uint16_t		NumOfAntenna:2;
		uint16_t		TxDefaultAntenna:2; /* default of antenna, 0: diversity, 1:antenna-A, 2:antenna-B reserved (default = 0) */
		uint16_t		RxDefaultAntenna:2; /* default of antenna, 0: diversity, 1:antenna-A, 2:antenna-B reserved (default = 0) */
		uint16_t		FrameType:1; /* 0: DPDT , 1: SPDT , noted this bit is valid for g only.	*/
		uint16_t		Rsv:2;
		uint16_t    DynamicTxAgcControl:1;
		uint16_t		HardwareRadioControl:1;	/* 1: Hardware controlled radio enabled, Read GPIO0 required. */
		uint16_t    RfIcType:5;
	}	field;
	uint16_t			word;
}	EEPROM_ANTENNA_STRUC, *PEEPROM_ANTENNA_STRUC;

typedef	union	_EEPROM_NIC_CINFIG2_STRUC	{
	struct	{
		uint16_t		Rsv1:4;
		uint16_t		ExternalLNA:1;
		uint16_t    Rsv2:11;
	}	field;
	uint16_t			word;
}	EEPROM_NIC_CONFIG2_STRUC, *PEEPROM_NIC_CONFIG2_STRUC;

typedef	union	_EEPROM_TX_PWR_STRUC	{
	struct	{
		uint8_t	    Byte0; /* Low Byte */
		uint8_t	    Byte1; /* High Byte */
	}	field;
	uint16_t	    word;
}	EEPROM_TX_PWR_STRUC, *PEEPROM_TX_PWR_STRUC;

typedef	union	_EEPROM_VERSION_STRUC	{
	struct	{
		uint8_t	    FaeReleaseNumber; /* Low Byte */
		uint8_t	    Version; /* High Byte */
	}	field;
	uint16_t	    word;
}	EEPROM_VERSION_STRUC, *PEEPROM_VERSION_STRUC;

typedef	union	_EEPROM_LED_STRUC	{
	struct	{
		uint16_t	PolarityRDY_G:1;
		uint16_t	PolarityRDY_A:1;
		uint16_t	PolarityACT:1;
		uint16_t	PolarityGPIO_0:1;
		uint16_t	PolarityGPIO_1:1;
		uint16_t	PolarityGPIO_2:1;
		uint16_t	PolarityGPIO_3:1;
		uint16_t	PolarityGPIO_4:1;
		uint16_t	LedMode:5;
		uint16_t	Rsvd:3;
	}	field;
	uint16_t	  word;
}	EEPROM_LED_STRUC, *PEEPROM_LED_STRUC;

typedef	union	_EEPROM_TXPOWER_DELTA_STRUC	{
	struct	{
		uint8_t	DeltaValue:6; /* Tx Power delta value (MAX=4) */
		uint8_t	Type:1; /* 1: plus the delta value, 0: minus the delta value */
		uint8_t	TxPowerEnable:1;
	}	field;
	uint8_t	value;
}	EEPROM_TXPOWER_DELTA_STRUC, *PEEPROM_TXPOWER_DELTA_STRUC;

/* calibrate every 4s */
#define RT2501_CAL_PERIOD 40

/* RX/TX Descriptor */

typedef	struct	_RXD_STRUC	{
	/* Word	0 */
	uint32_t	Owner:1; /* 1: owned by ASIC, 0: owned by HOST driver */
	uint32_t	Drop:1;	 /* 1: drop without receiving to HOST */
	uint32_t	U2M:1; /* 1: this RX frame is unicast to me */
	uint32_t	Mcast:1; /* 1: this is a multicast frame */
	uint32_t	Bcast:1; /* 1: this is a broadcast frame */
	uint32_t	MyBss:1; /* 1: this frame belongs to the same BSSID */
	uint32_t	Crc:1; /* 1: CRC error */
	uint32_t	Ofdm:1; /* 1: this frame is received in OFDM rate */
	uint32_t	CipherErr:2; /* 0: decryption okay, 1:ICV error, 2:MIC error, 3:KEY not valid */
	uint32_t	KeyIndex:6; /* decryption key actually used */
	uint32_t	DataByteCnt:12;
	uint32_t	Rsv:1;
	uint32_t	CipherAlg:3;

	/* Word 1 */
	uint32_t	PlcpSignal:8; /* RX raw data rate reported by BBP */
	uint32_t	PlcpRssi:8; /* RSSI reported by BBP */
	uint32_t	Rsv0:8;
	uint32_t	FrameOffset:7;
	uint32_t	Rsv1:1;

	/* Word	2 */
	uint32_t	Iv; /* received IV if originally encrypted; for replay attack checking */

	/* Word 3 */
	uint32_t	Eiv; /* received EIV if originally encrypted; for replay attack checking */

	/* Word 4 */
	uint32_t	Rsv2;

	/* Word	5 */
	uint32_t	Rsv3; /* BufPhyAddr */
} volatile RXD_STRUC, *PRXD_STRUC;

#define RT2501_CIPHER_NONE		0
#define RT2501_CIPHER_WEP64		1
#define RT2501_CIPHER_WEP128		2
#define RT2501_CIPHER_TKIP		3
#define RT2501_CIPHER_AES		4
#define RT2501_CIPHER_CKIP64		5
#define RT2501_CIPHER_CKIP128		6
#define RT2501_CIPHER_TKIP_NO_MIC	7

typedef	struct	_TXD_STRUC {
	/* Word 0 */
	uint32_t	Burst:1; /* 1: Contiguously used current End Point, eg, Fragment packet should turn on. */
	uint32_t	Drop:1; /* 0: skip this frame, 1:valid frame inside */
	uint32_t	MoreFrag:1; /* 1: More fragment following this frame */
	uint32_t	ACK:1; /* 1: ACK is required */
	uint32_t	Timestamp:1; /* 1: MAC auto overwrite current TSF into frame body */
	uint32_t	Ofdm:1; /* 1: TX using OFDM rates */
	uint32_t	IFS:1; /* 1: require a BACKOFF before this frame, 0:SIFS before this frame */
	uint32_t	RetryMd:1;

	uint32_t	TkipMic:1; /* 1: ASIC is responsible for appending TKIP MIC if TKIP is in use */
	uint32_t	KeyTable:1; /* 1: use per-client pairwise KEY table, 0: shared KEY table */
	/*
	   Key index (0~31) to the pairwise KEY table; or
	   0~3 to shared KEY table 0 (BSS0). STA always use BSS0
	   4~7 to shared KEY table 1 (BSS1)
	   8~11 to shared KEY table 2 (BSS2)
	   12~15 to shared KEY table 3 (BSS3)
	*/
	uint32_t	KeyIndex:6;

	uint32_t	DataByteCnt:12;
	uint32_t	Burst2:1; /* same as "Burst" */
	uint32_t	CipherAlg:3;

	/* Word 1 */
	uint32_t	HostQId:4; /* EDCA/HCCA queue ID */
	uint32_t	Aifsn:4;
	uint32_t	Cwmin:4;
	uint32_t	Cwmax:4;
	uint32_t	IvOffset:6;
	uint32_t	:6;
	uint32_t	HwSeq:1; /* MAC auto replace the 12-bit frame sequence # */
	uint32_t	BufCount:3; /* number of buffers in this TXD */

	/* Word 2 */
	uint32_t	PlcpSignal:8;
	uint32_t	PlcpService:8;
	uint32_t	PlcpLengthLow:8;
	uint32_t	PlcpLengthHigh:8;

	/* Word 3 */
	uint32_t	Iv;

	/* Word 4 */
	uint32_t	Eiv;

	/* Word 5 */
	uint32_t	FrameOffset:8; /* frame start offset inside ASIC TXFIFO (after TXINFO field) */
	uint32_t	PktId:8; /* driver assigned packet ID to categorize TXResult in TxDoneInterrupt */
	uint32_t	BbpTxPower:8;
	uint32_t	bWaitingDmaDoneInt:1; /* pure s/w flag. 1:TXD been filled with data and waiting for TxDoneISR for housekeeping */
	uint32_t	Reserved:7;
}volatile TXD_STRUC, *PTXD_STRUC;

//#pragma pack()

#endif
