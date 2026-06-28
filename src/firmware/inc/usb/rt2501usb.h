/**
 * @file rt2501usb.h
 * @author Sebastien Bourdeauducq - 2006 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief RT2501 Wifi/Network driver
 */
#ifndef _RT2501_H_
#define _RT2501_H_

#include "usb/usbh.h"
#include "usb/rt2501usb_hw.h"
#include "net/ieee80211.h"

enum {
	RT2501_S_BROKEN,
	RT2501_S_IDLE,
	RT2501_S_SCAN,
	RT2501_S_CONNECTING,
	RT2501_S_CONNECTED,
	RT2501_S_MASTER,
};

struct rt2501_scan_result {
	uint8_t ssid[IEEE80211_SSID_MAXLEN+1];
	uint8_t mac[IEEE80211_ADDR_LEN];
	uint8_t bssid[IEEE80211_ADDR_LEN];
	int16_t rssi;
	uint8_t channel;
	uint16_t rateset;
	uint8_t encryption;
};

typedef void (*rt2501_scan_callback)(struct rt2501_scan_result *, void *);

struct rt2501buffer {
	struct rt2501buffer *next;			/* driver internal use             */
	uint32_t length;				/* length of the data              */
	uint8_t source_mac[IEEE80211_ADDR_LEN];	/* MAC address of the source       */
	uint8_t dest_mac[IEEE80211_ADDR_LEN];	/* MAC address of the destination  */
	uint8_t data[];				/* ethernet frame, starting at LLC */
};

extern PDEVINFO rt2501_dev;

#define RT2501_MAX_NUM_OF_CHANNELS 14
#define RT2501_USB_PACKET_SIZE     64
#define RT2501_MAX_FRAME_SIZE      2048

#define RT2501_MAX_ASSOCIATED_STA  5

#define	RT2501_RATEMASK_1		(1<<0)
#define	RT2501_RATEMASK_2		(1<<1)
#define	RT2501_RATEMASK_5_5		(1<<2)
#define	RT2501_RATEMASK_11		(1<<3)
/* OFDM rates */
#define RT2501_RATEMASK_6		(1<<4)
#define RT2501_RATEMASK_9		(1<<5)
#define RT2501_RATEMASK_12		(1<<6)
#define RT2501_RATEMASK_18		(1<<7)
#define RT2501_RATEMASK_24		(1<<8)
#define RT2501_RATEMASK_36		(1<<9)
#define RT2501_RATEMASK_48		(1<<10)
#define RT2501_RATEMASK_54		(1<<11)

#define	IEEE80211_RATEMASK_1		(1<<0)
#define	IEEE80211_RATEMASK_2		(1<<1)
#define	IEEE80211_RATEMASK_5_5		(1<<2)
#define IEEE80211_RATEMASK_6		(1<<3)
#define IEEE80211_RATEMASK_9		(1<<4)
#define	IEEE80211_RATEMASK_11		(1<<5)
#define IEEE80211_RATEMASK_12		(1<<6)
#define IEEE80211_RATEMASK_18		(1<<7)
#define IEEE80211_RATEMASK_24		(1<<8)
#define IEEE80211_RATEMASK_36		(1<<9)
#define IEEE80211_RATEMASK_48		(1<<10)
#define IEEE80211_RATEMASK_54		(1<<11)
#define IEEE80211_RATEMASK_HIGHEST	IEEE80211_RATEMASK_54

#define RT2501_RSSI_SAMPLES		15

void rt2501_switch_channel(uint8_t channel);
uint8_t rt2501_set_bssid(const uint8_t *bssid);
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
	uint8_t PacketId);
int8_t rt2501_tx(void *buffer, uint32_t length);
uint8_t rt2501_beacon(void *buffer, uint32_t length);
int32_t rt2501_set_key(uint8_t index, uint8_t *key, uint8_t *txmic, uint8_t *rxmic, uint8_t cipher);
uint16_t rt2501_txtime(uint32_t len, uint8_t rate);

extern uint8_t rt2501_mac[IEEE80211_ADDR_LEN];



int32_t rt2501_driver_install(void);
int32_t rt2501_state(void);
int32_t rt2501_rssi_average(void);
/* 100ms (approx.) timer. Must not be called in interrupt context. */
void rt2501_timer(void);
/* Must not be called in interrupt context */
void rt2501_setmode(int32_t mode, const uint8_t *ssid, uint8_t channel);
/* Must not be called in interrupt context */
void rt2501_scan(const uint8_t *ssid, rt2501_scan_callback callback, void *userparam);
/* Must not be called in interrupt context */
void rt2501_auth(const uint8_t *ssid, const uint8_t *mac,
		 const uint8_t *bssid, uint8_t channel,
		 uint16_t rateset,
		 uint8_t authmode,
		 uint8_t encryption,
		 const uint8_t *key);
/* Must not be called in interrupt context */
struct rt2501buffer *rt2501_receive(void);
/* Must not be called in interrupt context if mayblock=1 */
int32_t rt2501_send(const uint8_t *frame, uint32_t length, const uint8_t *dest_mac,
		int32_t lowrate, int32_t mayblock);

#endif /* _RT2501_H_ */
