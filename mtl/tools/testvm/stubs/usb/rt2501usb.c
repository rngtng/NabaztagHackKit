#include <stdlib.h>
#include <stdio.h>
#include <stdint.h>
#include "net/ieee80211.h"
#include "usb/usbh.h"
#include "usb/rt2501usb_hw.h"

uint8_t rt2501_mac[IEEE80211_ADDR_LEN];
PDEVINFO rt2501_dev;

int32_t rt2501_state(void)
{
  return 0;
}

int32_t rt2501_set_key(uint8_t index, uint8_t *key, uint8_t *txmic, uint8_t *rxmic, uint8_t cipher)
{

}

uint16_t rt2501_txtime(uint32_t len, uint8_t rate)
{

}

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

}

int32_t rt2501_tx(void *buffer, uint32_t length)
{

}

uint8_t rt2501_beacon(void *buffer, uint32_t length)
{

}

uint8_t rt2501_set_bssid(const uint8_t *bssid)
{

}

void rt2501_switch_channel(uint8_t channel)
{

}
