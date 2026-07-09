/**
 * @file eapol.h
 * @author Sebastien Bourdeauducq - 2006 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief Ralink RT2501 driver for Violet embedded platforms - EAPOL
 */
#ifndef _EAPOL_H_
#define _EAPOL_H_


#define LLC_LENGTH			8
extern const uint8_t eapol_llc[LLC_LENGTH];


//#pragma pack(1)


#define EAPOL_DTYPE_WPA2KEY       0x02
#define EAPOL_DTYPE_WPAKEY        0xFE

struct eapol_key_info {
 uint8_t key_mic:1;
 uint8_t secure:1;
 uint8_t error:1;
 uint8_t request:1;
 uint8_t ekd:1;
 uint8_t reserved:3;
 uint8_t key_desc_ver:3;
 uint8_t key_type:1;
 uint8_t key_index:2;
 uint8_t install:1;
 uint8_t key_ack:1;
};

#define EAPOL_RPC_LENGTH          8
#define EAPOL_NONCE_LENGTH        32
#define EAPOL_KEYIV_LENGTH        16
#define EAPOL_KEYRSC_LENGTH       8
#define EAPOL_KEYID_LENGTH        8
#define EAPOL_KEYMIC_LENGTH       16
#define EAPOL_RSN_LENGTH          24

struct eapol_key_frame {
 uint8_t descriptor_type;
 struct eapol_key_info key_info;
 uint8_t key_length[2];
 uint8_t replay_counter[EAPOL_RPC_LENGTH];
 uint8_t key_nonce[EAPOL_NONCE_LENGTH];
 uint8_t key_iv[EAPOL_KEYIV_LENGTH];
 uint8_t key_rsc[EAPOL_KEYRSC_LENGTH];
 uint8_t key_id[EAPOL_KEYID_LENGTH];
 uint8_t key_mic[EAPOL_KEYMIC_LENGTH];
 uint8_t key_data_length[2];
 uint8_t key_data[EAPOL_RSN_LENGTH]; // Max RSN size
};

#define EAPOL_VERSION             0x01
#define EAPOL_VERSION2            0x02

#define EAPOL_TYPE_EAP            0x00
#define EAPOL_TYPE_START          0x01
#define EAPOL_TYPE_LOGOFF         0x02
#define EAPOL_TYPE_KEY            0x03
#define EAPOL_TYPE_ASF            0x04

struct eapol_frame {
 uint8_t llc[LLC_LENGTH];

 uint8_t protocol_version;
 uint8_t packet_type;
 uint8_t body_length[2];

 struct eapol_key_frame key_frame;
};

#define EAPOL_MICK_LENGTH         16
#define EAPOL_EK_LENGTH           16
#define EAPOL_TKIP_EK_LENGTH      16
#define EAPOL_TKIP_RXMICK_LENGTH  8
#define EAPOL_TKIP_TXMICK_LENGTH  8

#define EAPOL_AES_EK_LENGTH      16
#define EAPOL_AES_RXMICK_LENGTH  8
#define EAPOL_AES_TXMICK_LENGTH  8

#define EAPOL_PTK_LENGTH          (EAPOL_MICK_LENGTH + \
                                    EAPOL_EK_LENGTH + EAPOL_TKIP_EK_LENGTH + \
                                    EAPOL_TKIP_RXMICK_LENGTH + \
                                    EAPOL_TKIP_TXMICK_LENGTH)


#define EAPOL_MASTER_KEY_LENGTH   32
#define EAPOL_TSC_LENGTH          6

//#pragma pack()

typedef enum {
	EAPOL_S_MSG1,  /* awaiting first message */
	EAPOL_S_MSG3,  /* awaiting third message */
	EAPOL_S_GROUP, /* awaiting group key     */
	EAPOL_S_RUN,
} eapol_state_t;

extern eapol_state_t eapol_state;
extern uint8_t ptk_tsc[];

void eapol_init(void);
void eapol_input(uint8_t *frame, uint32_t length);

#endif
