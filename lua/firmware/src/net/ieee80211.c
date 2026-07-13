/**
 * @file ieee80211.c
 * @author Sebastien Bourdeauducq - 2006 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief RT2501 Wifi/Network driver
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "common.h"

#include "utils/delay.h"
#include "utils/debug.h"
#include "utils/mem.h"

#include "usb/hcd.h"
#include "usb/hcdmem.h"
#include "usb/usbh.h"
#include "usb/usbctrl.h"

#include "usb/rt2501usb.h"
#include "usb/rt2501usb_hw.h"
#include "usb/rt2501usb_buffer.h"
#include "usb/rt2501usb_io.h"

#include "net/ieee80211.h"
#include "net/eapol.h"

/* TEMPORARY -Os WPA-join bisect (see Makefile O1_DIRS/O1_SRCS log): join
 * tracks ieee80211_associate's opt level across 8 hardware runs, yet its -Os
 * and -O1 codegen are proven behaviorally equivalent (byte-identical assoc
 * frame and call args - see the Makefile log). So this annotation MASKS the
 * real bug via timing/bus-pattern side effects rather than fixing a
 * miscompile. Keep it (it makes join work) but don't trust it as a fix;
 * remove once the true cause is instrumented down. Cross-level inlining is
 * blocked, so an O1_FN function's static -Os callees stay out-of-line at
 * -Os - the annotation tests exactly the annotated function's own code. */
#define O1_FN __attribute__((optimize("O1")))


const uint8_t ieee80211_broadcast_address[IEEE80211_ADDR_LEN] =
{ 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
const uint8_t ieee80211_null_address[IEEE80211_ADDR_LEN] =
{ 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
const uint8_t ieee80211_multicast_address[IEEE80211_ADDR_LEN] =
{ 0x01, 0x00, 0x5e, 0xff, 0xff, 0xff };

static const uint8_t ieee80211_vendor_wpa_id[] =
{ 0x00, 0x50, 0xf2, 0x01, 0x01, 0x00 };

static const uint8_t ieee80211_wpa_oui[IEEE80211_OUI_LEN] =
{ 0x00, 0x50, 0xf2, 0x00 };

static const uint8_t ieee80211_wpa2_oui[IEEE80211_OUI_LEN] =
{ 0x00, 0x0f, 0xac, 0x00 };

int32_t ieee80211_mode;
int32_t ieee80211_state;
uint32_t ieee80211_timeout;
static rt2501_scan_callback ieee80211_scallback;
static void *ieee80211_scallback_userparam;

uint8_t ieee80211_assoc_mac[IEEE80211_ADDR_LEN];
uint8_t ieee80211_assoc_bssid[IEEE80211_ADDR_LEN];
uint8_t ieee80211_assoc_ssid[IEEE80211_SSID_MAXLEN+1];
uint8_t ieee80211_assoc_channel;
uint16_t ieee80211_assoc_rateset;

static int16_t ieee80211_rssi_samples[RT2501_RSSI_SAMPLES];
static int16_t ieee80211_rssi_sample_index;
static int16_t ieee80211_rssi_average;

uint8_t ieee80211_authmode;
uint8_t ieee80211_encryption;
uint8_t ieee80211_key[IEEE80211_MAX_KEYLEN];


static struct ieee80211_sta_state ieee80211_associated_sta[RT2501_MAX_ASSOCIATED_STA];

/* IEEE80211_RATEMASK_* */
/* filled at auth with the lowest TX rate, updated according to RSSI when in RUN state and Managed mode */
/* also filled when entering Master mode */
static uint16_t ieee80211_txrate;
/* filled at auth and when entering Master mode, does not change */
static uint16_t ieee80211_lowest_txrate;

static uint16_t ieee80211_rate_to_mask(uint8_t rate)
{
	switch(rate) {
		case 2:
			return IEEE80211_RATEMASK_1;
		case 4:
			return IEEE80211_RATEMASK_2;
		case 11:
			return IEEE80211_RATEMASK_5_5;
		case 22:
			return IEEE80211_RATEMASK_11;
		case 12:
			return IEEE80211_RATEMASK_6;
		case 18:
			return IEEE80211_RATEMASK_9;
		case 24:
			return IEEE80211_RATEMASK_12;
		case 36:
			return IEEE80211_RATEMASK_18;
		case 48:
			return IEEE80211_RATEMASK_24;
		case 72:
			return IEEE80211_RATEMASK_36;
		case 96:
			return IEEE80211_RATEMASK_48;
		case 108:
			return IEEE80211_RATEMASK_54;
		default:
#ifdef DEBUG_WIFI
			sprintf(dbg_buffer,
				"Unknown rate in ieee80211_rate_to_mask (%d)"EOL,
				rate);
			DBG_WIFI(dbg_buffer);
#endif
			return 0;
	}
}

static uint8_t ieee80211_mask_to_rate(uint16_t mask)
{
	switch(mask) {
		case IEEE80211_RATEMASK_1:
			return 2;
		case IEEE80211_RATEMASK_2:
			return 4;
		case IEEE80211_RATEMASK_5_5:
			return 11;
		case IEEE80211_RATEMASK_11:
			return 22;
		case IEEE80211_RATEMASK_6:
			return 12;
		case IEEE80211_RATEMASK_9:
			return 18;
		case IEEE80211_RATEMASK_12:
			return 24;
		case IEEE80211_RATEMASK_18:
			return 36;
		case IEEE80211_RATEMASK_24:
			return 48;
		case IEEE80211_RATEMASK_36:
			return 72;
		case IEEE80211_RATEMASK_48:
			return 96;
		case IEEE80211_RATEMASK_54:
			return 108;
		default:
			DBG_WIFI("Unknown mask in ieee80211_mask_to_rate"EOL);
			return 0;
	}
}

static uint8_t ieee80211_mask_to_rt2501rate(uint16_t mask)
{
	switch(mask) {
		case IEEE80211_RATEMASK_1:
			return RT2501_RATE_1;
		case IEEE80211_RATEMASK_2:
			return RT2501_RATE_2;
		case IEEE80211_RATEMASK_5_5:
			return RT2501_RATE_5_5;
		case IEEE80211_RATEMASK_11:
			return RT2501_RATE_11;
		case IEEE80211_RATEMASK_6:
			return RT2501_RATE_6;
		case IEEE80211_RATEMASK_9:
			return RT2501_RATE_9;
		case IEEE80211_RATEMASK_24:
			return RT2501_RATE_24;
		case IEEE80211_RATEMASK_36:
			return RT2501_RATE_36;
		case IEEE80211_RATEMASK_48:
			return RT2501_RATE_48;
		case IEEE80211_RATEMASK_54:
			return RT2501_RATE_54;
		default:
			return RT2501_RATE_1;
	}
}

static uint16_t ieee80211_mask_to_rt2501mask(unsigned short ieee80211mask)
{
	uint16_t rt2501mask;

	rt2501mask = 0;
	if(ieee80211mask & IEEE80211_RATEMASK_1) rt2501mask |= RT2501_RATEMASK_1;
	if(ieee80211mask & IEEE80211_RATEMASK_2) rt2501mask |= RT2501_RATEMASK_2;
	if(ieee80211mask & IEEE80211_RATEMASK_5_5) rt2501mask |= RT2501_RATEMASK_5_5;
	if(ieee80211mask & IEEE80211_RATEMASK_6) rt2501mask |= RT2501_RATEMASK_6;
	if(ieee80211mask & IEEE80211_RATEMASK_9) rt2501mask |= RT2501_RATEMASK_9;
	if(ieee80211mask & IEEE80211_RATEMASK_11) rt2501mask |= RT2501_RATEMASK_11;
	if(ieee80211mask & IEEE80211_RATEMASK_12) rt2501mask |= RT2501_RATEMASK_12;
	if(ieee80211mask & IEEE80211_RATEMASK_18) rt2501mask |= RT2501_RATEMASK_18;
	if(ieee80211mask & IEEE80211_RATEMASK_24) rt2501mask |= RT2501_RATEMASK_24;
	if(ieee80211mask & IEEE80211_RATEMASK_36) rt2501mask |= RT2501_RATEMASK_36;
	if(ieee80211mask & IEEE80211_RATEMASK_48) rt2501mask |= RT2501_RATEMASK_48;
	if(ieee80211mask & IEEE80211_RATEMASK_54) rt2501mask |= RT2501_RATEMASK_54;

	return rt2501mask;
}

static uint8_t ieee80211_crypt_to_rt2501cipher(uint8_t crypt)
{
	switch(crypt&0xF0) 
	{
		case IEEE80211_CRYPT_WEP:
      switch(crypt)
      {
        case IEEE80211_CRYPT_WEP64:
          return RT2501_CIPHER_WEP64;
        case IEEE80211_CRYPT_WEP128:
          return RT2501_CIPHER_WEP128;
        default:
					DBG_WIFI("Unknown WEP Crypt");
          break;
      };
      /* No break here would fall through into the WPA case below and
       * mis-parse crypt&0x0F (a WEP key-length subtype) as a WPA cipher.
       * Unknown WEP → the safe RT2501_CIPHER_NONE default at the bottom. */
      break;
    case IEEE80211_CRYPT_WPA:
    case IEEE80211_CRYPT_WPA2:
      switch(crypt&0x0F)
      {
        case IEEE80211_CIPHER_TKIP:
          return RT2501_CIPHER_TKIP;
        case IEEE80211_CIPHER_CCMP:
          return RT2501_CIPHER_AES;
        default:
					DBG_WIFI("Unknown WPA Crypt");
          break;
      };
      break;
    case IEEE80211_CRYPT_NONE:
			return RT2501_CIPHER_NONE;
		default:
			DBG_WIFI("Unknown IEEE80211 Crypt");
	}
	return RT2501_CIPHER_NONE;
}

/* IEEE80211_RATEMASK_* */
static uint16_t ieee80211_find_closest_rate(uint16_t wanted_rate)
{
	uint16_t i;

	i = wanted_rate;
	while(!(ieee80211_assoc_rateset & i) && (i != 1)) i = (i >> 1);
	if(ieee80211_assoc_rateset & i) return i;

	if(ieee80211_assoc_rateset == 0) return IEEE80211_RATEMASK_1;
	i = 1;
	/* always terminates because ieee80211_assoc_rateset != 0 */
	while(!(ieee80211_assoc_rateset & i)) i = (i << 1);
	return i;
}

static void ieee80211_new_rssi_sample(int16_t rssi)
{
	ieee80211_rssi_samples[ieee80211_rssi_sample_index++] = rssi;
	if(ieee80211_rssi_sample_index == RT2501_RSSI_SAMPLES) {
		uint32_t i;
		int32_t a;
		uint16_t wanted_txrate;

		a = 0;
		for(i=0;i<RT2501_RSSI_SAMPLES;i++)
			a += ieee80211_rssi_samples[i];
		ieee80211_rssi_average = a/RT2501_RSSI_SAMPLES;
		ieee80211_rssi_sample_index = 0;

		/* Update TX rate */
		if(ieee80211_rssi_average >= -65)
			wanted_txrate = IEEE80211_RATEMASK_54;
		else if(ieee80211_rssi_average >= -66)
			wanted_txrate = IEEE80211_RATEMASK_48;
		else if(ieee80211_rssi_average >= -70)
			wanted_txrate = IEEE80211_RATEMASK_36;
		else if(ieee80211_rssi_average >= -74)
			wanted_txrate = IEEE80211_RATEMASK_24;
		else if(ieee80211_rssi_average >= -77)
			wanted_txrate = IEEE80211_RATEMASK_18;
		else if(ieee80211_rssi_average >= -79)
			wanted_txrate = IEEE80211_RATEMASK_12;
		else if(ieee80211_rssi_average >= -81)
			wanted_txrate = IEEE80211_RATEMASK_11;
		else if(ieee80211_rssi_average >= -84)
			wanted_txrate = IEEE80211_RATEMASK_5_5;
		else if(ieee80211_rssi_average >= -85)
			wanted_txrate = IEEE80211_RATEMASK_2;
		else
			wanted_txrate = IEEE80211_RATEMASK_1;
		ieee80211_txrate = ieee80211_find_closest_rate(wanted_txrate);
	}
}

static int32_t ieee80211_find_sta_slot_auth(uint8_t *sta_mac)
{
	int32_t i;

	/*
	Is the MAC address of the station already in the table ?
	It can happen if the station is retransmitting its auth because it
	failed to receive our reply.
	*/
	for(i=0;i<RT2501_MAX_ASSOCIATED_STA;i++) {
		if((ieee80211_associated_sta[i].state != IEEE80211_S_IDLE)
				  && (memcmp(ieee80211_associated_sta[i].mac, sta_mac,
				      IEEE80211_ADDR_LEN) == 0)) return i;
	}
	/* No, search for an empty slot */
	for(i=0;i<RT2501_MAX_ASSOCIATED_STA;i++) {
		if(ieee80211_associated_sta[i].state == IEEE80211_S_IDLE)
			return i;
	}
	/* No slot found ! */
	return -1;
}

static int32_t ieee80211_find_sta_slot(uint8_t *sta_mac)
{
	int32_t i;

	for(i=0;i<RT2501_MAX_ASSOCIATED_STA;i++) {
		if((ieee80211_associated_sta[i].state != IEEE80211_S_IDLE)
				  && (memcmp(ieee80211_associated_sta[i].mac, sta_mac,
				      IEEE80211_ADDR_LEN) == 0)) return i;
	}
	return -1;
}

static void ieee80211_send_auth(uint8_t *destination_mac,
				uint16_t algorithm,
				uint16_t auth_seq,
				uint16_t status)
{
//#pragma pack(1)
	struct {
	TXD_STRUC txd;
	struct ieee80211_frame header;
	uint8_t auth[6];
	} *auth;
//#pragma pack()
	uint16_t duration;

#ifdef DEBUG_WIFI
	sprintf(dbg_buffer, "Sending auth to %02x:%02x:%02x:%02x:%02x:%02x, status %d"EOL,
		destination_mac[0], destination_mac[1], destination_mac[2],
		destination_mac[3], destination_mac[4], destination_mac[5],
		status);
	DBG_WIFI(dbg_buffer);
#endif

	disable_ohci_irq();
	auth = hcd_malloc(sizeof(*auth)+7, COMRAM,8);
	enable_ohci_irq();
	if(auth == NULL) {
		DBG_WIFI("hcd_malloc failed in ieee80211_send_auth"EOL);
		return;
	}
	auth->header.i_fc[0] = IEEE80211_FC0_TYPE_MGT|IEEE80211_FC0_SUBTYPE_AUTH
			|(IEEE80211_FC0_VERSION_0 << IEEE80211_FC0_VERSION_SHIFT);
	auth->header.i_fc[1] = IEEE80211_FC1_DIR_NODS;
	duration = rt2501_txtime(sizeof(*auth)-sizeof(TXD_STRUC), ieee80211_mask_to_rate(ieee80211_lowest_txrate))+IEEE80211_SIFS;
	auth->header.i_dur[0] = ((duration & 0x00ff) >> 0);
	auth->header.i_dur[1] = ((duration & 0xff00) >> 8);
	memcpy(auth->header.i_addr1, destination_mac, IEEE80211_ADDR_LEN);
	memcpy(auth->header.i_addr2, rt2501_mac, IEEE80211_ADDR_LEN);
	memcpy(auth->header.i_addr3, ieee80211_assoc_bssid, IEEE80211_ADDR_LEN);
	auth->header.i_seq[0] = 0;
	auth->header.i_seq[1] = 0;

	/* algo[2], seq[2], status[2] */
	auth->auth[0] = ((algorithm & 0x00ff) >> 0);
	auth->auth[1] = ((algorithm & 0xff00) >> 8);
	auth->auth[2] = ((auth_seq & 0x00ff) >> 0);
	auth->auth[3] = ((auth_seq & 0xff00) >> 8);
	auth->auth[4] = ((status & 0x00ff) >> 0);
	auth->auth[5] = ((status & 0xff00) >> 8);

	rt2501_make_tx_descriptor(
	&auth->txd,
	RT2501_CIPHER_NONE,			/* CipherAlg */
	0,					/* KeyTable */
	0,					/* KeyIdx */
	1,					/* Ack */
	0,					/* Fragment */
	0,					/* InsTimestamp */
	1,					/* RetryMode */
	0,					/* Ifs */
	/* Rate */
	ieee80211_mask_to_rt2501rate(ieee80211_lowest_txrate),
	sizeof(*auth)-sizeof(TXD_STRUC),	/* Length */
	0,					/* QueIdx */
	0					/* PacketId */
				 );

	if(!rt2501_tx(auth, sizeof(*auth)))
  {
		DBG_WIFI("TX error in ieee80211_send_auth"EOL);
  }
}

static void ieee80211_send_assocresp(uint8_t *sta_mac, uint16_t status,
				     uint16_t assoc_id)
{
//#pragma pack(1)
	struct {
	TXD_STRUC txd;
	struct ieee80211_frame header;
	uint8_t assoc[6+2+8+2+4];
	} *assoc;
//#pragma pack()
	uint8_t *write_ptr;
	uint32_t frame_length;
	uint16_t duration;

#ifdef DEBUG_WIFI
	sprintf(dbg_buffer, "Sending assoc reply, status %d"EOL, status);
	DBG_WIFI(dbg_buffer);
#endif

	disable_ohci_irq();
	assoc = hcd_malloc(sizeof(*assoc)+7, COMRAM,9);
	enable_ohci_irq();
	if(assoc == NULL) {
		DBG_WIFI("hcd_malloc failed in ieee80211_send_assocresp"EOL);
		return ;
	}

	assoc->header.i_fc[0] = IEEE80211_FC0_TYPE_MGT|IEEE80211_FC0_SUBTYPE_ASSOC_RESP
			|(IEEE80211_FC0_VERSION_0 << IEEE80211_FC0_VERSION_SHIFT);
	assoc->header.i_fc[1] = IEEE80211_FC1_DIR_NODS;
	memcpy(assoc->header.i_addr1, sta_mac, IEEE80211_ADDR_LEN);
	memcpy(assoc->header.i_addr2, rt2501_mac, IEEE80211_ADDR_LEN);
	memcpy(assoc->header.i_addr3, ieee80211_assoc_bssid, IEEE80211_ADDR_LEN);
	assoc->header.i_seq[0] = 0;
	assoc->header.i_seq[1] = 0;

	write_ptr = assoc->assoc;
	/* FIXED PARAMETERS */
	*(write_ptr++) = (IEEE80211_CAPINFO_ESS & 0x00ff) >> 0;
	*(write_ptr++) = (IEEE80211_CAPINFO_ESS & 0xff00) >> 8;
	*(write_ptr++) = (status & 0x00ff) >> 0;
	*(write_ptr++) = (status & 0xff00) >> 8;
	*(write_ptr++) = (assoc_id & 0x00ff) >> 0;
	*(write_ptr++) = (assoc_id & 0xff00) >> 8;
	/* TAGGED PARAMETERS */
	/* Supported Rates */
	*(write_ptr++) = IEEE80211_ELEMID_RATES;
	*(write_ptr++) = 8;
	*(write_ptr++) = 0x02;
	*(write_ptr++) = 0x04;
	*(write_ptr++) = 0x0b;
	*(write_ptr++) = 0x16;
	*(write_ptr++) = 0x24;
	*(write_ptr++) = 0x30;
	*(write_ptr++) = 0x48;
	*(write_ptr++) = 0x6c;
	/* Extended Supported Rates */
	*(write_ptr++) = IEEE80211_ELEMID_XRATES;
	*(write_ptr++) = 4;
	*(write_ptr++) = 0x0c;
	*(write_ptr++) = 0x12;
	*(write_ptr++) = 0x18;
	*(write_ptr++) = 0x60;

	frame_length = sizeof(struct ieee80211_frame)+((uint32_t)write_ptr - (uint32_t)assoc->assoc);

	duration = rt2501_txtime(frame_length, ieee80211_mask_to_rate(ieee80211_lowest_txrate))+IEEE80211_SIFS;
	assoc->header.i_dur[0] = ((duration & 0x00ff) >> 0);
	assoc->header.i_dur[1] = ((duration & 0xff00) >> 8);

	rt2501_make_tx_descriptor(
	&assoc->txd,
	RT2501_CIPHER_NONE,		/* CipherAlg */
	0,				/* KeyTable */
	0,				/* KeyIdx */
	1,				/* Ack */
	0,				/* Fragment */
	0,				/* InsTimestamp */
	1,				/* RetryMode */
	0,				/* Ifs */
	/* Rate */
	ieee80211_mask_to_rt2501rate(ieee80211_lowest_txrate),
	frame_length,			/* Length */
	0,				/* QueIdx */
	0				/* PacketId */
				 );

	if(!rt2501_tx(assoc, frame_length+sizeof(TXD_STRUC)))
  {
		DBG_WIFI("TX error in ieee80211_send_assoc"EOL);
  }
}

static void ieee80211_auth_sta(uint8_t *sta_mac, uint16_t algorithm,
			       uint16_t auth_seq,
			       uint16_t status)
{
	int32_t sta_slot;

	if(algorithm != IEEE80211_AUTH_ALG_OPEN) {
		ieee80211_send_auth(sta_mac, algorithm, auth_seq+1, IEEE80211_STATUS_ALG);
		return;
	}
	if(auth_seq != IEEE80211_AUTH_OPEN_REQUEST) {
		ieee80211_send_auth(sta_mac, algorithm, auth_seq+1, IEEE80211_STATUS_SEQUENCE);
		return;
	}
	if(status != IEEE80211_STATUS_SUCCESS) {
		ieee80211_send_auth(sta_mac, algorithm, auth_seq+1, IEEE80211_STATUS_OTHER);
		return;
	}

	/*
	Find an empty slot, or a slot that has the MAC address of the station
	(see comments above).
	*/
	sta_slot = ieee80211_find_sta_slot_auth(sta_mac);
	if(sta_slot == -1) {
		ieee80211_send_auth(sta_mac,
				    IEEE80211_AUTH_ALG_OPEN,
				    IEEE80211_AUTH_OPEN_RESPONSE,
				    IEEE80211_STATUS_TOO_MANY_STATIONS);
		return;
	}
	ieee80211_associated_sta[sta_slot].state = IEEE80211_S_ASSOC;
	ieee80211_associated_sta[sta_slot].timer = IEEE80211_STA_ASSOC_TIMEOUT;
	memcpy(ieee80211_associated_sta[sta_slot].mac, sta_mac, IEEE80211_ADDR_LEN);
	ieee80211_send_auth(sta_mac,
			    IEEE80211_AUTH_ALG_OPEN,
			    IEEE80211_AUTH_OPEN_RESPONSE,
			    IEEE80211_STATUS_SUCCESS);
}

static void ieee80211_deauth_sta(int32_t index, uint16_t reason)
{
//#pragma pack(1)
	struct {
	TXD_STRUC txd;
	struct ieee80211_frame header;
	uint8_t deauth[2];
	} *deauth;
//#pragma pack()
	uint16_t duration;

#ifdef DEBUG_WIFI
	sprintf(dbg_buffer, "Deauthenticating %02x:%02x:%02x:%02x:%02x:%02x"EOL,
		ieee80211_associated_sta[index].mac[0],
		ieee80211_associated_sta[index].mac[1],
		ieee80211_associated_sta[index].mac[2],
		ieee80211_associated_sta[index].mac[3],
		ieee80211_associated_sta[index].mac[4],
		ieee80211_associated_sta[index].mac[5]);
	DBG_WIFI(dbg_buffer);
#endif

	ieee80211_associated_sta[index].state = IEEE80211_S_IDLE;

	disable_ohci_irq();
	deauth = hcd_malloc(sizeof(*deauth)+7, COMRAM,10);
	enable_ohci_irq();
	if(deauth == NULL) {
		DBG_WIFI("hcd_malloc failed in ieee80211_deauth_sta"EOL);
		return;
	}
	deauth->header.i_fc[0] = IEEE80211_FC0_TYPE_MGT|IEEE80211_FC0_SUBTYPE_DEAUTH
			|(IEEE80211_FC0_VERSION_0 << IEEE80211_FC0_VERSION_SHIFT);
	deauth->header.i_fc[1] = IEEE80211_FC1_DIR_NODS;
	duration = rt2501_txtime(sizeof(*deauth)-sizeof(TXD_STRUC), ieee80211_mask_to_rate(ieee80211_lowest_txrate))+IEEE80211_SIFS;
	deauth->header.i_dur[0] = ((duration & 0x00ff) >> 0);
	deauth->header.i_dur[1] = ((duration & 0xff00) >> 8);
	memcpy(deauth->header.i_addr1, ieee80211_associated_sta[index].mac, IEEE80211_ADDR_LEN);
	memcpy(deauth->header.i_addr2, rt2501_mac, IEEE80211_ADDR_LEN);
	memcpy(deauth->header.i_addr3, ieee80211_assoc_bssid, IEEE80211_ADDR_LEN);
	deauth->header.i_seq[0] = 0;
	deauth->header.i_seq[1] = 0;

	deauth->deauth[0] = ((reason & 0x00ff) >> 0);
	deauth->deauth[1] = ((reason & 0xff00) >> 8);

	rt2501_make_tx_descriptor(
	&deauth->txd,
	RT2501_CIPHER_NONE,			/* CipherAlg */
	0,					/* KeyTable */
	0,					/* KeyIdx */
	1,					/* Ack */
	0,					/* Fragment */
	0,					/* InsTimestamp */
	1,					/* RetryMode */
	0,					/* Ifs */
	/* Rate */
	ieee80211_mask_to_rt2501rate(ieee80211_lowest_txrate),
	sizeof(*deauth)-sizeof(TXD_STRUC),	/* Length */
	0,					/* QueIdx */
	0					/* PacketId */
				 );

	if(!rt2501_tx(deauth, sizeof(*deauth)))
  {
		DBG_WIFI("TX error in ieee80211_deauth_sta"EOL);
  }
}

static void O1_FN ieee80211_associate(void)
{
//#pragma pack(1)
	struct {
	TXD_STRUC txd;
	struct ieee80211_frame header;
	uint8_t assoc[4
		+2+IEEE80211_SSID_MAXLEN
		+2+8
		+2+4
		+2+0x16];//+4];
	} *assoc;
//#pragma pack()
	uint8_t *write_ptr;
	uint32_t i, j;
	uint32_t frame_length;
	uint16_t duration;

	DBG_WIFI("Associating"EOL);

	disable_ohci_irq();
	assoc = hcd_malloc(sizeof(*assoc)+7, COMRAM,11);
	enable_ohci_irq();
	if(assoc == NULL) {
		DBG_WIFI("hcd_malloc failed in ieee80211_associate"EOL);
		return ;
	}

	assoc->header.i_fc[0] = IEEE80211_FC0_TYPE_MGT|IEEE80211_FC0_SUBTYPE_ASSOC_REQ
			|(IEEE80211_FC0_VERSION_0 << IEEE80211_FC0_VERSION_SHIFT);
	assoc->header.i_fc[1] = IEEE80211_FC1_DIR_NODS;
	memcpy(assoc->header.i_addr1, ieee80211_assoc_mac, IEEE80211_ADDR_LEN);
	memcpy(assoc->header.i_addr2, rt2501_mac, IEEE80211_ADDR_LEN);
	memcpy(assoc->header.i_addr3, ieee80211_assoc_bssid, IEEE80211_ADDR_LEN);
	assoc->header.i_seq[0] = 0;
	assoc->header.i_seq[1] = 0;

	write_ptr = assoc->assoc;
	/* FIXED PARAMETERS */
	/* Capability Information */
	*(write_ptr++) = ((IEEE80211_CAPINFO_ESS & 0x00ff) >> 0);
	*(write_ptr++) = ((IEEE80211_CAPINFO_ESS & 0xff00) >> 8);
	/* Listen Interval */
	*(write_ptr++) = 0x64;
	*(write_ptr++) = 0x00;
	/* TAGGED PARAMETERS */
	/* SSID */
	*(write_ptr++) = IEEE80211_ELEMID_SSID;
	j = strlen((char*)ieee80211_assoc_ssid);
	*(write_ptr++) = j;
	for(i=0;i<j;i++)
		*(write_ptr++) = ieee80211_assoc_ssid[i];
	/* Supported Rates */
	*(write_ptr++) = IEEE80211_ELEMID_RATES;
	*(write_ptr++) = 8;
	*(write_ptr++) = 0x02;
	*(write_ptr++) = 0x04;
	*(write_ptr++) = 0x0b;
	*(write_ptr++) = 0x16;
	*(write_ptr++) = 0x24;
	*(write_ptr++) = 0x30;
	*(write_ptr++) = 0x48;
	*(write_ptr++) = 0x6c;
	/* Extended Supported Rates */
	*(write_ptr++) = IEEE80211_ELEMID_XRATES;
	*(write_ptr++) = 4;
	*(write_ptr++) = 0x0c;
	*(write_ptr++) = 0x12;
	*(write_ptr++) = 0x18;
	*(write_ptr++) = 0x60;

  switch(ieee80211_encryption&0xF0)
  {
    case IEEE80211_CRYPT_WPA:
      *(write_ptr++) = IEEE80211_ELEMID_VENDOR;
      *(write_ptr++) = 0x16;//+4;
      for(i=0;i<sizeof(ieee80211_vendor_wpa_id);i++)
        *(write_ptr++) = ieee80211_vendor_wpa_id[i];
      /* Multicast cipher suite: TKIP */
      for(i=0;i<IEEE80211_OUI_LEN-1;i++)
        *(write_ptr++) = ieee80211_wpa_oui[i];
      *(write_ptr++) = IEEE80211_CIPHER_TKIP;
      /* 1 unicast cipher suite */
      *(write_ptr++) = 0x01;
      *(write_ptr++) = 0x00;
      /* Unicast cipher suites: TKIP or CCMP */
      for(i=0;i<IEEE80211_OUI_LEN-1;i++)
        *(write_ptr++) = ieee80211_wpa_oui[i];
      *(write_ptr++) = IEEE80211_CIPHER_TKIP;
      //for(i=0;i<IEEE80211_OUI_LEN;i++)
      //  *(write_ptr++) = ieee80211_wpa_oui[i];
      //*(write_ptr-1) = IEEE80211_CIPHER_TKIP;
      /* 1 auth key management suite */
      *(write_ptr++) = 0x01;
      *(write_ptr++) = 0x00;
      /* Auth key management suites: PSK */
      for(i=0;i<IEEE80211_OUI_LEN;i++)
        *(write_ptr++) = ieee80211_wpa_oui[i];
      *(write_ptr-1) = IEEE80211_AUTH_PSK;
      break;
    case IEEE80211_CRYPT_WPA2:
      /* Okay, so... Both TKIP and CCMP are available, pick CCMP
       * Note: This "hack" is quite fragile since it relies on hardcoded bit values
       *       that "magically" become IEEE80211 compatible cipher values.
       *       It also works because we only support two ciphers: TKIP and CCMP) as
       *       it uses all four available low bits in ieee80211_encryption
       *
       * There's one more line for the hack just before the "break"
       */
      if(ieee80211_encryption&0x08)       /* 0x08 = 0b1000 */     /* Group cipher */
        ieee80211_encryption &= ~(0x04);  /* 0x04 = 0b0100 */
      if(ieee80211_encryption&0x02)       /* 0x02 = 0b0010 */     /* Pairwise cipher */
        ieee80211_encryption &= ~0x01;    /* 0x01 = 0b0001 */
#ifdef DEBUG_WIFI
      sprintf(dbg_buffer,"ieee80211_assoc Encryption: 0x%02X"EOL,ieee80211_encryption);
      DBG_WIFI(dbg_buffer);
#endif

      *(write_ptr++) = 0x30;   // RSN IE
      *(write_ptr++) = 0x14;//+4; // Length
      *(write_ptr++) = 0x01;   // Version
      *(write_ptr++) = 0x00;   // Version
      /* Group cipher: AES */
      for(i=0;i<IEEE80211_OUI_LEN-1;i++)
        *(write_ptr++) = ieee80211_wpa2_oui[i];
      *(write_ptr++) = (ieee80211_encryption&0x0C)>>1; /* 0x0C = 0b1100 */
      /* 1 pairwise */
      *(write_ptr++) = 0x01;
      *(write_ptr++) = 0x00;
      /* Unicast cipher: TKIP or AES */
      for(i=0;i<IEEE80211_OUI_LEN-1;i++)
        *(write_ptr++) = ieee80211_wpa2_oui[i];
      *(write_ptr++) = (ieee80211_encryption&0x03)<<1; /* 0x03 = 0b0011 */
      /* 1 auth key management */
      *(write_ptr++) = 0x01;
      *(write_ptr++) = 0x00;
      /* Auth key management suites: PSK */
      for(i=0;i<IEEE80211_OUI_LEN;i++)
        *(write_ptr++) = ieee80211_wpa2_oui[i];
      *(write_ptr-1) = IEEE80211_AUTH_PSK;
          /* RSN Capability */
      *(write_ptr++) = 0x00;
      *(write_ptr++) = 0x00;
      /* Final line for this hack, reset "normal" cipher using pairwise cipher 0x03 = 0b0011 */
      ieee80211_encryption = (ieee80211_encryption&0xF0)|((ieee80211_encryption&0x03)<<1);
      break;
    default:
#ifdef DEBUG_WIFI
      sprintf(dbg_buffer,"ieee80211_assoc Unknown encryption: %d"EOL,ieee80211_encryption);
      DBG_WIFI(dbg_buffer);
#endif
      break;
  }
	
	frame_length = sizeof(struct ieee80211_frame)+(write_ptr - assoc->assoc);

	duration = rt2501_txtime(frame_length, ieee80211_mask_to_rate(ieee80211_lowest_txrate))+IEEE80211_SIFS;
	assoc->header.i_dur[0] = ((duration & 0x00ff) >> 0);
	assoc->header.i_dur[1] = ((duration & 0xff00) >> 8);

	rt2501_make_tx_descriptor(
	&assoc->txd,
	RT2501_CIPHER_NONE,		/* CipherAlg */
	0,				/* KeyTable */
	0,				/* KeyIdx */
	1,				/* Ack */
	0,				/* Fragment */
	0,				/* InsTimestamp */
	1,				/* RetryMode */
	0,				/* Ifs */
	/* Rate */
	ieee80211_mask_to_rt2501rate(ieee80211_lowest_txrate),
	frame_length,			/* Length */
	0,				/* QueIdx */
	0				/* PacketId */
				 );

	if(!rt2501_tx(assoc, sizeof(TXD_STRUC)+frame_length)) {
		DBG_WIFI("TX error in ieee80211_associate"EOL);
		ieee80211_state = IEEE80211_S_IDLE;
		return;
	}

	ieee80211_state = IEEE80211_S_ASSOC;
	ieee80211_timeout = IEEE80211_ASSOC_TIMEOUT;
}

static void ieee80211_send_challenge_reply(uint8_t *challenge, uint32_t challenge_length)
{
//#pragma pack(1)
	struct {
	TXD_STRUC txd;
	struct ieee80211_frame header;
	uint8_t auth[6+2+IEEE80211_CHALLENGE_LEN];
	} *auth;
//#pragma pack()
	uint16_t duration;
	uint8_t *write_ptr;
	uint32_t i;
	uint32_t frame_length;

	DBG_WIFI("Replying to challenge"EOL);
	if(challenge_length != IEEE80211_CHALLENGE_LEN) {
		DBG_WIFI("Incorrect challenge received (length)"EOL);
		return;
	}

	disable_ohci_irq();
	auth = hcd_malloc(sizeof(*auth)+7, COMRAM,12);
	enable_ohci_irq();
	if(auth == NULL) {
		DBG_WIFI("hcd_malloc failed in ieee80211_send_challenge_reply"EOL);
		return ;
	}

	auth->header.i_fc[0] = IEEE80211_FC0_TYPE_MGT|IEEE80211_FC0_SUBTYPE_AUTH
			|(IEEE80211_FC0_VERSION_0 << IEEE80211_FC0_VERSION_SHIFT);
	auth->header.i_fc[1] = IEEE80211_FC1_DIR_NODS|IEEE80211_FC1_PROTECTED;
	memcpy(auth->header.i_addr1, ieee80211_assoc_mac, IEEE80211_ADDR_LEN);
	memcpy(auth->header.i_addr2, rt2501_mac, IEEE80211_ADDR_LEN);
	memcpy(auth->header.i_addr3, ieee80211_assoc_bssid, IEEE80211_ADDR_LEN);
	auth->header.i_seq[0] = 0;
	auth->header.i_seq[1] = 0;

	write_ptr = auth->auth;
	/* FIXED PARAMETERS */
	*(write_ptr++) = ((IEEE80211_AUTH_ALG_SHARED & 0x00ff) >> 0);
	*(write_ptr++) = ((IEEE80211_AUTH_ALG_SHARED & 0xff00) >> 8);
	*(write_ptr++) = ((IEEE80211_AUTH_SHARED_RESPONSE & 0x00ff) >> 0);
	*(write_ptr++) = ((IEEE80211_AUTH_SHARED_RESPONSE & 0xff00) >> 8);
	*(write_ptr++) = ((IEEE80211_STATUS_SUCCESS & 0x00ff) >> 0);
	*(write_ptr++) = ((IEEE80211_STATUS_SUCCESS & 0xff00) >> 8);
	/* TAGGED PARAMETERS */
	/* Challenge */
	*(write_ptr++) = IEEE80211_ELEMID_CHALLENGE;
	*(write_ptr++) = IEEE80211_CHALLENGE_LEN;
	for(i=0;i<IEEE80211_CHALLENGE_LEN;i++)
		*(write_ptr++) = challenge[i];

	frame_length = sizeof(struct ieee80211_frame)+(write_ptr - auth->auth);

	duration = rt2501_txtime(frame_length, ieee80211_mask_to_rate(ieee80211_lowest_txrate))+IEEE80211_SIFS;
	auth->header.i_dur[0] = ((duration & 0x00ff) >> 0);
	auth->header.i_dur[1] = ((duration & 0xff00) >> 8);

	rt2501_make_tx_descriptor(
	&auth->txd,
	/* CipherAlg */
	ieee80211_crypt_to_rt2501cipher(ieee80211_encryption),
	0,				/* KeyTable */
	0,				/* KeyIdx */
	1,				/* Ack */
	0,				/* Fragment */
	0,				/* InsTimestamp */
	1,				/* RetryMode */
	0,				/* Ifs */
	/* Rate */
	ieee80211_mask_to_rt2501rate(ieee80211_lowest_txrate),
	frame_length,			/* Length */
	0,				/* QueIdx */
	0				/* PacketId */
				 );
	auth->txd.Iv = rand() & 0x00ffffff;
	auth->txd.IvOffset = sizeof(struct ieee80211_frame);

#ifdef DEBUG_WIFI
	sprintf(dbg_buffer, "IV = 0x%08lx, offset 0x%04x"EOL, auth->txd.Iv, auth->txd.IvOffset);
	DBG_WIFI(dbg_buffer);
#endif

	if(!rt2501_tx(auth, sizeof(TXD_STRUC)+frame_length)) {
		DBG_WIFI("TX error in ieee80211_send_challenge_reply"EOL);
		ieee80211_state = IEEE80211_S_IDLE;
		return;
	}
}

static void ieee80211_input_shared_auth(uint16_t algorithm,
					uint16_t auth_seq,
					uint16_t status,
					uint8_t *tagged_parameters, uint32_t tagged_length)
{
	if(algorithm != IEEE80211_AUTH_ALG_SHARED) {
		DBG_WIFI("Shared auth failed: unexpected algo reply"EOL);
		ieee80211_state = IEEE80211_S_IDLE;
		return;
	}
	if(status != IEEE80211_STATUS_SUCCESS) {
		DBG_WIFI("Shared auth failed: denied by AP"EOL);
		ieee80211_state = IEEE80211_S_IDLE;
		return;
	}
	switch(auth_seq) {
		case IEEE80211_AUTH_SHARED_CHALLENGE: {
			uint8_t *frame_current, *frame_end;
			uint8_t *challenge;
			uint8_t challenge_length;

			DBG_WIFI("Received challenge"EOL);

			challenge = NULL;
			challenge_length = 0;
			frame_current = tagged_parameters;
			frame_end = tagged_parameters + tagged_length;

			while(frame_current < frame_end) {
				if(frame_current[0] == IEEE80211_ELEMID_CHALLENGE) {
					challenge = &frame_current[2];
					challenge_length = frame_current[1];
				}
				frame_current += (frame_current[1] + 2);
			}

			if((challenge == NULL) || (challenge_length == 0)) {
				DBG_WIFI("Shared auth failed: tagged parameters do not contain challenge"EOL);
				ieee80211_state = IEEE80211_S_IDLE;
				break;
			}

			ieee80211_send_challenge_reply(challenge, challenge_length);

			break;
		}
		case IEEE80211_AUTH_SHARED_PASS:
			DBG_WIFI("Shared auth OK !"EOL);
			ieee80211_associate();
			break;
		default:
			DBG_WIFI("Shared auth failed: unexpected sequence reply"EOL);
			ieee80211_state = IEEE80211_S_IDLE;
			break;
	}
}

static void ieee80211_send_probe_response(uint8_t *dest_mac)
{
//#pragma pack(1)
	struct {
		TXD_STRUC txd;
		struct ieee80211_frame header;
		uint8_t presp[4 				/* Fixed Parameters */
			+2+IEEE80211_SSID_MAXLEN 	/* SSID */
			+2+8				/* Rates */
			+2+4				/* Extended rates */
			+2+1];				/* Channel */
	} *presp;
//#pragma pack()
	uint8_t *write_ptr;
	uint32_t i, j;
	uint32_t frame_length;
	uint16_t duration;

#ifdef DEBUG_WIFI
	sprintf(dbg_buffer, "Sending probe response to %02x:%02x:%02x:%02x:%02x:%02x"EOL,
		dest_mac[0], dest_mac[1], dest_mac[2], dest_mac[3], dest_mac[4], dest_mac[5]);
	DBG_WIFI(dbg_buffer);
#endif

	disable_ohci_irq();
	presp = hcd_malloc(sizeof(*presp)+7, COMRAM,13);
	enable_ohci_irq();
	if(presp == NULL) {
		DBG_WIFI("hcd_malloc failed in ieee80211_send_probe_response"EOL);
		return ;
	}

	presp->header.i_fc[0] = IEEE80211_FC0_TYPE_MGT|IEEE80211_FC0_SUBTYPE_PROBE_RESP
			|(IEEE80211_FC0_VERSION_0 << IEEE80211_FC0_VERSION_SHIFT);
	presp->header.i_fc[1] = IEEE80211_FC1_DIR_NODS;
	memcpy(presp->header.i_addr1, dest_mac, IEEE80211_ADDR_LEN);
	memcpy(presp->header.i_addr2, rt2501_mac, IEEE80211_ADDR_LEN);
	memcpy(presp->header.i_addr3, ieee80211_assoc_bssid, IEEE80211_ADDR_LEN);
	presp->header.i_seq[0] = 0;
	presp->header.i_seq[1] = 0;

	write_ptr = presp->presp;
	/* FIXED PARAMETERS */
	/* Timestamp, handled by Ralink chip */
	*(write_ptr++) = 0x00;
	*(write_ptr++) = 0x00;
	*(write_ptr++) = 0x00;
	*(write_ptr++) = 0x00;
	*(write_ptr++) = 0x00;
	*(write_ptr++) = 0x00;
	*(write_ptr++) = 0x00;
	*(write_ptr++) = 0x00;
	/* Beacon Interval */
	*(write_ptr++) = 0x64;
	*(write_ptr++) = 0x00;
	/* Capability Information */
	*(write_ptr++) = ((IEEE80211_CAPINFO_ESS & 0x00ff) >> 0);
	*(write_ptr++) = ((IEEE80211_CAPINFO_ESS & 0xff00) >> 8);
	/* TAGGED PARAMETERS */
	/* SSID */
	*(write_ptr++) = IEEE80211_ELEMID_SSID;
	j = strlen((char*)ieee80211_assoc_ssid);
	*(write_ptr++) = j;
	for(i=0;i<j;i++)
		*(write_ptr++) = ieee80211_assoc_ssid[i];
	/* Supported Rates */
	*(write_ptr++) = IEEE80211_ELEMID_RATES;
	*(write_ptr++) = 1;
	*(write_ptr++) = 0x82;
	/* Current Channel */
	*(write_ptr++) = IEEE80211_ELEMID_DSPARMS;
	*(write_ptr++) = 1;
	*(write_ptr++) = ieee80211_assoc_channel;

	frame_length = sizeof(struct ieee80211_frame)+((uintptr_t)write_ptr - (uintptr_t)presp->presp);

	duration = rt2501_txtime(frame_length, 2)+IEEE80211_SIFS;
	presp->header.i_dur[0] = ((duration & 0x00ff) >> 0);
	presp->header.i_dur[1] = ((duration & 0xff00) >> 8);

	rt2501_make_tx_descriptor(
	&presp->txd,
	RT2501_CIPHER_NONE,		/* CipherAlg */
	0,				/* KeyTable */
	0,				/* KeyIdx */
	0,				/* Ack */
	0,				/* Fragment */
	1,				/* InsTimestamp */
	1,				/* RetryMode */
	0,				/* Ifs */
	RT2501_RATE_1,			/* Rate */
	frame_length,			/* Length */
	0,				/* QueIdx */
	0				/* PacketId */
	);

	if(!rt2501_tx(presp, sizeof(TXD_STRUC)+frame_length)) {
		DBG_WIFI("TX error in ieee80211_send_probe_response"EOL);
		return;
	}
}

static void ieee80211_input_mgt(uint8_t *frame, uint32_t length, int16_t rssi)
{
	struct ieee80211_frame *fr = (struct ieee80211_frame *)frame;
	uint8_t *frame_current, *frame_end;
	uint32_t i;

	if(length < sizeof(struct ieee80211_frame)) return;
	/* All management frames must be flagged "No DS" */
	if((fr->i_fc[1] & IEEE80211_FC1_DIR_MASK) != IEEE80211_FC1_DIR_NODS)
		return;
	if((ieee80211_mode == IEEE80211_M_MANAGED) && (ieee80211_state == IEEE80211_S_RUN))
		if(memcmp(fr->i_addr2, ieee80211_assoc_mac, IEEE80211_ADDR_LEN) == 0)
			ieee80211_new_rssi_sample(rssi);
	frame_current = frame+sizeof(struct ieee80211_frame);
	frame_end = frame+length;
	switch(fr->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) {
		case IEEE80211_FC0_SUBTYPE_PROBE_REQ:
      DBG_WIFI("Probe_Req"EOL);
			if(ieee80211_mode == IEEE80211_M_MASTER) {
				uint8_t ssid_present;
				char ssid[IEEE80211_SSID_MAXLEN+1];

				ssid_present = 0;
				while(frame_current < frame_end) {
					if(frame_current[0] == IEEE80211_ELEMID_SSID) {
						ssid_present = 1;
						for(i=0;i<frame_current[1];i++)
							ssid[i] = frame_current[i+2];
						ssid[i] = 0;
					}
					frame_current += (frame_current[1] + 2);
				}
				if(ssid[0] == 0) ssid_present = 0;

#ifdef DEBUG_WIFI
				if(!ssid_present) {
					DBG_WIFI("Received probe request with no SSID"EOL);
				} else {
					sprintf(dbg_buffer, "Received probe request with SSID \"%s\""EOL, ssid);
					DBG_WIFI(dbg_buffer);
				}
#endif
				if((!ssid_present) || (strcmp(ssid, (char*)ieee80211_assoc_ssid) == 0))
					ieee80211_send_probe_response(fr->i_addr2);
			}
			break;
		case IEEE80211_FC0_SUBTYPE_PROBE_RESP:
      DBG_WIFI("Probe_Resp"EOL);
			/* fall through */
		case IEEE80211_FC0_SUBTYPE_BEACON:
			/*
			We're only interested in beacons and probe responses
			- while scanning in Managed mode (1)
			- when associated, to check the AP is still alive (2)
			*/
//			DBG_WIFI("[BEACON]"EOL);
//                   dump(frame,length);
			if(ieee80211_mode == IEEE80211_M_MANAGED) {
				if(ieee80211_state == IEEE80211_S_SCAN) {
					/* (1) */
					struct rt2501_scan_result scan_result;
					uint16_t capinfo;

					//DBG_WIFI("Received beacon/PR while scanning"EOL);

					if((frame_end - frame_current) < 12) return;

					capinfo = (frame_current[10] << 0)|(frame_current[11] << 8);
					frame_current += 12; /* skip timestamp, beacon interval, capinfo */

					scan_result.ssid[0] = 0;
					memcpy(scan_result.mac, fr->i_addr2, IEEE80211_ADDR_LEN);
					memcpy(scan_result.bssid, fr->i_addr3, IEEE80211_ADDR_LEN);
					scan_result.channel = 0;
					scan_result.rssi = rssi;
					scan_result.rateset = 0;
					if(capinfo & IEEE80211_CAPINFO_PRIVACY)
						scan_result.encryption = IEEE80211_CRYPT_WEP;
					else
						scan_result.encryption = IEEE80211_CRYPT_NONE;

					while(frame_current < frame_end) {
						switch(frame_current[0]) {
							case IEEE80211_ELEMID_SSID:
								if(frame_current[1] < sizeof(scan_result.ssid)) {
									for(i=0;i<frame_current[1];i++)
										scan_result.ssid[i] = frame_current[i+2];
									scan_result.ssid[i] = 0;
								}
								break;
							case IEEE80211_ELEMID_DSPARMS:
								scan_result.channel = frame_current[2];
								break;
							case IEEE80211_ELEMID_RATES:
							case IEEE80211_ELEMID_XRATES:
								for(i=0;i<frame_current[1];i++) {
									scan_result.rateset |= ieee80211_rate_to_mask(frame_current[i+2] & 0x7f);
#ifdef DEBUG_WIFI
									//sprintf(dbg_buffer, "supported rate:0x%02x"EOL, frame_current[i+2]);
									//DBG_WIFI(dbg_buffer);
#endif
								}
								break;
              case IEEE80211_ELEMID_RSN:
              {
                uint8_t *current;
                uint16_t count;
                uint8_t found;

                #ifdef DEBUG_WIFI
                //DBG_WIFI("RSN Information"EOL);
                //dump(frame_current,frame_current[1]);
                #endif
                //frame_current += frame_current[1];

                current = &frame_current[2];
                /* Element 1: Group cipher suites (OUIs) */
                count = (current[0] << 0)|(current[1] << 8);
                current += 2;
                found = 0;
                scan_result.encryption = 0;
                for(i=0;i<count;i++) {
                  if(memcmp(current, ieee80211_wpa2_oui,  IEEE80211_OUI_LEN-1) == 0)
                  {
                    if(*(current+IEEE80211_OUI_LEN-1) == IEEE80211_CIPHER_TKIP)
                    {
                      found = 1;
                      scan_result.encryption |= IEEE80211_CIPHER_TKIP<<1;
                      DBG_WIFI("GROUP TKIP supported"EOL);
                    }
                    else if(*(current+IEEE80211_OUI_LEN-1) == IEEE80211_CIPHER_CCMP)
                    {
                      found = 1;
                      scan_result.encryption |= IEEE80211_CIPHER_CCMP<<1;
                      DBG_WIFI("GROUP CCMP supported"EOL);
                    }
                  }
                  current += IEEE80211_OUI_LEN;
                }
                if(!found) {
                  scan_result.encryption = IEEE80211_CRYPT_UNSUPPORTED;
                  break;
                }
                /* Element 2: Pairwise cipher suites (OUIs) */
                count = (current[0] << 0)|(current[1] << 8);
                current += 2;
                found = 0;
                for(i=0;i<count;i++) {
                  if(memcmp(current, ieee80211_wpa2_oui,  IEEE80211_OUI_LEN-1) == 0)
                  {
                    if(*(current+IEEE80211_OUI_LEN-1) == IEEE80211_CIPHER_TKIP)
                    {
                      found = 1;
                      scan_result.encryption |= IEEE80211_CIPHER_TKIP>>1;
                      DBG_WIFI("Pairwise TKIP supported"EOL);
                    }
                    else if(*(current+IEEE80211_OUI_LEN-1) == IEEE80211_CIPHER_CCMP)
                    {
                      found = 1;
                      scan_result.encryption |= IEEE80211_CIPHER_CCMP>>1;
                      DBG_WIFI("Pairwise CCMP supported"EOL);
                    }
                  }
                  current += IEEE80211_OUI_LEN;
                }
                if(!found) {
                  scan_result.encryption = IEEE80211_CRYPT_UNSUPPORTED;
                  break;
                }
                /* Element 5: Auth key management suites (OUIs) */
                count = (current[0] << 0)|(current[1] << 8);
                current += 2;
                found = 0;
                for(i=0;i<count;i++) {
                  if(memcmp(current, ieee80211_wpa2_oui,  IEEE80211_OUI_LEN-1) == 0)
                  {
                    if(*(current+IEEE80211_OUI_LEN-1) == IEEE80211_AUTH_PSK)
                      found = 1;
                  }
                  current += IEEE80211_OUI_LEN;
                }
                if(!found) {
                  scan_result.encryption = IEEE80211_CRYPT_UNSUPPORTED;
                  break;
                }

                scan_result.encryption |= IEEE80211_CRYPT_WPA2;
                DBG_WIFI("WPA2 supported"EOL);
                frame_current += frame_current[1];
                break;
              }
							case IEEE80211_ELEMID_VENDOR:
              {
								/* Check for WPA */
								uint8_t *current;
								uint16_t count;
								int32_t found;

								/* Minimal size of a WPA element */
								if(frame_current[1] < 22) break;

                if(scan_result.encryption == IEEE80211_CRYPT_WPA2)
                  break;

								/* Check RSN IE */
								current = &frame_current[2];
								if(memcmp(current, ieee80211_vendor_wpa_id, sizeof(ieee80211_vendor_wpa_id)) != 0) break;
								current += sizeof(ieee80211_vendor_wpa_id);
								DBG_WIFI("WPA supported"EOL);

								/* Element 1: Multicast cipher suite (OUI) */
								if(memcmp(current, ieee80211_wpa_oui, IEEE80211_OUI_LEN-1) != 0) {
									scan_result.encryption = IEEE80211_CRYPT_UNSUPPORTED;
									break;
								}
								current += IEEE80211_OUI_LEN;

								/* Element 2: Number of unicast cipher suites, 2 bytes */
								count = (current[0] << 0)|(current[1] << 8);
								current += 2;

								/* Element 3: Unicast cipher suites (OUIs) */
								found = 0;
								for(i=0;i<count;i++) {
                  if(memcmp(current, ieee80211_wpa_oui,  IEEE80211_OUI_LEN-1) == 0)
                  {
                    if(*(current+IEEE80211_OUI_LEN-1) == IEEE80211_CIPHER_TKIP ||
                        *(current+IEEE80211_OUI_LEN-1) == IEEE80211_CIPHER_CCMP)
                      found = 1;
                  }
                  current += IEEE80211_OUI_LEN;
								}
								if(!found) {
									scan_result.encryption = IEEE80211_CRYPT_UNSUPPORTED;
									break;
								}

								/* Element 4: Number of auth key management suites, 2 bytes */
								count = (current[0] << 0)|(current[1] << 8);
								current += 2;

								/* Element 5: Auth key management suites (OUIs) */
								found = 0;
								for(i=0;i<count;i++) {
                  if(memcmp(current, ieee80211_wpa_oui,  IEEE80211_OUI_LEN-1) == 0)
                  {
                    if(*(current+IEEE80211_OUI_LEN-1) == IEEE80211_CIPHER_TKIP ||
                        *(current+IEEE80211_OUI_LEN-1) == IEEE80211_CIPHER_CCMP)
                      found = 1;
                  }
                  current += IEEE80211_OUI_LEN;
								}
								if(!found) {
									scan_result.encryption = IEEE80211_CRYPT_UNSUPPORTED;
									DBG_WIFI("Unsupported encryption"EOL);
									break;
								}

								scan_result.encryption = IEEE80211_CRYPT_WPA;
								break;
							}
							default:
								break;
						}
						frame_current += (frame_current[1] + 2);
					}
					if(scan_result.rateset == 0) scan_result.rateset = IEEE80211_RATEMASK_1;

					ieee80211_scallback(&scan_result, ieee80211_scallback_userparam);
				}
				if(ieee80211_state == IEEE80211_S_RUN) {
					/* (2) */
					if((memcmp(fr->i_addr2, ieee80211_assoc_mac, IEEE80211_ADDR_LEN) == 0)
						&& (memcmp(fr->i_addr3, ieee80211_assoc_bssid, IEEE80211_ADDR_LEN) == 0))
								ieee80211_timeout = IEEE80211_RUN_TIMEOUT;
				}
			} /* ieee80211_mode == IEEE80211_M_MANAGED */
			break;
		case IEEE80211_FC0_SUBTYPE_AUTH:
			if(length < (sizeof(struct ieee80211_frame)+6)) return;
      DBG_WIFI("Auth"EOL);
      //dump(frame_current,length);
			if(ieee80211_mode == IEEE80211_M_MANAGED) {
				/*
				Managed mode.
				If we are authenticating, check for possible AP reply.
				*/
				if(ieee80211_state == IEEE80211_S_AUTH) {
					if(memcmp(fr->i_addr1, rt2501_mac, IEEE80211_ADDR_LEN) != 0) break;
					if(memcmp(fr->i_addr2, ieee80211_assoc_mac, IEEE80211_ADDR_LEN) != 0) break;
					if(memcmp(fr->i_addr3, ieee80211_assoc_bssid, IEEE80211_ADDR_LEN) != 0) break;
					if(ieee80211_authmode == IEEE80211_AUTH_OPEN) {
						/* Open authentication */
						if(((frame_current[0] << 0)|(frame_current[1] << 8)) != IEEE80211_AUTH_ALG_OPEN) {
							DBG_WIFI("Open auth failed: unexpected algo reply"EOL);
							ieee80211_state = IEEE80211_S_IDLE;
							break;
						}
						if(((frame_current[2] << 0)|(frame_current[3] << 8)) != IEEE80211_AUTH_OPEN_RESPONSE)  {
							DBG_WIFI("Open auth failed: unexpected sequence reply"EOL);
							ieee80211_state = IEEE80211_S_IDLE;
							break;
						}
						if(((frame_current[4] << 0)|(frame_current[5] << 8)) != IEEE80211_STATUS_SUCCESS) {
							DBG_WIFI("Open auth failed: denied by AP"EOL);
							ieee80211_state = IEEE80211_S_IDLE;
							break;
						}
						ieee80211_associate();
					} else {
						/* Shared key authentication */
						/* Algo, Auth SEQ, Status code, remaining of the frame */
						ieee80211_input_shared_auth((frame_current[0] << 0)|(frame_current[1] << 8),
								(frame_current[2] << 0)|(frame_current[3] << 8),
								(frame_current[4] << 0)|(frame_current[5] << 8),
								&frame_current[6], length-sizeof(struct ieee80211_frame)-6);
					}
				}
			} else {
				/*
				Master mode.
				Check for a station trying to authenticate to us.
				*/
				if(memcmp(fr->i_addr1, rt2501_mac, IEEE80211_ADDR_LEN) != 0) break;
				/* i_addr2 is the STA MAC */
				if(memcmp(fr->i_addr3, ieee80211_assoc_bssid, IEEE80211_ADDR_LEN) != 0) break;
				/* STA MAC, Algorithm, Auth SEQ, Status code */
				ieee80211_auth_sta(fr->i_addr2,
						(frame_current[0] << 0)|(frame_current[1] << 8),
						(frame_current[2] << 0)|(frame_current[3] << 8),
						(frame_current[4] << 0)|(frame_current[5] << 8));
			}
			break;
		case IEEE80211_FC0_SUBTYPE_ASSOC_REQ:
      DBG_WIFI("Assoc_Req"EOL);
			/*
			We're only insterested in them when in Master mode,
			to take care of authenticated stations trying to associate
			to us.
			*/
			if(ieee80211_mode == IEEE80211_M_MASTER) {
				int32_t sta_slot;

				if(memcmp(fr->i_addr1, rt2501_mac, IEEE80211_ADDR_LEN) != 0) break;
				/* i_addr2 is the STA MAC */
				if(memcmp(fr->i_addr3, ieee80211_assoc_bssid, IEEE80211_ADDR_LEN) != 0) break;
				sta_slot = ieee80211_find_sta_slot(fr->i_addr2);
				if(sta_slot != -1) {
					/*
					We have only one SSID, the station should not
					request another. Always accept association.
					*/
					ieee80211_send_assocresp(fr->i_addr2,
							IEEE80211_STATUS_SUCCESS,
							sta_slot+1);
					ieee80211_associated_sta[sta_slot].state = IEEE80211_S_RUN;
					ieee80211_associated_sta[sta_slot].timer = IEEE80211_STA_MAX_IDLE;
				} else {
					DBG_WIFI("Assoc request from a not authenticated station"EOL);
				}
			}
			break;
		case IEEE80211_FC0_SUBTYPE_ASSOC_RESP:
      DBG_WIFI("Assoc_Resp"EOL);
			/*
			Association Responses are only interesting after we have
			sent an Association Request, in Managed mode only.
			*/
			if((ieee80211_mode == IEEE80211_M_MANAGED) && (ieee80211_state == IEEE80211_S_ASSOC)) {
				uint32_t assoc_code;

				if(memcmp(fr->i_addr1, rt2501_mac, IEEE80211_ADDR_LEN) != 0) break;
				if(memcmp(fr->i_addr2, ieee80211_assoc_mac, IEEE80211_ADDR_LEN) != 0) break;
				if(memcmp(fr->i_addr3, ieee80211_assoc_bssid, IEEE80211_ADDR_LEN) != 0) break;

				assoc_code = ((frame_current[2] << 0)|(frame_current[3] << 8));
				if(assoc_code != IEEE80211_STATUS_SUCCESS)  {
#ifdef DEBUG_WIFI
					sprintf(dbg_buffer, "Assoc failed by AP (0x%04lx)"EOL, assoc_code);
					DBG_WIFI(dbg_buffer);
#endif
					ieee80211_state = IEEE80211_S_IDLE;
					break;
				}
#ifdef DEBUG_WIFI
				sprintf(dbg_buffer, "Association successful with \"%s\""EOL, ieee80211_assoc_ssid);
				DBG_WIFI(dbg_buffer);
#endif
				ieee80211_rssi_sample_index = 0;
				ieee80211_rssi_average = -100;
				/* ieee80211_txrate was filled at auth */
				if((ieee80211_encryption&0xF0) == IEEE80211_CRYPT_WPA ||
           (ieee80211_encryption&0xF0) == IEEE80211_CRYPT_WPA2) {
					ieee80211_state = IEEE80211_S_EAPOL;
					ieee80211_timeout = IEEE80211_EAPOL_TIMEOUT;
				} else {
					ieee80211_state = IEEE80211_S_RUN;
					ieee80211_timeout = IEEE80211_RUN_TIMEOUT;
				}
			}
			break;
		case IEEE80211_FC0_SUBTYPE_DISASSOC:
      DBG_WIFI("Disassoc"EOL);
			// fall through
		case IEEE80211_FC0_SUBTYPE_DEAUTH:
      DBG_WIFI("Deauth"EOL);
			if(ieee80211_mode == IEEE80211_M_MANAGED) {
				/* Managed mode */
				if(ieee80211_state != IEEE80211_S_IDLE) {
					if(memcmp(fr->i_addr1, rt2501_mac, IEEE80211_ADDR_LEN) != 0) break;
					if(memcmp(fr->i_addr2, ieee80211_assoc_mac, IEEE80211_ADDR_LEN) != 0) break;
					if(memcmp(fr->i_addr3, ieee80211_assoc_bssid, IEEE80211_ADDR_LEN) != 0) break;

#ifdef DEBUG_WIFI
					sprintf(dbg_buffer, "Lost connection (0x%04x) !"EOL,
						(frame_current[0] << 0)|(frame_current[1] << 8));
					DBG_WIFI(dbg_buffer);
#endif
					ieee80211_state = IEEE80211_S_IDLE;
				}
			} else {
				/* Master mode */
				int32_t sta_slot;

				if(memcmp(fr->i_addr1, rt2501_mac, IEEE80211_ADDR_LEN) != 0) break;
				if(memcmp(fr->i_addr3, ieee80211_assoc_bssid, IEEE80211_ADDR_LEN) != 0) break;
				sta_slot = ieee80211_find_sta_slot(fr->i_addr2);
				if(sta_slot == -1) break;
				if((fr->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) == IEEE80211_FC0_SUBTYPE_DISASSOC) {
					ieee80211_associated_sta[sta_slot].state = IEEE80211_S_AUTH;
					ieee80211_associated_sta[sta_slot].timer = IEEE80211_STA_ASSOC_TIMEOUT;
				} else ieee80211_associated_sta[sta_slot].state = IEEE80211_S_IDLE;
			}
			break;
		default:
      DBG_WIFI("Unknown"EOL);
			break;
	}
}

static void ieee80211_input_ctl(uint8_t *frame, uint32_t length)
{
	DBG_WIFI("Received control frame"EOL);
  (void)frame;
  (void)length;
	/* handled by the RT2501 ASIC */
}

static void ieee80211_input_data(uint8_t *frame, uint32_t length, int16_t rssi)
{
//#pragma pack(1)
	struct {
	struct ieee80211_frame header;
	uint8_t data[];
	} *fr;
//#pragma pack()
	uint8_t *source_mac, *dest_mac;

/*        DBG_WIFI("ieee80211_input_data"EOL); */

	if((ieee80211_state != IEEE80211_S_EAPOL)
	   && (ieee80211_state != IEEE80211_S_RUN)) return;
	if(length < sizeof(struct ieee80211_frame)) return;
	fr = (void *)frame;

	/* In Managed mode, we are only interested in frames From DS */
	if(((fr->header.i_fc[1] & IEEE80211_FC1_DIR_MASK) == IEEE80211_FC1_DIR_FROMDS)
		    && (ieee80211_mode == IEEE80211_M_MANAGED)) {
		/* Destination address */
		if((memcmp(fr->header.i_addr1, rt2501_mac, IEEE80211_ADDR_LEN) != 0)
				  && (memcmp(fr->header.i_addr1, ieee80211_broadcast_address, IEEE80211_ADDR_LEN) != 0)
				  && (memcmp(fr->header.i_addr1, ieee80211_multicast_address, IEEE80211_ADDR_LEN/2) != 0)
	          ) return;
		dest_mac = fr->header.i_addr1;
		/* BSSID */
		if(memcmp(fr->header.i_addr2, ieee80211_assoc_bssid, IEEE80211_ADDR_LEN) != 0) return;
		/* Source address (i_addr3) can be anything */
		source_mac = fr->header.i_addr3;
		ieee80211_new_rssi_sample(rssi);
	} else {
		/* In Master mode, we are only interested in frames To DS */
		if(((fr->header.i_fc[1] & IEEE80211_FC1_DIR_MASK) == IEEE80211_FC1_DIR_TODS)
			&& (ieee80211_mode == IEEE80211_M_MASTER)) {
			int32_t sta_slot;

			/* BSSID */
			if(memcmp(fr->header.i_addr1, ieee80211_assoc_bssid, IEEE80211_ADDR_LEN) != 0) return;

			/*
			Source address:
			Check the station is associated, and reset its timer
			*/
			sta_slot = ieee80211_find_sta_slot(fr->header.i_addr2);

			if((sta_slot == -1) || (ieee80211_associated_sta[sta_slot].state != IEEE80211_S_RUN)) {
				DBG_WIFI("Data frame from not associated station, dropped"EOL);
				return;
			}
			ieee80211_associated_sta[sta_slot].timer = IEEE80211_STA_MAX_IDLE;
			source_mac = fr->header.i_addr2;

			/*
			Destination address must be the device's MAC or
			broadcast or multicast, as we don't route frames between
			associated stations in this driver.
			*/
			if((memcmp(fr->header.i_addr3, rt2501_mac, IEEE80211_ADDR_LEN) != 0)
				 && (memcmp(fr->header.i_addr3, ieee80211_broadcast_address, IEEE80211_ADDR_LEN) != 0)
				 && (memcmp(fr->header.i_addr3, ieee80211_multicast_address, IEEE80211_ADDR_LEN/2) != 0)
			  ) return;
			dest_mac = fr->header.i_addr3;
		} else return; /* Drop other frames */
	}

	switch(fr->header.i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK) {
	case IEEE80211_FC0_SUBTYPE_DATA:
		if(!rt2501buffer_new(fr->data,
			length-sizeof(struct ieee80211_frame),
			source_mac, dest_mac))
    {
				DBG_WIFI("Unable to queue up received data frame"EOL);
    }
		break;
	case IEEE80211_FC0_SUBTYPE_NODATA:
		if((ieee80211_mode == IEEE80211_M_MANAGED)
			&& (memcmp(source_mac, ieee80211_assoc_mac, IEEE80211_ADDR_LEN) == 0)) {
			DBG_WIFI("Ping from AP, replying"EOL);
			rt2501_send(NULL, 0, ieee80211_assoc_mac, 1, 0);
		}
		break;
	default:
      DBG_WIFI("Unhandled frame in ieee80211_input_data"EOL);
	    break;
	}
}

void ieee80211_input(uint8_t *frame, uint32_t length, int16_t rssi)
{
//#ifdef DEBUG_WIFI
//  DBG_WIFI("Rxed:"EOL);
//  dump(frame,length);
//#endif
	struct ieee80211_frame_min *frame_min;

	if(ieee80211_state == IEEE80211_S_IDLE) return;

	if(length < sizeof(struct ieee80211_frame_min)) return;
	frame_min = (struct ieee80211_frame_min *)frame;

	switch(frame_min->i_fc[0] & IEEE80211_FC0_TYPE_MASK) {
		case IEEE80211_FC0_TYPE_MGT:
			ieee80211_input_mgt(frame, length, rssi);
			break;
		case IEEE80211_FC0_TYPE_CTL:
			ieee80211_input_ctl(frame, length);
			break;
		case IEEE80211_FC0_TYPE_DATA:
			if(length < sizeof(struct ieee80211_frame)) return;
			ieee80211_input_data(frame, length, rssi);
			break;
    default:
      DBG_WIFI("Unhandled frame in ie80211_input"EOL);
      break;
	}
}

void ieee80211_init(void)
{
	rt2501_setmode(IEEE80211_M_MANAGED, NULL, 0);
}

static void ieee80211_start_beacon()
{
//#pragma pack(1)
	struct {
		TXD_STRUC txd;
		struct ieee80211_frame header;
		uint8_t beacon[4 				/* Fixed Parameters */
			+2+IEEE80211_SSID_MAXLEN 	/* SSID */
			+2+8				/* Rates */
			+2+4				/* Extended rates */
			+2+1				/* Channel */
			+3];				/* Possible alignment */
	} beacon;
//#pragma pack()
	uint8_t *write_ptr;
	uint32_t i, j;
	uint32_t frame_length;

	DBG_WIFI("Starting beacon emission"EOL);

	beacon.header.i_fc[0] = IEEE80211_FC0_TYPE_MGT|IEEE80211_FC0_SUBTYPE_BEACON
			|(IEEE80211_FC0_VERSION_0 << IEEE80211_FC0_VERSION_SHIFT);
	beacon.header.i_fc[1] = IEEE80211_FC1_DIR_NODS;
	beacon.header.i_dur[0] = 0;
	beacon.header.i_dur[1] = 0;
	memcpy(beacon.header.i_addr1, ieee80211_broadcast_address, IEEE80211_ADDR_LEN);
	memcpy(beacon.header.i_addr2, rt2501_mac, IEEE80211_ADDR_LEN);
	memcpy(beacon.header.i_addr3, ieee80211_assoc_bssid, IEEE80211_ADDR_LEN);
	beacon.header.i_seq[0] = 0;
	beacon.header.i_seq[1] = 0;

	write_ptr = beacon.beacon;
	/* FIXED PARAMETERS */
	/* Timestamp, handled by Ralink chip */
	*(write_ptr++) = 0x00;
	*(write_ptr++) = 0x00;
	*(write_ptr++) = 0x00;
	*(write_ptr++) = 0x00;
	*(write_ptr++) = 0x00;
	*(write_ptr++) = 0x00;
	*(write_ptr++) = 0x00;
	*(write_ptr++) = 0x00;
	/* Beacon Interval */
	*(write_ptr++) = 0x64;
	*(write_ptr++) = 0x00;
	/* Capability Information */
	*(write_ptr++) = ((IEEE80211_CAPINFO_ESS & 0x00ff) >> 0);
	*(write_ptr++) = ((IEEE80211_CAPINFO_ESS & 0xff00) >> 8);
	/* TAGGED PARAMETERS */
	/* SSID */
	*(write_ptr++) = IEEE80211_ELEMID_SSID;
	j = strlen((char*)ieee80211_assoc_ssid);
	*(write_ptr++) = j;
	for(i=0;i<j;i++)
		*(write_ptr++) = ieee80211_assoc_ssid[i];
	/* Supported Rates */
	*(write_ptr++) = IEEE80211_ELEMID_RATES;
	*(write_ptr++) = 1;
	*(write_ptr++) = 0x82;
	/* Current Channel */
	*(write_ptr++) = IEEE80211_ELEMID_DSPARMS;
	*(write_ptr++) = 1;
	*(write_ptr++) = ieee80211_assoc_channel;

	frame_length = sizeof(struct ieee80211_frame)+((uint32_t)write_ptr - (uint32_t)beacon.beacon);
	rt2501_make_tx_descriptor(
	&beacon.txd,
	RT2501_CIPHER_NONE,		/* CipherAlg */
	0,				/* KeyTable */
	0,				/* KeyIdx */
	0,				/* Ack */
	0,				/* Fragment */
	1,				/* InsTimestamp */
	1,				/* RetryMode */
	0,				/* Ifs */
	RT2501_RATE_1,			/* Rate */
	frame_length,			/* Length */
	0,				/* QueIdx */
	0				/* PacketId */
	);

	rt2501_beacon(&beacon, frame_length+sizeof(TXD_STRUC));
}

static void ieee80211_stop_beacon(void)
{
	rt2501_beacon(NULL, 0);
}

void rt2501_setmode(int32_t mode, const uint8_t *ssid, uint8_t channel)
{
	int32_t i;
	struct rt2501buffer *b;

	switch(mode) {
		case IEEE80211_M_MANAGED:
			DBG_WIFI("Switching to Managed mode"EOL);

			disable_ohci_irq();
			ieee80211_state = IEEE80211_S_IDLE;
			ieee80211_mode = IEEE80211_M_MANAGED;
			enable_ohci_irq();

			ieee80211_stop_beacon();
			break;
		case IEEE80211_M_MASTER:
			DBG_WIFI("Switching to Master mode"EOL);

			disable_ohci_irq();
			ieee80211_state = IEEE80211_S_RUN;
			ieee80211_mode = IEEE80211_M_MASTER;
			strcpy((char*)ieee80211_assoc_ssid, (char*)ssid);
			memcpy(ieee80211_assoc_bssid, rt2501_mac, IEEE80211_ADDR_LEN);
			ieee80211_assoc_channel = channel;
			for(i=0;i<RT2501_MAX_ASSOCIATED_STA;i++)
				ieee80211_associated_sta[i].state = IEEE80211_S_IDLE;
			ieee80211_encryption = IEEE80211_CRYPT_NONE;
			ieee80211_assoc_rateset = ieee80211_txrate = ieee80211_lowest_txrate = IEEE80211_RATEMASK_1;
			enable_ohci_irq();

			/* Update ASIC BSSID */
			rt2501_set_bssid(ieee80211_assoc_bssid);

			/* AP only supports 1Mbps. Tell the ASIC autoresponder. */
			rt2501_write(rt2501_dev, RT2501_TXRX_CSR5, RT2501_RATEMASK_1);

			rt2501_switch_channel(ieee80211_assoc_channel);

			ieee80211_start_beacon();

			break;
	}
	/* Remove all RX queued data */
	disable_ohci_irq();
	do {
		b = rt2501_receive();
		hcd_free(b);
	} while(b != NULL);
	enable_ohci_irq();
}

void rt2501_scan(const uint8_t *ssid, rt2501_scan_callback callback, void *userparam)
{
//#pragma pack(1)
	struct {
	TXD_STRUC txd;
	struct ieee80211_frame header;
	uint8_t probe[2+IEEE80211_SSID_MAXLEN+2+8+2+4];
	} *probe;
//#pragma pack()
	uint8_t *write_ptr;
	uint32_t frame_length;
	uint8_t channel, i, j;

	if(ieee80211_mode != IEEE80211_M_MANAGED) return;

	DBG_WIFI("Scanning..."EOL);

	/* Set up the state machine */
	ieee80211_scallback = callback;
	ieee80211_scallback_userparam = userparam;
	ieee80211_state = IEEE80211_S_SCAN;

	/* Set no BSSID */
	rt2501_set_bssid(ieee80211_null_address);

	for(channel=1;channel<RT2501_MAX_NUM_OF_CHANNELS+1;channel++) {
#ifdef DEBUG_WIFI
		sprintf(dbg_buffer, "channel %d"EOL, channel);
		DBG_WIFI(dbg_buffer);
#endif
		rt2501_switch_channel(channel);

		disable_ohci_irq();
		probe = hcd_malloc(sizeof(*probe)+7, COMRAM,14);
		enable_ohci_irq();
		if(probe == NULL) {
			DBG_WIFI("hcd_malloc failed in rt2501_scan"EOL);
			return;
		}

		/* Send the probe request */
		probe->header.i_fc[0] = IEEE80211_FC0_TYPE_MGT|IEEE80211_FC0_SUBTYPE_PROBE_REQ
				|(IEEE80211_FC0_VERSION_0 << IEEE80211_FC0_VERSION_SHIFT);
		probe->header.i_fc[1] = IEEE80211_FC1_DIR_NODS;
		probe->header.i_dur[0] = 0;
		probe->header.i_dur[1] = 0;
		memcpy(probe->header.i_addr1, ieee80211_broadcast_address, IEEE80211_ADDR_LEN);
		memcpy(probe->header.i_addr2, rt2501_mac, IEEE80211_ADDR_LEN);
		memcpy(probe->header.i_addr3, ieee80211_broadcast_address, IEEE80211_ADDR_LEN);
		probe->header.i_seq[0] = 0;
		probe->header.i_seq[1] = 0;

		write_ptr = probe->probe;
		/* TAGGED PARAMETERS */
		if(ssid != NULL) {
			/* SSID */
			*(write_ptr++) = IEEE80211_ELEMID_SSID;
			j = strlen((char*)ssid);
			*(write_ptr++) = j;
			for(i=0;i<j;i++)
				*(write_ptr++) = ssid[i];
		}
		/* Supported Rates */
		*(write_ptr++) = IEEE80211_ELEMID_RATES;
		*(write_ptr++) = 8;
		*(write_ptr++) = 0x02;
		*(write_ptr++) = 0x04;
		*(write_ptr++) = 0x0b;
		*(write_ptr++) = 0x16;
		*(write_ptr++) = 0x24;
		*(write_ptr++) = 0x30;
		*(write_ptr++) = 0x48;
		*(write_ptr++) = 0x6c;
		/* Extended Supported Rates */
		*(write_ptr++) = IEEE80211_ELEMID_XRATES;
		*(write_ptr++) = 4;
		*(write_ptr++) = 0x0c;
		*(write_ptr++) = 0x12;
		*(write_ptr++) = 0x18;
		*(write_ptr++) = 0x60;

		frame_length = sizeof(struct ieee80211_frame)+((uint32_t)write_ptr - (uint32_t)probe->probe);

		rt2501_make_tx_descriptor(
		&probe->txd,
		RT2501_CIPHER_NONE,		/* CipherAlg */
		0,				/* KeyTable */
		0,				/* KeyIdx */
		1,				/* Ack */
		0,				/* Fragment */
		0,				/* InsTimestamp */
		1,				/* RetryMode */
		0,				/* Ifs */
		RT2501_RATE_1,			/* Rate */
		frame_length,			/* Length */
		0,				/* QueIdx */
		0				/* PacketId */
					 );

		if(!rt2501_tx(probe, frame_length+sizeof(TXD_STRUC)))
    {
			DBG_WIFI("Unable to send probe request !"EOL);
    }

    hcd_free(probe);

		DelayMs(350);
	}
	DBG_WIFI(EOL);

	ieee80211_state = IEEE80211_S_IDLE;
}

void rt2501_auth(const uint8_t *ssid, const uint8_t *mac,
		 const uint8_t *bssid, uint8_t channel,
		 uint16_t rateset,
		 uint8_t authmode,
		 uint8_t encryption,
		 const uint8_t *key)
{
//#pragma pack(1)
	struct {
		TXD_STRUC txd;
		struct ieee80211_frame header;
		char auth[6];
	} *auth;
//#pragma pack()
	uint16_t duration;

	if(ieee80211_mode != IEEE80211_M_MANAGED) return;

#ifdef DEBUG_WIFI
	sprintf(dbg_buffer, "Connecting to \"%s\" (%02x:%02x:%02x:%02x:%02x:%02x) on channel %d, encryption: %d"EOL,
		ssid, bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5], channel,encryption);
	DBG_WIFI(dbg_buffer);
#endif

	/* Store network parameters */
	ieee80211_state = IEEE80211_S_IDLE;

	memcpy(ieee80211_assoc_mac, mac, IEEE80211_ADDR_LEN);
	memcpy(ieee80211_assoc_bssid, bssid, IEEE80211_ADDR_LEN);
	strcpy((char*)ieee80211_assoc_ssid, (char*)ssid);

	ieee80211_assoc_channel = channel;

	ieee80211_assoc_rateset = rateset;

	ieee80211_encryption = encryption;
	switch(ieee80211_encryption&0xF0) {
		case IEEE80211_CRYPT_NONE:
			ieee80211_authmode = IEEE80211_AUTH_OPEN;
			rt2501_set_key(0, NULL, NULL, NULL, RT2501_CIPHER_NONE);
			break;
		case IEEE80211_CRYPT_WEP:
      ieee80211_authmode = authmode;
      if(strlen((char*)&key) > 8)
      {
        ieee80211_encryption = IEEE80211_CRYPT_WEP128;
        memcpy(ieee80211_key, key, IEEE80211_WEP128_KEYLEN);
        rt2501_set_key(0, ieee80211_key, NULL, NULL, RT2501_CIPHER_WEP128);
      }
      else
      {
        ieee80211_encryption = IEEE80211_CRYPT_WEP64;
        memcpy(ieee80211_key, key, IEEE80211_WEP64_KEYLEN);
        rt2501_set_key(0, ieee80211_key, NULL, NULL, RT2501_CIPHER_WEP64);
      }
			break;
		case IEEE80211_CRYPT_WPA:
    case IEEE80211_CRYPT_WPA2:
			ieee80211_authmode = IEEE80211_AUTH_OPEN;
			memcpy(ieee80211_key, key, IEEE80211_MAX_KEYLEN);
			rt2501_set_key(0, NULL, NULL, NULL, RT2501_CIPHER_NONE);
			eapol_init();
			break;
		default:
      sprintf(dbg_buffer,"Unknown encryption specified: %d"EOL,ieee80211_encryption);
			DBG_WIFI(dbg_buffer);
			return;
	}

	ieee80211_lowest_txrate = ieee80211_find_closest_rate(IEEE80211_RATEMASK_1);
	ieee80211_txrate = ieee80211_lowest_txrate;
#ifdef DEBUG_WIFI
	sprintf(dbg_buffer, "supported rates 0x%08x, lowest TX rate 0x%08x"EOL,
		ieee80211_assoc_rateset,
		ieee80211_lowest_txrate);
	DBG_WIFI(dbg_buffer);
#endif

	rt2501_set_bssid(ieee80211_assoc_bssid);

	DBG_WIFI("Sending auth request"EOL);

	/* Send the initial AUTH frame */
	do {
		disable_ohci_irq();
		auth = hcd_malloc(sizeof(*auth)+7, COMRAM,15);
		enable_ohci_irq();
	} while(auth == NULL);

	/*
	We dont use ieee80211_send_basic_auth here because we want to support
	other algorithms.
	*/
	auth->header.i_fc[0] = IEEE80211_FC0_TYPE_MGT|IEEE80211_FC0_SUBTYPE_AUTH
			|(IEEE80211_FC0_VERSION_0 << IEEE80211_FC0_VERSION_SHIFT);
	auth->header.i_fc[1] = IEEE80211_FC1_DIR_NODS;
	duration = rt2501_txtime(sizeof(*auth)-sizeof(TXD_STRUC), ieee80211_mask_to_rate(ieee80211_lowest_txrate))+IEEE80211_SIFS;
	auth->header.i_dur[0] = ((duration & 0x00ff) >> 0);
	auth->header.i_dur[1] = ((duration & 0xff00) >> 8);
	memcpy(auth->header.i_addr1, mac, IEEE80211_ADDR_LEN);
	memcpy(auth->header.i_addr2, rt2501_mac, IEEE80211_ADDR_LEN);
	memcpy(auth->header.i_addr3, bssid, IEEE80211_ADDR_LEN);
	auth->header.i_seq[0] = 0;
	auth->header.i_seq[1] = 0;

	if(authmode == IEEE80211_AUTH_OPEN) {
		/* Open authentication: algo[2], seq[2], status[2] */
		auth->auth[0] = ((IEEE80211_AUTH_ALG_OPEN & 0x00ff) >> 0);
		auth->auth[1] = ((IEEE80211_AUTH_ALG_OPEN & 0xff00) >> 8);
		auth->auth[2] = ((IEEE80211_AUTH_OPEN_REQUEST & 0x00ff) >> 0);
		auth->auth[3] = ((IEEE80211_AUTH_OPEN_REQUEST & 0xff00) >> 8);
	} else {
		/* Shared key authentication: algo[2], seq[2], status[2] */
		auth->auth[0] = ((IEEE80211_AUTH_ALG_SHARED & 0x00ff) >> 0);
		auth->auth[1] = ((IEEE80211_AUTH_ALG_SHARED & 0xff00) >> 8);
		auth->auth[2] = ((IEEE80211_AUTH_SHARED_REQUEST & 0x00ff) >> 0);
		auth->auth[3] = ((IEEE80211_AUTH_SHARED_REQUEST & 0xff00) >> 8);
	}
		auth->auth[4] = ((IEEE80211_STATUS_SUCCESS & 0x00ff) >> 0);
		auth->auth[5] = ((IEEE80211_STATUS_SUCCESS & 0xff00) >> 8);

	/* Tell the ASIC autoresponder the rates the AP supports */
	rt2501_write(rt2501_dev, RT2501_TXRX_CSR5,
		     ieee80211_mask_to_rt2501mask(ieee80211_assoc_rateset));

	rt2501_switch_channel(ieee80211_assoc_channel);

	rt2501_make_tx_descriptor(
	&auth->txd,
	RT2501_CIPHER_NONE,			/* CipherAlg */
	0,					/* KeyTable */
	0,					/* KeyIdx */
	1,					/* Ack */
	0,					/* Fragment */
	0,					/* InsTimestamp */
	1,					/* RetryMode */
	0,					/* Ifs */
	/* Rate */
	ieee80211_mask_to_rt2501rate(ieee80211_lowest_txrate),
	sizeof(*auth)-sizeof(TXD_STRUC),	/* Length */
	0,					/* QueIdx */
	0					/* PacketId */
				 );

	disable_ohci_irq();
	if(!rt2501_tx(auth, sizeof(*auth))) {
		DBG_WIFI("TX error in rt2501_auth"EOL);
		enable_ohci_irq();
		return;
	}
	/* Frame sent, update the state machine */
	ieee80211_state = IEEE80211_S_AUTH;
	ieee80211_timeout = IEEE80211_AUTH_TIMEOUT;
	enable_ohci_irq();
}

int32_t rt2501_send(const uint8_t *frame, uint32_t length, const uint8_t *dest_mac,
		int32_t lowrate, int32_t mayblock)
{
//#pragma pack(1)
	struct {
		TXD_STRUC txd;
		struct ieee80211_frame header;
		uint8_t data[];
	} *fr;
//#pragma pack()
	uint8_t encryption;
	uint8_t encryption_overhead=0;
	uint16_t duration;

	if((ieee80211_state != IEEE80211_S_EAPOL)
	   && (ieee80211_state != IEEE80211_S_RUN)) return 0;
	if(frame == NULL) length = 0;

	do {
		disable_ohci_irq();
		fr = hcd_malloc(sizeof(TXD_STRUC)+sizeof(struct ieee80211_frame)+length+7,
				COMRAM,16);
		enable_ohci_irq();
	} while((fr == NULL) && mayblock);
	if(fr == NULL) return 0;

	fr->header.i_fc[0] = IEEE80211_FC0_TYPE_DATA|(IEEE80211_FC0_VERSION_0 << IEEE80211_FC0_VERSION_SHIFT);
	if(length == 0) {
		fr->header.i_fc[0] |= IEEE80211_FC0_SUBTYPE_NODATA;
		encryption = IEEE80211_CRYPT_NONE;
	} else {
		fr->header.i_fc[0] |= IEEE80211_FC0_SUBTYPE_DATA;
		if(((ieee80211_encryption&0xF0) == IEEE80211_CRYPT_WPA ||
        (ieee80211_encryption&0xF0) == IEEE80211_CRYPT_WPA2
       ) &&
       (eapol_state == EAPOL_S_MSG1 || eapol_state == EAPOL_S_MSG3)
      )
      encryption = IEEE80211_CRYPT_NONE;
    else
      encryption = ieee80211_encryption;
	}

	if(ieee80211_mode == IEEE80211_M_MANAGED)
		fr->header.i_fc[1] = IEEE80211_FC1_DIR_TODS;
	else
		fr->header.i_fc[1] = IEEE80211_FC1_DIR_FROMDS;

	if(encryption != IEEE80211_CRYPT_NONE)
		fr->header.i_fc[1] |= IEEE80211_FC1_PROTECTED;

	switch(encryption&0xF0) {
		case IEEE80211_CRYPT_NONE:
			encryption_overhead = 0;
			break;
		case IEEE80211_CRYPT_WEP:
			encryption_overhead = 8; /* IV[4] + ICV [4] */
			break;
		case IEEE80211_CRYPT_WPA:
    case IEEE80211_CRYPT_WPA2:
      {
        switch(encryption&0x0F)
        {
          case IEEE80211_CIPHER_TKIP:
            encryption_overhead = 20; /* (TKIP) IV[8] + MIC[8] + ICV[4] */
            break;
          case IEEE80211_CIPHER_CCMP:
            encryption_overhead = 16; /* (AES/CCMP) header[8] + MIC[8]; TKIP's 20 above does not apply */
            break;
          default:
            break;
        };
        break;
      }
    default:
      break;
	};

	if((ieee80211_mode == IEEE80211_M_MASTER) && (IEEE80211_IS_MULTICAST(dest_mac)))
		duration = 0;
	else
		duration = rt2501_txtime(sizeof(struct ieee80211_frame)+length+encryption_overhead, ieee80211_mask_to_rate(lowrate ? ieee80211_lowest_txrate : ieee80211_txrate))+IEEE80211_SIFS;
	fr->header.i_dur[0] = (duration & 0x00ff) >> 0;
	fr->header.i_dur[1] = (duration & 0xff00) >> 8;

	if(ieee80211_mode == IEEE80211_M_MANAGED) {
		memcpy(fr->header.i_addr1, ieee80211_assoc_bssid, IEEE80211_ADDR_LEN);
		memcpy(fr->header.i_addr2, rt2501_mac, IEEE80211_ADDR_LEN);
		memcpy(fr->header.i_addr3, dest_mac, IEEE80211_ADDR_LEN);
	} else {
		memcpy(fr->header.i_addr1, dest_mac, IEEE80211_ADDR_LEN);
		memcpy(fr->header.i_addr2, ieee80211_assoc_bssid, IEEE80211_ADDR_LEN);
		memcpy(fr->header.i_addr3, rt2501_mac, IEEE80211_ADDR_LEN);
	}

	/* Sequence number is filled in by the ASIC */
	fr->header.i_seq[0] = 0;
	fr->header.i_seq[1] = 0;

	if(length > 0) memcpy(fr->data, frame, length);

	rt2501_make_tx_descriptor(
	&fr->txd,
	/* CipherAlg */
	ieee80211_crypt_to_rt2501cipher(encryption),
	0,					/* KeyTable */
	0,					/* KeyIdx */
	/* Ack */
	(ieee80211_mode == IEEE80211_M_MANAGED) /* AP should ack all our frames */
			|| !(IEEE80211_IS_MULTICAST(dest_mac)), /* When in AP mode, only unicast frames should be acked */
	0,					/* Fragment */
	0,					/* InsTimestamp */
	1,					/* RetryMode */
	0,					/* Ifs */
	/* Rate */
	ieee80211_mask_to_rt2501rate(lowrate ? ieee80211_lowest_txrate : ieee80211_txrate),
	sizeof(struct ieee80211_frame)+length,	/* Length */
	0,					/* QueIdx */
	0					/* PacketId */
	);
	switch(encryption&0xF0)
  {
      case IEEE80211_CRYPT_WEP:
        fr->txd.Iv = rand() & 0x00ffffff;
        fr->txd.IvOffset = sizeof(struct ieee80211_frame);
        break;
      case IEEE80211_CRYPT_WPA:
      case IEEE80211_CRYPT_WPA2:
        switch(encryption & 0x0F)
        {
          case IEEE80211_CIPHER_TKIP:
            {
              struct ieee80211_tkip_iv iv;
              uint32_t i;

              iv.iv16.field.rc0 = ptk_tsc[1];
              iv.iv16.field.rc1 = (iv.iv16.field.rc0 | 0x20) & 0x7f;
              iv.iv16.field.rc2 = ptk_tsc[0];
              iv.iv16.field.control.field.reserved = 0;
              iv.iv16.field.control.field.ext_iv = 1;
              iv.iv16.field.control.field.key_id = 0;
          //		iv.iv32 = *((uint32_t *)&ptk_tsc[2]);
                          iv.iv32 = ptk_tsc[2] | (ptk_tsc[3] << 8) | (ptk_tsc[4] << 16) | (ptk_tsc[5] << 24);

              fr->txd.Iv = iv.iv16.word;
              fr->txd.Eiv = iv.iv32;
              fr->txd.IvOffset = sizeof(struct ieee80211_frame);

              i = 0;
              while(++ptk_tsc[i] == 0) {													\
                i++;
                if(i == EAPOL_TSC_LENGTH) {
                  DBG_WIFI("TSC cycle !!!"EOL);
                  break;
                }
              }
            }
            break;
          case IEEE80211_CIPHER_CCMP:
            {
              struct ieee80211_ccmp_iv iv;
              uint32_t i;

                // PN0,1
              iv.iv16.field.pn0 = ptk_tsc[0];
              iv.iv16.field.pn1 = ptk_tsc[1];
                // Ctrl
              iv.iv16.field.reserved = 0x00;
              iv.iv16.field.control.field.reserved = 0;
              iv.iv16.field.control.field.ext_iv = 1;
              iv.iv16.field.control.field.key_id = 0;
                // PN2,3,4,5
              iv.iv32 = ptk_tsc[2] | (ptk_tsc[3] << 8) | (ptk_tsc[4] << 16) | (ptk_tsc[5] << 24);

              fr->txd.Iv = iv.iv16.word;
              fr->txd.Eiv = iv.iv32;
              fr->txd.IvOffset = sizeof(struct ieee80211_frame);

              i = 0;
              while(++ptk_tsc[i] == 0) {
                i++;
                if(i == EAPOL_TSC_LENGTH) {
                  DBG_WIFI("TSC cycle !!!"EOL);
                  break;
                }
              }
            }
            break;
          default:
            break;
        };
      default:
        break;
  };

#ifdef DEBUG_WIFI
	sprintf(dbg_buffer, "in rt2501_send, encryption=%d, length=%ld, fc0=0x%02x, fc1=0x%02x"EOL,
		encryption, length, fr->header.i_fc[0], fr->header.i_fc[1]);
	DBG_WIFI(dbg_buffer);
#endif

	disable_ohci_irq();
	if(!rt2501_tx(fr, sizeof(TXD_STRUC)+sizeof(struct ieee80211_frame)+length)) {
		DBG_WIFI("TX error in rt2501_send"EOL);
		enable_ohci_irq();
		return 0;
	}
	enable_ohci_irq();
	DBG_WIFI("TX done in rt2501_send"EOL);
	return 1;
}

void ieee80211_timer(void)
{
	disable_ohci_irq();
	if(ieee80211_mode == IEEE80211_M_MANAGED) {
		/* Managed mode */
		if(ieee80211_timeout > 0) ieee80211_timeout--;
		if(ieee80211_timeout == 0)
			ieee80211_state = IEEE80211_S_IDLE;
	} else {
		/* Master mode */
		uint32_t i;

		for(i=0;i<RT2501_MAX_ASSOCIATED_STA;i++) {
			if(ieee80211_associated_sta[i].state != IEEE80211_S_IDLE) {
				if(ieee80211_associated_sta[i].timer > 0)
					ieee80211_associated_sta[i].timer--;
				if(ieee80211_associated_sta[i].timer == 0)
					ieee80211_deauth_sta(i, IEEE80211_REASON_AUTH_EXPIRE);
			}
		}
	}
	enable_ohci_irq();
}

int32_t rt2501_rssi_average(void)
{
  return ieee80211_rssi_average;
}
