/**
 * @file rt2501usb.c
 * @author Sebastien Bourdeauducq - 2006 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief RT2501 Wifi/Network driver
 */
#include <stdio.h>
#include <string.h>

#include "ml674061.h"
#include "ml60842.h"
#include "common.h"

#include "utils/debug.h"
#include "utils/delay.h"
#include "utils/mem.h"


#include "net/ieee80211.h"
#include "net/eapol.h"

#include "usb/hcdmem.h"
#include "usb/hcd.h"
#include "usb/usbctrl.h"
#include "usb/usbh.h"

#include "usb/rt2501usb.h"
#include "usb/rt2501usb_hw.h"
#include "usb/rt2501usb_firmware.h"
#include "usb/rt2501usb_buffer.h"
#include "usb/rt2501usb_io.h"


typedef struct _CHANNEL_TX_POWER {
  uint8_t Channel;
  int8_t Power;
}  CHANNEL_TX_POWER, *PCHANNEL_TX_POWER;

typedef struct  _RT2501_RF_REGS {
  uint8_t Channel;
  uint32_t R1;
  uint32_t R2;
  uint32_t R3;
  uint32_t R4;
}  RT2501_RF_REGS, *PRT2501_RF_REGS;

static char rt2501_connected;

PDEVINFO rt2501_dev;

//static uint32_t rt2501_asicver;

uint8_t rt2501_mac[IEEE80211_ADDR_LEN];
static uint16_t rt2501_eeprom_defaults[RT2501_NUM_EEPROM_BBP_PARMS];
static uint8_t rt2501_BbpRssiToDbmDelta;
static EEPROM_ANTENNA_STRUC rt2501_Antenna;
static uint32_t rt2501_RfFreqOffset; /* Frequency offset for channel switching */
static uint8_t rt2501_auto_tx_agc; /* Enable driver auto Tx Agc control */
static uint8_t rt2501_TSSI_Ref; /* Store Tssi reference value as 25 tempature. */
static uint8_t rt2501_TSSI_PlusBoundary[5]; /* Tssi boundary for increase Tx power to compensate. */
static uint8_t rt2501_TSSI_MinusBoundary[5]; /* Tssi boundary for decrease Tx power to compensate. */
static uint8_t rt2501_TxAgcStep; /* Store Tx TSSI delta increment / decrement value */
static int8_t rt2501_TxAgcCompensate; /* Store the compensation (TxAgcStep * (idx-1)) */
static int8_t rt2501_RssiOffset1; /* Store B/G RSSI#1 Offset value on EEPROM 0x9Ah */
static int8_t rt2501_RssiOffset2; /* Store B/G RSSI#2 Offset value */

static CHANNEL_TX_POWER  rt2501_txpower[RT2501_MAX_NUM_OF_CHANNELS]; /* Store Tx power value for all channels. */
static EEPROM_TXPOWER_DELTA_STRUC rt2501_TxPowerDeltaConfig;
static RT2501_RF_REGS rt2501_LatchRfRegs;

static uint8_t rt2501_channel;

static const struct {
  uint16_t reg;
  uint32_t val;
} rt2501_mac_defaults[] = {
  {RT2501_TXRX_CSR0,  0x025fb032}, /* 0x3040, RX control, default Disable RX */
  {RT2501_TXRX_CSR1,  0x9eaa9eaf}, /* 0x3044, BBP 30:Ant-A RSSI, R51:Ant-B RSSI, R42:OFDM rate, R47:CCK SIGNAL */
  {RT2501_TXRX_CSR2,  0x8a8b8c8d}, /* 0x3048, CCK TXD BBP registers */
  {RT2501_TXRX_CSR3,  0x00858687}, /* 0x304c, OFDM TXD BBP registers */
  {RT2501_TXRX_CSR7,  0x2E31353B}, /* 0x305c, ACK/CTS payload consume time for 18/12/9/6 mbps */
  {RT2501_TXRX_CSR8,  0x2a2a2a2c}, /* 0x3060, ACK/CTS payload consume time for 54/48/36/24 mbps */
  {RT2501_TXRX_CSR15,  0x0000000f}, /* 0x307c, TKIP MIC priority byte "AND" mask */
  {RT2501_MAC_CSR6,  0x00000fff}, /* 0x3018, MAX frame length */
  {RT2501_MAC_CSR8,  0x016c030a}, /* 0x3020, SIFS/EIFS time, set SIFS delay time. */
  {RT2501_MAC_CSR10,  0x00000718}, /* 0x3028, ASIC PIN control in various power states */
  {RT2501_MAC_CSR12,  0x00000004}, /* 0x3030, power state control, set to AWAKE state */
  {RT2501_MAC_CSR13,  0x00007f00}, /* 0x3034, GPIO pin#7 as bHwRadio (input:0), otherwise (output:1) */
  {RT2501_SEC_CSR0,  0x00000000}, /* 0x30a0, invalidate all shared key entries */
  {RT2501_SEC_CSR1,  0x00000000}, /* 0x30a4, reset all shared key algorithm to "none" */
  {RT2501_SEC_CSR5,  0x00000000}, /* 0x30b4, reset all shared key algorithm to "none" */
  {RT2501_PHY_CSR1,  0x000023b0}, /* 0x3084, BBP Register R/W mode set to "Parallel mode" */
  {RT2501_PHY_CSR5,  0x00040a06},
  {RT2501_PHY_CSR6,  0x00080606},
  {RT2501_PHY_CSR7,  0x00000408},
  {RT2501_AIFSN_CSR,  0x00002273},
  {RT2501_CWMIN_CSR,  0x00002344},
  {RT2501_CWMAX_CSR,  0x000034aa},
};

static const struct {
  uint8_t reg;
  uint8_t val;
} rt2501_bbp_defaults[] = {
  {3,   0x80},
  {15,  0x30},
  {17,  0x20},
  {21,  0xc8},
  {22,  0x38},
  {23,  0x06},
  {24,  0xfe},
  {25,  0x0a},
  {26,  0x0d},
  {32,  0x0b},
  {34,  0x12},
  {37,  0x07},
  {39,  0xf8},
  {41,  0x60},
  {53,  0x10},
  {54,  0x18},
  {60,  0x10},
  {61,  0x04},
  {62,  0x04},
  {75,  0xfe},
  {86,  0xfe},
  {88,  0xfe},
  {90,  0x0f},
  {99,  0x00},
  {102,  0x16},
  {107,  0x04},
};

static const RT2501_RF_REGS RF2528RegTable[] = {
/*    ch   R1      R2      R3(TX0~4=0) R4 */
    {1,  0x94002c0c, 0x94000786, 0x94068255, 0x940fea0b},
    {2,  0x94002c0c, 0x94000786, 0x94068255, 0x940fea1f},
    {3,  0x94002c0c, 0x9400078a, 0x94068255, 0x940fea0b},
    {4,  0x94002c0c, 0x9400078a, 0x94068255, 0x940fea1f},
    {5,  0x94002c0c, 0x9400078e, 0x94068255, 0x940fea0b},
    {6,  0x94002c0c, 0x9400078e, 0x94068255, 0x940fea1f},
    {7,  0x94002c0c, 0x94000792, 0x94068255, 0x940fea0b},
    {8,  0x94002c0c, 0x94000792, 0x94068255, 0x940fea1f},
    {9,  0x94002c0c, 0x94000796, 0x94068255, 0x940fea0b},
    {10, 0x94002c0c, 0x94000796, 0x94068255, 0x940fea1f},
    {11, 0x94002c0c, 0x9400079a, 0x94068255, 0x940fea0b},
    {12, 0x94002c0c, 0x9400079a, 0x94068255, 0x940fea1f},
    {13, 0x94002c0c, 0x9400079e, 0x94068255, 0x940fea0b},
    {14, 0x94002c0c, 0x940007a2, 0x94068255, 0x940fea13}
};
#define RT2501_NUM_OF_2528_CHNL (sizeof(RF2528RegTable)/sizeof(RT2501_RF_REGS))

static const RT2501_RF_REGS RF5226RegTable[] = {
/*    ch   R1      R2      R3(TX0~4=0) R4 */
    {1,  0x94002c0c, 0x94000786, 0x94068255, 0x940fea0b},
    {2,  0x94002c0c, 0x94000786, 0x94068255, 0x940fea1f},
    {3,  0x94002c0c, 0x9400078a, 0x94068255, 0x940fea0b},
    {4,  0x94002c0c, 0x9400078a, 0x94068255, 0x940fea1f},
    {5,  0x94002c0c, 0x9400078e, 0x94068255, 0x940fea0b},
    {6,  0x94002c0c, 0x9400078e, 0x94068255, 0x940fea1f},
    {7,  0x94002c0c, 0x94000792, 0x94068255, 0x940fea0b},
    {8,  0x94002c0c, 0x94000792, 0x94068255, 0x940fea1f},
    {9,  0x94002c0c, 0x94000796, 0x94068255, 0x940fea0b},
    {10, 0x94002c0c, 0x94000796, 0x94068255, 0x940fea1f},
    {11, 0x94002c0c, 0x9400079a, 0x94068255, 0x940fea0b},
    {12, 0x94002c0c, 0x9400079a, 0x94068255, 0x940fea1f},
    {13, 0x94002c0c, 0x9400079e, 0x94068255, 0x940fea0b},
    {14, 0x94002c0c, 0x940007a2, 0x94068255, 0x940fea13},
};
#define RT2501_NUM_OF_5226_CHNL (sizeof(RF5226RegTable)/sizeof(RT2501_RF_REGS))

/* Reset the RFIC setting to new series */
static const RT2501_RF_REGS RF5225RegTable[] = {
/*    ch   R1      R2      R3(TX0~4=0) R4 */
    {1,  0x95002ccc, 0x95004786, 0x95068455, 0x950ffa0b},
    {2,  0x95002ccc, 0x95004786, 0x95068455, 0x950ffa1f},
    {3,  0x95002ccc, 0x9500478a, 0x95068455, 0x950ffa0b},
    {4,  0x95002ccc, 0x9500478a, 0x95068455, 0x950ffa1f},
    {5,  0x95002ccc, 0x9500478e, 0x95068455, 0x950ffa0b},
    {6,  0x95002ccc, 0x9500478e, 0x95068455, 0x950ffa1f},
    {7,  0x95002ccc, 0x95004792, 0x95068455, 0x950ffa0b},
    {8,  0x95002ccc, 0x95004792, 0x95068455, 0x950ffa1f},
    {9,  0x95002ccc, 0x95004796, 0x95068455, 0x950ffa0b},
    {10, 0x95002ccc, 0x95004796, 0x95068455, 0x950ffa1f},
    {11, 0x95002ccc, 0x9500479a, 0x95068455, 0x950ffa0b},
    {12, 0x95002ccc, 0x9500479a, 0x95068455, 0x950ffa1f},
    {13, 0x95002ccc, 0x9500479e, 0x95068455, 0x950ffa0b},
    {14, 0x95002ccc, 0x950047a2, 0x95068455, 0x950ffa13},
};
#define RT2501_NUM_OF_5225_CHNL (sizeof(RF5225RegTable) / sizeof(RT2501_RF_REGS))

static int32_t rt2501_setup(void)
{
  uint32_t i;
  MAC_CSR12_STRUC csr12;

  DBG_WIFI("Loading 8051 firmware...");
  for(i=0;i<sizeof(rt2501_firmware);i+=4) {
    if(!rt2501_write(rt2501_dev, RT2501_FIRMWARE_IMAGE_BASE+i,
           *((int32_t *)(rt2501_firmware+i)))) return 0;
#ifdef DEBUG_WIFI
    if((i % 64) == 0) DBG_WIFI(".");
#endif
  }

  /* Start firmware */
  usbh_control_transfer(rt2501_dev,
        0,           /* pipe */
        0x01,        /* bRequest */
        USB_TYPE_VENDOR|USB_DIR_OUT,  /* bmRequestType */
        0x08,        /* wValue */
        0x00,        /* wIndex */
        0,        /* wLength */
        NULL);
  DBG_WIFI("OK!"EOL);

  /* Get ASIC version */
/*
        rt2501_asicver = rt2501_read(rt2501_dev, RT2501_MAC_CSR0);
#ifdef DEBUG_WIFI
  sprintf(dbg_buffer, "ASIC version: 0x%08x"EOL, rt2501_asicver);
  DBG_WIFI(dbg_buffer);
#endif
*/
  DBG_WIFI("Initializing MAC registers to the default values...");
  for(i=0;i<sizeof(rt2501_mac_defaults)/sizeof(rt2501_mac_defaults[0]);i++) {
    DBG_WIFI(".");
    if(!rt2501_write(rt2501_dev, rt2501_mac_defaults[i].reg, rt2501_mac_defaults[i].val))
      return 0;
  }
  DBG_WIFI("OK!"EOL);

  DBG_WIFI("Resetting...");
  rt2501_write(rt2501_dev, RT2501_MAC_CSR1, 0x03);
  rt2501_write(rt2501_dev, RT2501_MAC_CSR1, 0x00);
  DBG_WIFI("OK!"EOL);

  DBG_WIFI("Waiting for the hardware to be up and running...");
  while(1) {
    DBG_WIFI(".");
    csr12.word = rt2501_read(rt2501_dev, RT2501_MAC_CSR12);
    if(csr12.field.BbpRfStatus) break;
    /* Force wake-up */
    if(!rt2501_write(rt2501_dev, RT2501_MAC_CSR12, 0x4)) return 0;
    DelayMs(1000);
  }
  DBG_WIFI("OK!"EOL);

  DBG_WIFI("Setting up BBP...");
  /* Make sure BBP is okay */
  while(1) {
    if(rt2501_read_bbp(rt2501_dev, RT2501_BBP_R0) != 0) break;
    DelayMs(500);
    DBG_WIFI(".");
  }

  /* Initialize BBP register to default values */
  for(i=0;i<sizeof(rt2501_bbp_defaults)/sizeof(rt2501_bbp_defaults[0]);i++) {
    DBG_WIFI(".");
    rt2501_write_bbp(rt2501_dev, rt2501_bbp_defaults[i].reg,
         rt2501_bbp_defaults[i].val);
  }
  DBG_WIFI("OK!"EOL);

  /* Assert HOST ready bit */
  if(!rt2501_write(rt2501_dev, RT2501_MAC_CSR1, 0x04)) return 0;

  return 1;
}

static int32_t rt2501_setup_eeprom(void)
{
  int32_t i;
  int8_t channels[RT2501_MAX_NUM_OF_CHANNELS];
  uint16_t value;
  uint32_t value2;
  MAC_CSR2_STRUC csr2;
  MAC_CSR3_STRUC csr3;

  for(i=0;i<14;i++) rt2501_txpower[i].Channel = i + 1;

  /* Get various information from EEPROM */
  rt2501_read_eeprom(rt2501_dev, RT2501_EEPROM_BBP_BASE_OFFSET,
         rt2501_eeprom_defaults,
         sizeof(rt2501_eeprom_defaults));
  rt2501_Antenna.word = rt2501_eeprom_defaults[0];
  rt2501_auto_tx_agc = (rt2501_Antenna.field.DynamicTxAgcControl == 1);

  /*
     For RFIC RT2501_RFIC_5225 & RT2501_RFIC_2527
     Must enable RF RPI mode on PHY_CSR1 bit 16.
  */
  if((rt2501_Antenna.field.RfIcType == RT2501_RFIC_5225) || (rt2501_Antenna.field.RfIcType == RT2501_RFIC_2527)) {
    value2 = rt2501_read(rt2501_dev, RT2501_PHY_CSR1);
    value2 |= 0x10000;
    rt2501_write(rt2501_dev, RT2501_PHY_CSR1, value2);
  }


  rt2501_read_eeprom(rt2501_dev, RT2501_EEPROM_G_TX_PWR_OFFSET,
         channels,
         2*RT2501_NUM_EEPROM_TX_G_PARMS);
  for(i=0;i<2*RT2501_NUM_EEPROM_TX_G_PARMS;i++) {
    if((channels[i] > 36) || (channels[i] < -6))
      rt2501_txpower[i].Power = 24;
    else
      rt2501_txpower[i].Power = channels[i];

#ifdef DEBUG_WIFI
    sprintf(dbg_buffer, "TX power for channel %d: %0x"EOL,
      rt2501_txpower[i].Channel, rt2501_txpower[i].Power);
    DBG_WIFI(dbg_buffer);
#endif
  }

  rt2501_read_eeprom(rt2501_dev, RT2501_EEPROM_BG_TSSI_CALIBRAION,
         channels,
         10);
  rt2501_TSSI_MinusBoundary[4] = channels[0];
  rt2501_TSSI_MinusBoundary[3] = channels[1];
  rt2501_TSSI_MinusBoundary[2] = channels[2];
  rt2501_TSSI_MinusBoundary[1] = channels[3];
  rt2501_TSSI_PlusBoundary[1] = channels[4];
  rt2501_TSSI_PlusBoundary[2] = channels[5];
  rt2501_TSSI_PlusBoundary[3] = channels[6];
  rt2501_TSSI_PlusBoundary[4] = channels[7];
  rt2501_TSSI_Ref = channels[8];
  rt2501_TxAgcStep = channels[9];
  rt2501_TxAgcCompensate = 0;
  rt2501_TSSI_MinusBoundary[0] = rt2501_TSSI_Ref;
  rt2501_TSSI_PlusBoundary[0] = rt2501_TSSI_Ref;

  /* Disable TxAgc if the based value is not right */
  if (rt2501_TSSI_Ref == 0xff) rt2501_auto_tx_agc = 0;

  rt2501_BbpRssiToDbmDelta = 0x79;

  rt2501_read_eeprom(rt2501_dev, RT2501_EEPROM_FREQ_OFFSET,
         &value,
         sizeof(value));
  value &= 0x00FF;
  if(value != 0x00FF) rt2501_RfFreqOffset = value;
  else rt2501_RfFreqOffset = 0;
#ifdef DEBUG_WIFI
  sprintf(dbg_buffer, "EEPROM: RF freq offset=0x%lx"EOL, rt2501_RfFreqOffset);
  DBG_WIFI(dbg_buffer);
#endif

  rt2501_read_eeprom(rt2501_dev, RT2501_EEPROM_RSSI_BG_OFFSET,
         &value,
         sizeof(value));
  rt2501_RssiOffset1 = value & 0x00ff;
  rt2501_RssiOffset2 = (value >> 8);

  /* Validate RSSI_1 offset */
  if((rt2501_RssiOffset1 < -10) || (rt2501_RssiOffset1 > 10))
    rt2501_RssiOffset1 = 0;

  /* Validate RSSI_2 offset */
  if((rt2501_RssiOffset2 < -10) || (rt2501_RssiOffset2 > 10))
    rt2501_RssiOffset2 = 0;

  rt2501_read_eeprom(rt2501_dev, RT2501_EEPROM_TXPOWER_DELTA_OFFSET,
         &value,
         sizeof(value));
  value &= 0x00ff;
  if(value != 0xff) {
    rt2501_TxPowerDeltaConfig.value = value;
    if(rt2501_TxPowerDeltaConfig.field.DeltaValue > 0x04)
      rt2501_TxPowerDeltaConfig.field.DeltaValue = 0x04;
  } else rt2501_TxPowerDeltaConfig.field.TxPowerEnable = 0;

  DBG_WIFI("Setting up BBP from EEPROM...");
  for(i=3;i<RT2501_NUM_EEPROM_BBP_PARMS;i++) {
    if((rt2501_eeprom_defaults[i] != 0xffff) &&
       (rt2501_eeprom_defaults[i] != 0x0000)) {
         rt2501_write_bbp(rt2501_dev,
              (rt2501_eeprom_defaults[i] >> 8) & 0xff,
              (rt2501_eeprom_defaults[i] >> 0) & 0xff);
         DBG_WIFI(".");
       }
  }
  DBG_WIFI("OK!"EOL);

  /* Get MAC address from the EEPROM */
  rt2501_read_eeprom(rt2501_dev, RT2501_EEPROM_MAC_ADDRESS_BASE_OFFSET, rt2501_mac,
         sizeof(rt2501_mac));
#ifdef DEBUG_WIFI
  sprintf(dbg_buffer, "MAC address: %02x:%02x:%02x:%02x:%02x:%02x"EOL,
    rt2501_mac[0],
    rt2501_mac[1],
    rt2501_mac[2],
    rt2501_mac[3],
    rt2501_mac[4],
    rt2501_mac[5]);
  DBG_WIFI(dbg_buffer);
#endif

  DBG_WIFI("Programming MAC address...");
  csr2.field.Byte0 = rt2501_mac[0];
  csr2.field.Byte1 = rt2501_mac[1];
  csr2.field.Byte2 = rt2501_mac[2];
  csr2.field.Byte3 = rt2501_mac[3];
  csr3.field.Byte4 = rt2501_mac[4];
  csr3.field.Byte5 = rt2501_mac[5];
  csr3.field.U2MeMask = 0xff;
  rt2501_write(rt2501_dev, RT2501_MAC_CSR2, csr2.word);
  rt2501_write(rt2501_dev, RT2501_MAC_CSR3, csr3.word);
  DBG_WIFI("OK!"EOL);

  return 1;
}

static void rt2501_antenna_setting()
{
  uint8_t R3 = 0, R4 = 0, R77 = 0;
  uint8_t FrameTypeMaskBit5 = 0;

  sprintf(dbg_buffer, "RxDefaultAntenna=%x"EOL"RfIcType=%x",rt2501_Antenna.field.RxDefaultAntenna,rt2501_Antenna.field.RfIcType);
  DBG(dbg_buffer);

        if(rt2501_Antenna.field.RxDefaultAntenna == RT2501_SOFTWARE_DIVERSITY)
    rt2501_Antenna.field.RxDefaultAntenna = RT2501_ANTENNA_A;

        // SH 060918 : on force le mode HARDWARE_DIVERSITY pour améliorer la réception de l'antenne
//        rt2501_Antenna.field.RxDefaultAntenna = RT2501_HARDWARE_DIVERSITY;
        rt2501_Antenna.field.RxDefaultAntenna = RT2501_ANTENNA_A;
        rt2501_Antenna.field.RxDefaultAntenna = RT2501_ANTENNA_B;
  /*
     driver must disable Rx when switching antenna, otherwise ASIC will keep default state
     after switching, driver needs to re-enable Rx later
  */
  rt2501_write(rt2501_dev, RT2501_TXRX_CSR0, 0x0257b032);

  R3 = rt2501_read_bbp(rt2501_dev, RT2501_BBP_R3);
  R4 = rt2501_read_bbp(rt2501_dev, RT2501_BBP_R4);
  R77 = rt2501_read_bbp(rt2501_dev, RT2501_BBP_R77);

  R3 &= 0xfe;
  R4 &= ~0x23;

  FrameTypeMaskBit5 = ~(rt2501_Antenna.field.FrameType << 5);

  switch(rt2501_Antenna.field.RfIcType) {
  case RT2501_RFIC_5226:
    if(rt2501_Antenna.field.RxDefaultAntenna == RT2501_ANTENNA_A) {
      R4 = R4 | 0x01;
      R4 = R4 & FrameTypeMaskBit5;

      R77 = R77 | 0x03;

      rt2501_write_bbp(rt2501_dev, RT2501_BBP_R77, R77);
    } else if(rt2501_Antenna.field.RxDefaultAntenna == RT2501_ANTENNA_B) {
      R4 = R4 | 0x01;
      R4 = R4 & FrameTypeMaskBit5;

      R77 = R77 & 0xfc;

      rt2501_write_bbp(rt2501_dev, RT2501_BBP_R77, R77);
    } else if(rt2501_Antenna.field.RxDefaultAntenna == RT2501_HARDWARE_DIVERSITY) {
      R4 = R4 | 0x22;
      R4 = R4 & FrameTypeMaskBit5;
    }
    break;
                // notre carte :
  case RT2501_RFIC_2528:
    if(rt2501_Antenna.field.RxDefaultAntenna == RT2501_ANTENNA_A) {
      R4 = R4 | 0x21;
      R4 = R4 & FrameTypeMaskBit5;

      R77 = R77 | 0x03;

      rt2501_write_bbp(rt2501_dev, RT2501_BBP_R77, R77);
    } else if(rt2501_Antenna.field.RxDefaultAntenna == RT2501_ANTENNA_B) {
      R4 = R4 | 0x21;
      R4 = R4 & FrameTypeMaskBit5;

      R77 = R77 & 0xfc;

      rt2501_write_bbp(rt2501_dev, RT2501_BBP_R77, R77);
    } else if (rt2501_Antenna.field.RxDefaultAntenna == RT2501_HARDWARE_DIVERSITY) {
      R4 = R4 | 0x22;
      R4 = R4 & FrameTypeMaskBit5;
    }
    break;

  case RT2501_RFIC_5225:
    if(rt2501_Antenna.field.RxDefaultAntenna == RT2501_ANTENNA_A) {
      R4 = R4 | 0x01;
      R77 = R77 | 0x03;

      rt2501_write_bbp(rt2501_dev, RT2501_BBP_R77, R77);
    } else if(rt2501_Antenna.field.RxDefaultAntenna == RT2501_ANTENNA_B) {
      R4 = R4 | 0x01;
      R77 = R77 & 0xfc;

      rt2501_write_bbp(rt2501_dev, RT2501_BBP_R77, R77);
    } else if(rt2501_Antenna.field.RxDefaultAntenna == RT2501_HARDWARE_DIVERSITY) {
      R4 = R4 | 0x22;
    }
    break;

  case RT2501_RFIC_2527:
    if(rt2501_Antenna.field.RxDefaultAntenna == RT2501_ANTENNA_A) {
      R4 = R4 | 0x21;
      R4 = R4 & FrameTypeMaskBit5;

      R77 = R77 | 0x03;

      rt2501_write_bbp(rt2501_dev, RT2501_BBP_R77, R77);
    } else if(rt2501_Antenna.field.RxDefaultAntenna == RT2501_ANTENNA_B) {
      R4 = R4 | 0x21;
      R4 = R4 & FrameTypeMaskBit5;

      R77 = R77 & 0xfc;

      rt2501_write_bbp(rt2501_dev, RT2501_BBP_R77, R77);
    } else if(rt2501_Antenna.field.RxDefaultAntenna == RT2501_HARDWARE_DIVERSITY) {
      R4 = R4 | 0x22;
      R4 = R4 & FrameTypeMaskBit5;
    }
    break;

  default:
    break;
  }

  rt2501_write_bbp(rt2501_dev, RT2501_BBP_R3, R3);
  rt2501_write_bbp(rt2501_dev, RT2501_BBP_R4, R4);
}

static void rt2501_update_rf(void)
{
  /* Set RF value 1's set R3[bit2] = [0] */
  rt2501_write_rf(rt2501_dev, rt2501_LatchRfRegs.R1);
  rt2501_write_rf(rt2501_dev, rt2501_LatchRfRegs.R2);
  rt2501_write_rf(rt2501_dev, (rt2501_LatchRfRegs.R3 & (~0x04)));
  rt2501_write_rf(rt2501_dev, rt2501_LatchRfRegs.R4);

  /* Set RF value 2's set R3[bit2] = [1] */
  rt2501_write_rf(rt2501_dev, rt2501_LatchRfRegs.R1);
  rt2501_write_rf(rt2501_dev, rt2501_LatchRfRegs.R2);
  rt2501_write_rf(rt2501_dev, (rt2501_LatchRfRegs.R3 | 0x04));
  rt2501_write_rf(rt2501_dev, rt2501_LatchRfRegs.R4);

  /* Set RF value 3's set R3[bit2] = [0] */
  rt2501_write_rf(rt2501_dev, rt2501_LatchRfRegs.R1);
  rt2501_write_rf(rt2501_dev, rt2501_LatchRfRegs.R2);
  rt2501_write_rf(rt2501_dev, (rt2501_LatchRfRegs.R3 & (~0x04)));
  rt2501_write_rf(rt2501_dev, rt2501_LatchRfRegs.R4);
}

void rt2501_switch_channel(uint8_t channel)
{
  uint32_t R3 = 5, R4;
  int8_t TxPwer = 0, Bbp94 = BBPR94_DEFAULT;
  uint8_t index, BbpReg;

  rt2501_channel = channel;

  rt2501_antenna_setting();

  /* Search for Tx power value */
  for(index=0;index<RT2501_MAX_NUM_OF_CHANNELS;index++) {
    if(channel == rt2501_txpower[index].Channel) {
      TxPwer = rt2501_txpower[index].Power;
      break;
    }
  }

  if(TxPwer > 31) {
    /* R3 can't be larger than 36 (0x24), 31 ~ 36 used by BBP 94 */
    R3 = 31;
    if(TxPwer <= 36)
      Bbp94 = BBPR94_DEFAULT + (uint8_t )(TxPwer - 31);
  } else if(TxPwer < 0) {
    /* R3 can't be less than 0, -1 ~ -6 used by BBP 94 */
    R3 = 0;
    if(TxPwer >= -6)
      Bbp94 = BBPR94_DEFAULT + TxPwer;
  } else {
    /* 0 ~ 31 */
    R3 = (unsigned int)TxPwer;
  }

  if(Bbp94 < 0) Bbp94 = 0;

  R3 = R3 << 9; /* shift TX power control to correct RF R3 bit position */

  switch(rt2501_Antenna.field.RfIcType) {
  case RT2501_RFIC_2528:
    for(index=0;index<RT2501_NUM_OF_2528_CHNL;index++) {
      if(channel == RF2528RegTable[index].Channel) {
        R3 = R3 | (RF2528RegTable[index].R3 & 0xffffc1ff); /* set TX power */
        R4 = (RF2528RegTable[index].R4 & (~0x0003f000)) | (rt2501_RfFreqOffset << 12);

        /* Update variables */
        rt2501_LatchRfRegs.Channel = channel;
        rt2501_LatchRfRegs.R1 = RF2528RegTable[index].R1;
        rt2501_LatchRfRegs.R2 = RF2528RegTable[index].R2;
        rt2501_LatchRfRegs.R3 = R3;
        rt2501_LatchRfRegs.R4 = R4;

        break;
      }
    }
    break;

  case RT2501_RFIC_5226:
    for(index=0;index<RT2501_NUM_OF_5226_CHNL;index++) {
      if(channel == RF5226RegTable[index].Channel) {
        R3 = R3 | (RF5226RegTable[index].R3 & 0xffffc1ff); /* set TX power */
        R4 = (RF5226RegTable[index].R4 & (~0x0003f000)) | (rt2501_RfFreqOffset << 12);

        /* Update variables */
        rt2501_LatchRfRegs.Channel = channel;
        rt2501_LatchRfRegs.R1 = RF5226RegTable[index].R1;
        rt2501_LatchRfRegs.R2 = RF5226RegTable[index].R2;
        rt2501_LatchRfRegs.R3 = R3;
        rt2501_LatchRfRegs.R4 = R4;

        break;
      }
    }
    break;

  case RT2501_RFIC_5225:
  case RT2501_RFIC_2527:
    for(index=0;index<RT2501_NUM_OF_5225_CHNL;index++) {
      if(channel == RF5225RegTable[index].Channel) {
        R3 = R3 | (RF5225RegTable[index].R3 & 0xffffc1ff); /* set TX power */
        R4 = (RF5225RegTable[index].R4 & (~0x0003f000)) | (rt2501_RfFreqOffset << 12);

        /* Update variables */
        rt2501_LatchRfRegs.Channel = channel;
        rt2501_LatchRfRegs.R1 = RF5225RegTable[index].R1;
        rt2501_LatchRfRegs.R2 = RF5225RegTable[index].R2;
        rt2501_LatchRfRegs.R3 = R3;
        rt2501_LatchRfRegs.R4 = R4;

        break;
      }
    }

    BbpReg = rt2501_read_bbp(rt2501_dev, RT2501_BBP_R3);
    if ((rt2501_Antenna.field.RfIcType == RT2501_RFIC_5225)
        || (rt2501_Antenna.field.RfIcType == RT2501_RFIC_2527))
      BbpReg &= 0xFE; /* b0=0 for no Smart mode */
    else
      BbpReg |= 0x01; /* b0=1 for Smart mode */
    rt2501_write_bbp(rt2501_dev, RT2501_BBP_R3, BbpReg);
    break;

  default:
    break;
  }

  if (Bbp94 != BBPR94_DEFAULT)
    rt2501_write_bbp(rt2501_dev, RT2501_BBP_R94, Bbp94);

/*  if (!OPSTATUS_TEST_FLAG(pAd, fOP_STATUS_MEDIA_STATE_CONNECTED))
  {
      if (pAd->BbpTuning.R17LowerUpperSelect == 0)
        rt2501_write_bbp(rt2501_dev, RT2501_BBP_R17, pAd->BbpTuning.R17LowerBoundG);
      else
        rt2501_write_bbp(rt2501_dev, RT2501_BBP_R17, pAd->BbpTuning.R17UpperBoundG);
  }*/

  rt2501_update_rf();

  /* Wait for BBP and RF to be stable */
  DelayMs(2);

  rt2501_write(rt2501_dev, RT2501_TXRX_CSR0, 0x025eb032);

#ifdef DEBUG_WIFI
  sprintf(dbg_buffer, "SwitchChannel(RF=%d) to #%d, TXPwr=%ld, R1=0x%08lx, R2=0x%08lx, R3=0x%08lx, R4=0x%08lx"EOL,
    rt2501_Antenna.field.RfIcType,
    rt2501_LatchRfRegs.Channel,
    (R3 & 0x00003e00) >> 9,
    rt2501_LatchRfRegs.R1,
    rt2501_LatchRfRegs.R2,
    rt2501_LatchRfRegs.R3,
    rt2501_LatchRfRegs.R4);
  DBG_WIFI(dbg_buffer);
#endif
}

uint8_t rt2501_set_bssid(const uint8_t *bssid)
{
  MAC_CSR4_STRUC csr4;
  MAC_CSR5_STRUC csr5;

#ifdef DEBUG_WIFI
  sprintf(dbg_buffer, "Setting BSSID to %02x:%02x:%02x:%02x:%02x:%02x"EOL,
    bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
  DBG_WIFI(dbg_buffer);
#endif

  csr4.field.Byte0 = bssid[0];
  csr4.field.Byte1 = bssid[1];
  csr4.field.Byte2 = bssid[2];
  csr4.field.Byte3 = bssid[3];
  if(!rt2501_write(rt2501_dev, RT2501_MAC_CSR4, csr4.word)) return 0;

  csr5.word = 0;
  csr5.field.Byte4 = bssid[4];
  csr5.field.Byte5 = bssid[5];
  csr5.field.BssIdMask = 3;
  if(!rt2501_write(rt2501_dev, RT2501_MAC_CSR5, csr5.word)) return 0;

  return 1;
}

static void rt2501_calibrate(void)
{
  uint8_t index;
  int8_t TxPwer=0, CurrTxPwr;
  int32_t R3 = 5;
  uint8_t BbpR1;

  for(index=0;index<RT2501_MAX_NUM_OF_CHANNELS;index++) {
    if(rt2501_channel == rt2501_txpower[index].Channel) {
      TxPwer = rt2501_txpower[index].Power;
      break;
    }
  }

  if((TxPwer > 31) || (TxPwer < 0))
    R3 = 0;
  else
    R3 = TxPwer;

  if(R3 > 31) R3 = 31;

  if(rt2501_auto_tx_agc) {
    BbpR1 = rt2501_read_bbp(rt2501_dev, RT2501_BBP_R1);
    if(BbpR1 > rt2501_TSSI_MinusBoundary[1]) {
      /* Reading is larger than the reference value */
      for(index=1;index<5;index++) {
        if(BbpR1 <= rt2501_TSSI_MinusBoundary[index])
          break;
      }
      /* The index is the step we should decrease, index = 0 means there is nothing to compensate */
      if(R3 > (rt2501_TxAgcStep*(index-1)))
        rt2501_TxAgcCompensate = -(rt2501_TxAgcStep*(index-1));
      else
        rt2501_TxAgcCompensate = -R3;

      R3 += rt2501_TxAgcCompensate;
#ifdef DEBUG_WIFI
      sprintf(dbg_buffer, "-- Tx Power, BBP R1=%x, TxAgcStep=%x, step = -%d"EOL,
        BbpR1, rt2501_TxAgcStep, index-1);
      DBG_WIFI(dbg_buffer);
#endif
    } else if (BbpR1 < rt2501_TSSI_PlusBoundary[1]) {
      /* Reading is smaller than the reference value */
      for(index=1;index<5;index++) {
        if(BbpR1 >= rt2501_TSSI_PlusBoundary[index])
          break;
      }
      /* The index is the step we should increase, index = 0 means there is nothing to compensate */
      rt2501_TxAgcCompensate = rt2501_TxAgcStep*(index-1);
      R3 += rt2501_TxAgcCompensate;
#ifdef DEBUG_WIFI
      sprintf(dbg_buffer, "++ Tx Power, BBP R1=%x, TxAgcStep=%x, step = +%d"EOL,
        BbpR1, rt2501_TxAgcStep, index-1);
      DBG_WIFI(dbg_buffer);
#endif
    }
  }

  if(R3 > 31) R3 = 31;

  CurrTxPwr = (rt2501_LatchRfRegs.R3 >> 9) & 0x0000001f;
  if(CurrTxPwr != R3) {
    R3 = (rt2501_LatchRfRegs.R3 & 0xffffc1ff) | (R3 << 9);
    rt2501_LatchRfRegs.R3 = R3;
    rt2501_update_rf();
  }
}

static uint8_t *rt2501_rxbuf; /* to store the URB temporarily */
static uint32_t rt2501_frame_position;
static uint8_t rt2501_frame[RT2501_MAX_FRAME_SIZE];

static void rt2501_submit_rx(void);

static void rt2501_rx_callback(PURB urb)
{
  PRXD_STRUC rxd;

  if(urb->status < 0) {
    sprintf(dbg_buffer, "USB BULK RX ERROR, status=%d, result=%ld"EOL,
      urb->status, urb->result);
    DBG_WIFI(dbg_buffer);
    rt2501_connected = 0;
    hcd_free(urb);
    return;
  }
  memcpy(rt2501_frame+rt2501_frame_position, urb->buffer, RT2501_USB_PACKET_SIZE);
  if(urb->status < RT2501_USB_PACKET_SIZE) {
    rxd = (PRXD_STRUC)rt2501_frame;
#if 0
//#ifdef DEBUG_WIFI
      sprintf(dbg_buffer, "RX Crc=%d, CipherErr=%d, KeyIndex=%d, CipherAlg=%d, MyBss=%d, Iv=%08lx, Eiv=%08lx"EOL,
        rxd->Crc,
        rxd->CipherErr,
        rxd->KeyIndex,
        rxd->CipherAlg,
        rxd->MyBss,
        rxd->Iv,
        rxd->Eiv);
      DBG_WIFI(dbg_buffer);
#endif
    if(!rxd->Crc && ((rxd->CipherAlg == RT2501_CIPHER_NONE) || (rxd->CipherErr == 0))) {
      ieee80211_input(rt2501_frame+sizeof(RXD_STRUC),
          rxd->DataByteCnt,
          rxd->PlcpRssi-rt2501_BbpRssiToDbmDelta);
    }
    rt2501_frame_position = 0;
  } else {
    rt2501_frame_position += RT2501_USB_PACKET_SIZE;
    if((rt2501_frame_position+64) >= RT2501_MAX_FRAME_SIZE) {
      /* this shouldn't happen (see MAC_CSR6) */
      DBG_WIFI("RX Buffer overrun"EOL);
      rt2501_connected = 0;
      return;
    }
  }
  hcd_free(urb);

  rt2501_submit_rx();
}

static void rt2501_submit_rx(void)
{
  PURB urb;

  if(!rt2501_connected) return;

  disable_ohci_irq();
  urb = hcd_malloc(sizeof(*urb), EXTRAM,17);
  enable_ohci_irq();
  if(urb == NULL) return;

  urb->buffer = rt2501_rxbuf;
  urb->length = RT2501_USB_PACKET_SIZE;
  urb->timeout = 0;
  urb->setup = NULL;
  urb->dev = rt2501_dev;
  urb->ed = rt2501_dev->pipe[2];
  urb->callback = rt2501_rx_callback;
  urb->dma_enable = 0;

  usbh_transfer_request(urb);
}

static const uint8_t RateIdToPlcpSignal[12] = {
  0, 1, 2, 3,    /* see BBP spec */
  11, 15, 10, 14,    /* see IEEE802.11a-1999 p.14 */
  9, 3, 8, 12    /* see IEEE802.11a-1999 p.14 */
};

void rt2501_make_tx_descriptor(
  PTXD_STRUC txd,
  uint8_t CipherAlg,
  uint8_t KeyTable,
  uint8_t KeyIdx,
  uint8_t Ack,
  uint8_t Fragment,
  uint8_t InsTimestamp,
  uint8_t RetryMode,
  uint8_t Ifs,
  uint32_t Rate,
  uint32_t Length,
  uint8_t QueIdx,
  uint8_t PacketId)
{
  uint32_t Residual;

  memset((void*)txd, 0, sizeof(TXD_STRUC));

  txd->HostQId    = QueIdx;
  txd->MoreFrag    = Fragment;
  txd->ACK    = Ack;
  txd->Timestamp   = InsTimestamp;
  txd->RetryMd    = RetryMode;
  txd->Ofdm    = (Rate < RT2501_RATE_6) ? 0:1;
  txd->IFS    = Ifs;
  txd->PktId     = PacketId;
  txd->Drop    = 1;
  txd->HwSeq     = 1;
  txd->BbpTxPower  = 0;
  txd->DataByteCnt = Length;

  /* fill encryption related information, if required */
  txd->CipherAlg = CipherAlg;

  sprintf(dbg_buffer, "Cipher = %u, rate = %lu"EOL, CipherAlg, Rate);
  DBG_WIFI(dbg_buffer);

  if(CipherAlg != RT2501_CIPHER_NONE) {
    txd->KeyTable    = KeyTable;
    txd->KeyIndex    = KeyIdx;
    txd->TkipMic    = 1;
  }
  /*
     In TKIP+fragmentation, TKIP MIC is already appended by driver.
     MAC does not need to generate MIC.
  */
  if(CipherAlg == RT2501_CIPHER_TKIP_NO_MIC) {
    txd->CipherAlg = RT2501_CIPHER_TKIP;
    txd->TkipMic = 0; /* tell MAC need not insert TKIP MIC */
  }

  txd->Cwmin = 4;
  txd->Cwmax = 10;
  txd->Aifsn = 2;

  /* fill up PLCP SIGNAL field */
  txd->PlcpSignal = RateIdToPlcpSignal[Rate];

  /* fill up PLCP SERVICE field, not used for OFDM rates */
  txd->PlcpService = 4;

  /* file up PLCP LENGTH_LOW and LENGTH_HIGH fields */
  Length += 4; /* CRC length */
  switch(CipherAlg) {
    case RT2501_CIPHER_WEP64:
      Length += 8; /* IV + ICV */
      break;
    case RT2501_CIPHER_WEP128:
      Length += 8; /* IV + ICV */
      break;
    case RT2501_CIPHER_TKIP:
      Length += 20; /* IV + EIV + MIC + ICV */
      break;
    case RT2501_CIPHER_AES:
      Length += 16; /* IV + EIV + MIC */
      break;
    case RT2501_CIPHER_CKIP64:
      /* IV + CMIC + ICV, but CMIC already inserted by driver */
      Length += 8;
      break;
    case RT2501_CIPHER_CKIP128:
      /* IV + CMIC + ICV, but CMIC already inserted by driver */
      Length += 8;
      break;
    case RT2501_CIPHER_TKIP_NO_MIC:
      Length += 12; /* IV + EIV + ICV */
      break;
    default:
      break;
  }

  if(Rate < RT2501_RATE_6) {
    if((Rate == RT2501_RATE_1) || ( Rate == RT2501_RATE_2)) {
      Length = Length * 8 / (Rate + 1);
    } else {
      Residual = ((Length * 16) % (11 * (1 + Rate - RT2501_RATE_5_5)));
      Length = Length * 16 / (11 * (1 + Rate - RT2501_RATE_5_5));
      if(Residual != 0) Length++;
      if((Residual <= (3 * (1 + Rate - RT2501_RATE_5_5))) && (Residual != 0))  {
        if (Rate == RT2501_RATE_11)
          txd->PlcpService |= 0x80;
      }
    }

    txd->PlcpLengthHigh = Length >> 8;
    txd->PlcpLengthLow = Length % 256;
  } else {
    txd->PlcpLengthHigh = Length >> 6;
    txd->PlcpLengthLow = Length % 64;
  }

  txd->Burst2 = txd->Burst = Fragment;
}

/* Buffer must be in COMRAM */
int8_t rt2501_tx(void *buffer, uint32_t length)
{
  int8_t ret;

  /* Length must be a multiple of 4 */
  if((length % 4) != 0) length += 4 - (length % 4);
  /* Moreover, it must not be a multiple of the USB packet size */
  if((length % RT2501_USB_PACKET_SIZE) == 0) length += 4;
#ifdef DEBUG_WIFI
  DBG_WIFI("Tx:"EOL);
  dump(buffer,length);
#endif
  ret = usbh_bulk_transfer_async(rt2501_dev, 1, buffer, length);
#ifdef DEBUG_WIFI
  sprintf(dbg_buffer, "ret = %d"EOL, ret);
  DBG_WIFI(dbg_buffer);
#endif
  return ((ret > 0) || (ret == URB_PENDING));
}

uint8_t rt2501_beacon(void *buffer, uint32_t length)
{
  TXRX_CSR9_STRUC csr9;
  uint32_t i;

  csr9.word = rt2501_read(rt2501_dev, RT2501_TXRX_CSR9);
  csr9.field.bBeaconGen = 0;
  csr9.field.TsfSyncMode = 0;
  csr9.field.bTBTTEnable = 0;
  csr9.field.bTsfTicking = 0;
  if(!rt2501_write(rt2501_dev, RT2501_TXRX_CSR9, csr9.word)) return 0;

  if(buffer != NULL) {
    /* Clear beacon owner/valid bits to prevent garbage */
    rt2501_write(rt2501_dev, RT2501_HW_BEACON_BASE0, 0);
    rt2501_write(rt2501_dev, RT2501_HW_BEACON_BASE1, 0);
    rt2501_write(rt2501_dev, RT2501_HW_BEACON_BASE2, 0);
    rt2501_write(rt2501_dev, RT2501_HW_BEACON_BASE3, 0);

    /*
       The buffer should have enough spare space for the 32-bit
       alignment. If it does not, the overflow should have no effect
       on this platform, but it's dirty.
    */
    if((length % 4) != 0) length += 4 - (length % 4);
    for(i=0;i<length;i+=4) {
      rt2501_write(rt2501_dev, RT2501_HW_BEACON_BASE0+i,
             *((uint32_t *)((uint8_t *)buffer+i)));
    }

    csr9.field.bTsfTicking = 1;
    csr9.field.TsfSyncMode = 3; /* do not sync TSF (AP mode) */
    csr9.field.bTBTTEnable = 1;
    csr9.field.bBeaconGen = 1;
    if(!rt2501_write(rt2501_dev, RT2501_TXRX_CSR9, csr9.word))
      return 0;
  }
  return 1;
}

static int32_t rt2501_write_key(uint32_t address, uint8_t *buffer, uint32_t length)
{
  uint32_t i;
  union {
    struct {
      uint8_t c1;
      uint8_t c2;
      uint8_t c3;
      uint8_t c4;
    } elements;
    uint32_t value;
  } block;

  for(i=0;i<length;i+=4) {
      block.elements.c1 = buffer[i+0];
      block.elements.c2 = buffer[i+1];
      block.elements.c3 = buffer[i+2];
      block.elements.c4 = buffer[i+3];
#ifdef DEBUG_WIFI
      sprintf(dbg_buffer, "writing key data: 0x%08lx <- 0x%08lx"EOL,
        address+i,
        block.value);
      DBG_WIFI(dbg_buffer);
#endif
      if(!rt2501_write(rt2501_dev, address+i, block.value)) return 0;
  }
  return 1;
}

int32_t rt2501_set_key(uint8_t index, uint8_t *key, uint8_t *txmic, uint8_t *rxmic, uint8_t cipher)
{
  uint32_t csr0, csr1;
  uint32_t key_length=0;
  uint32_t base_address;

  switch(cipher) {
  case RT2501_CIPHER_NONE:
    key_length = 0;
    break;
  case RT2501_CIPHER_WEP64:
    key_length = IEEE80211_WEP64_KEYLEN;
    break;
  case RT2501_CIPHER_WEP128:
    key_length = IEEE80211_WEP128_KEYLEN;
    break;
  case RT2501_CIPHER_TKIP:
    key_length = EAPOL_TKIP_EK_LENGTH;
    break;
  case RT2501_CIPHER_AES:
    key_length = EAPOL_AES_EK_LENGTH;
    break;
  }

  /* Mark the key invalid */
  csr0 = rt2501_read(rt2501_dev, RT2501_SEC_CSR0);
  csr0 &= ~(1 << index);
  if(!rt2501_write(rt2501_dev, RT2501_SEC_CSR0, csr0)) return 0;

  base_address = RT2501_SHARED_KEY_TABLE_BASE+index*RT2501_KEY_ENTRY_SIZE;

  if((key != NULL) && (cipher != RT2501_CIPHER_NONE)) {
    /* Send the key data */
    if(!rt2501_write_key(base_address, key, key_length)) return 0;

    if(cipher == RT2501_CIPHER_TKIP) {
      /* Send the TX MIC key */
      if(!rt2501_write_key(base_address+RT2501_MICS_OFFSET, txmic, EAPOL_TKIP_TXMICK_LENGTH)) return 0;
      /* Send the RX MIC key */
      if(!rt2501_write_key(base_address+RT2501_MICS_OFFSET+EAPOL_TKIP_TXMICK_LENGTH, rxmic, EAPOL_TKIP_RXMICK_LENGTH)) return 0;
    }
    else if(cipher == RT2501_CIPHER_AES) {
      /* Send the TX MIC key */
      if(!rt2501_write_key(base_address+RT2501_MICS_OFFSET, txmic, EAPOL_AES_TXMICK_LENGTH)) return 0;
      /* Send the RX MIC key */
      if(!rt2501_write_key(base_address+RT2501_MICS_OFFSET+EAPOL_AES_TXMICK_LENGTH, rxmic, EAPOL_AES_RXMICK_LENGTH)) return 0;
    }

    /* Set cipher algorithm */
    csr1 = rt2501_read(rt2501_dev, RT2501_SEC_CSR1);
    csr1 &= ~(0xf << (index*4));
    csr1 |= cipher << (index*4);
    if(!rt2501_write(rt2501_dev, RT2501_SEC_CSR1, csr1)) return 0;

    /* Mark the key valid again */
    csr0 |= 1 << index;
    if(!rt2501_write(rt2501_dev, RT2501_SEC_CSR0, csr0)) return 0;
  }

  return 1;
}

uint16_t rt2501_txtime(uint32_t len, uint8_t rate)
{
  uint16_t txtime;

  txtime = 20 + 6; /* 16 + 4 preamble + plcp + Signal Extension */
  txtime += 4 * ((11+len*4)/rate);
  if(((11+len*4) % rate) != 0) txtime += 4;
  return txtime;
}

static int32_t rt2501_cal_timer;

static void *rt2501_connect(PDEVINFO dev)
{
  struct usb_interface_descriptor *interface;
  struct usb_endpoint_descriptor *endpoint;
  int32_t bulk_tx_recognized, bulk_rx_recognized;

  if(!dev) return NULL;
  if(!dev->descriptor) return NULL;

  sprintf(dbg_buffer, "VID: 0x%04x PID: 0x%04x"EOL,
    dev->descriptor->idVendor,
    dev->descriptor->idProduct);
  DBG_WIFI(dbg_buffer);

  if(!(
      ((dev->descriptor->idVendor != RT2501_VENDORID1) && (dev->descriptor->idProduct != RT2501_PRODUCTID1))
    ||((dev->descriptor->idVendor != RT2501_VENDORID2) && (dev->descriptor->idProduct != RT2501_PRODUCTID2))
    ||((dev->descriptor->idVendor != RT2501_VENDORID3) && (dev->descriptor->idProduct != RT2501_PRODUCTID3))
       )) return NULL;
  if(!dev->descriptor->configuration) return NULL;
   interface = dev->descriptor->configuration->interface;
  if(rt2501_connected) {
    DBG_WIFI("This driver only supports one device at once"EOL);
    return NULL;
  }

  DBG_WIFI("RT2501 stick found !"EOL);

  /*
    If we don't send this Set Configuration request, some parts of the
    device don't get powered up. First symptom, all BBP registers read 0.
  */
  usbh_set_configuration(dev, 1);

  /* Find and open endpoints */
  endpoint = interface->endpoint;
  bulk_tx_recognized = 0;
  bulk_rx_recognized = 0;
  while(endpoint != NULL) {
    if((endpoint->bEndpointAddress == 0x01)
       && (endpoint->bmAttributes == 0x02)) {
         DBG_WIFI("Recognized TX bulk endpoint"EOL);
         disable_ohci_irq();
         dev->pipe[1] = usbh_create_pipe(dev,
        endpoint->bmAttributes,
        endpoint->bEndpointAddress,
        endpoint->wMaxPacketSize,
        endpoint->bInterval);
         enable_ohci_irq();
         if(dev->pipe[1] != NULL) bulk_tx_recognized = 1;
       }
    if((endpoint->bEndpointAddress == 0x81)
       && (endpoint->bmAttributes == 0x02)) {
         DBG_WIFI("Recognized RX bulk endpoint"EOL);
         disable_ohci_irq();
         dev->pipe[2] = usbh_create_pipe(dev,
        endpoint->bmAttributes,
        endpoint->bEndpointAddress,
        endpoint->wMaxPacketSize,
        endpoint->bInterval);
         enable_ohci_irq();
         if(dev->pipe[2] != NULL) bulk_rx_recognized = 1;
       }

    endpoint = endpoint->next;
  }

  if(!(bulk_tx_recognized && bulk_rx_recognized)) {
    disable_ohci_irq();
    if(bulk_tx_recognized) usbh_delete_pipe(dev, 1);
    if(bulk_rx_recognized) usbh_delete_pipe(dev, 2);
    enable_ohci_irq();
    return NULL;
  }

  rt2501_dev = dev;

  if(!rt2501_setup()) {
    disable_ohci_irq();
    usbh_delete_pipe(dev, 1);
    usbh_delete_pipe(dev, 2);
    enable_ohci_irq();
    return NULL;
  }

  if(!rt2501_setup_eeprom()) {
    disable_ohci_irq();
    usbh_delete_pipe(dev, 1);
    usbh_delete_pipe(dev, 2);
    enable_ohci_irq();
    return NULL;
  }

  rt2501_switch_channel(1);

  rt2501_rxbuf = hcd_malloc(RT2501_USB_PACKET_SIZE, COMRAM,18);
  if(rt2501_rxbuf == NULL) {
    DBG_WIFI("Unable to allocate RX buffer !"EOL);
    usbh_delete_pipe(dev, 1);
    usbh_delete_pipe(dev, 2);
    return NULL;
  }

  rt2501buffer_init();
  ieee80211_init();

  rt2501_cal_timer = RT2501_CAL_PERIOD;

  rt2501_connected = 1;

  rt2501_frame_position = 0;
  rt2501_submit_rx();

  return &rt2501_connected;
}

static void rt2501_disconnect(PDEVINFO dev)
{
  DBG_WIFI("RT2501 stick disconnected"EOL);
  rt2501buffer_free();
  dev->driver_data = NULL;
  dev->driver = NULL;
  rt2501_connected = 0;
}

static struct usbh_driver usb_rt2501_driver = {
  "rt2501",
  rt2501_connect,
  rt2501_disconnect,
  {NULL}
};

int32_t rt2501_driver_install(void)
{
  rt2501_connected = 0;
  usbh_driver_install(&usb_rt2501_driver);
  DBG_WIFI("RT2501 driver installed, (c) 2006 lekernel"EOL);
  return OK;
}

int32_t rt2501_state(void)
{
  if(!rt2501_connected)
    return RT2501_S_BROKEN;
  if(ieee80211_mode == IEEE80211_M_MASTER)
    return RT2501_S_MASTER;
  switch(ieee80211_state)
  {
    case IEEE80211_S_IDLE:
      return RT2501_S_IDLE;
    case IEEE80211_S_SCAN:
      return RT2501_S_SCAN;
    case IEEE80211_S_AUTH:
    case IEEE80211_S_ASSOC:
    case IEEE80211_S_EAPOL:
      return RT2501_S_CONNECTING;
    case IEEE80211_S_RUN:
      return RT2501_S_CONNECTED;
    default:
      return RT2501_S_BROKEN;
  }
}

void rt2501_timer(void)
{
  if(!rt2501_connected) return;
  rt2501_cal_timer--;
  if(rt2501_cal_timer == 0) {
    rt2501_cal_timer = RT2501_CAL_PERIOD;
    rt2501_calibrate();
  }
  ieee80211_timer();
}
