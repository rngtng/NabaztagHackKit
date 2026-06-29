/**
 * @file eapol.c
 * @author Sebastien Bourdeauducq - 2006 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief Ralink RT2501 driver for Violet embedded platforms - EAPOL
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ml674061.h"
#include "common.h"
#include "utils/mem.h"
#include "usb/hcdmem.h"
#include "usb/hcd.h"
#include "utils/debug.h"
#include "hal/led.h"


#include "usb/rt2501usb.h"
#include "net/eapol.h"
#include "net/hash.h"
#include "net/rc4.h"
#include "net/aes128.h"

/**
 * @brief Fill a buffer with random data
 *
 * @param [in] buffer Address of the buffer
 * @param [in] length Length of the buffer
 */
static void randbuffer(uint8_t *buffer, uint32_t length)
{
  uint32_t i;
  for(i=0;i<length;i++)
    buffer[i] = rand() & 0xFF;
}

/**
 * @brief FIXME
 *
 * F(P,  S, c, i) = U1 xor U2 xor ... Uc
 * U1 =  PRF(P, S || Int(i))
 * U2 =  PRF(P, U1)
 * Uc =  PRF(P, Uc-1)
 *
 * @param [in]  password       Buffer of the password
 * @param [in]  passwordlength Length of the password
 * @param [in]  ssid           Buffer of the SSID
 * @param [in]  ssidlength     Length of the SSID
 * @param [in]  iterations     FIXME
 * @param [in]  count          FIXME
 * @param [out] ouput          Output buffer
 *
 */
void F(const uint8_t *password, uint16_t passwordlength,
       uint8_t *ssid, uint16_t ssidlength,
       uint16_t iterations, uint32_t count, uint8_t *output)
{
  uint8_t digest[36], digest1[20], j;
  uint16_t i;
  struct rt2501buffer *r;

  /* U1 = PRF(P, S || int(i)) */
  memcpy(digest, ssid, ssidlength);
  digest[ssidlength] =   (uint8_t)((count>>24) & 0xFF);
  digest[ssidlength+1] = (uint8_t)((count>>16) & 0xFF);
  digest[ssidlength+2] = (uint8_t)((count>>8) & 0xFF);
  digest[ssidlength+3] = (uint8_t)(count & 0xFF);
  hmac_sha1(password, passwordlength, digest, ssidlength+4, digest1);
  /* output = U1 */
  memcpy(output, digest1, 20);
  for(i=1;i<iterations;i++) 
  {
    /* Un = PRF(P, Un-1) */
    hmac_sha1(password, passwordlength, digest1, 20, digest);
    memcpy(digest1, digest, 20);
    /* output = output xor Un */
    for(j=0;j<20;j++)
      output[j] ^= digest[j];
    if (!(i&63))
    {
      j=(i>>6)&3;
      set_led(1,(j==0)?0xff:0);
      set_led(2,(j&1)?0xff:0);
      set_led(3,(j==2)?0xff:0);
      usbhost_events();
      CLR_WDT;
      while((r=rt2501_receive()))
      {
        CLR_WDT;
        disable_ohci_irq();
        hcd_free(r);
        enable_ohci_irq();
      }
    }
  }
}

/**
 * @brief FIXME
 * Output must be 40 bytes long, only the first 32 bytes are useful
 *
 * @param [in]  password    Buffer for the password
 * @param [in]  ssid        Buffer of the SSID
 * @param [in]  ssidlength  Length of the SSID
 * @param [out] pmk         Ouput buffer for PMK
 */
static void password_to_pmk(const uint8_t *password,
                            uint8_t *ssid, uint16_t ssidlength,
                            uint8_t *pmk)
{
  F(password, strlen((char*)password), ssid, ssidlength, 4096, 1, pmk);
  F(password, strlen((char*)password), ssid, ssidlength, 4096, 2, pmk+20);
}

/**
 * @brief Abstraction for password_to_pmk
 *
 * @param [in]  password    Buffer for the password
 * @param [in]  ssid        Buffer of the SSID
 * @param [in]  ssidlength  Length of the SSID
 * @param [out] pmk         Ouput buffer for PMK
 */
void mypassword_to_pmk(const uint8_t *password,
                            uint8_t *ssid, uint16_t ssidlength,
                            uint8_t *pmk)
{
  password_to_pmk(password,ssid,ssidlength,pmk);
}

/**
 * @brief FIXME
 *
 * @param [in]  key         Key buffer
 * @param [in]  key_len     Key length
 * @param [in]  prefix      Prefix buffer
 * @param [in]  prefix_len  Prefix length
 * @param [in]  data        Data buffer
 * @param [in]  data_len    Data length
 * @param [out] output      Output buffer
 * @param [in]  len         Output length
 */
void prf(const uint8_t *key, uint16_t key_len,
         const uint8_t *prefix, uint16_t prefix_len,
         const uint8_t *data, uint16_t data_len,
         uint8_t *output, uint16_t len)
{
  uint16_t i;
  uint8_t *input; /* concatenated input */
  uint16_t currentindex = 0;
  uint16_t total_len;

  disable_ohci_irq();
  input = hcd_malloc(1024, EXTRAM,1); // Free in eapol.c:173, prf()
  enable_ohci_irq();
  if(input == NULL) {
    DBG_WIFI("hcd_malloc failed in prf"EOL);
    return;
  }
  memcpy(input, prefix, prefix_len);
  input[prefix_len] = 0; /* single octet 0 */
  memcpy(&input[prefix_len+1], data, data_len);
  total_len = prefix_len + 1 + data_len;
  input[total_len] = 0; /* single octet count, starts at 0 */
  total_len++;
  for(i=0;i<(len+19)/20;i++) {
    hmac_sha1(key, key_len, input, total_len, &output[currentindex]);
    currentindex += 20;   /* next concatenation location */
    input[total_len-1]++; /* increment octet count */
  }
  disable_ohci_irq();
  hcd_free(input);
  enable_ohci_irq();
}

static const uint8_t ptk_prefix[] = {
  'P', 'a', 'i', 'r', 'w', 'i', 's', 'e', ' ',
  'k', 'e', 'y', ' ',
  'e', 'x', 'p', 'a', 'n', 's', 'i', 'o', 'n' };

/**
 * @brief Compute PTK FIXME
 *
 * @param [in]  pmk     PMK buffer
 * @param [in]  anonce  ANonce buffer FIXME
 * @param [in]  aa      AA buffer FIXME
 * @param [in]  snonce  SNonce buffer FIXME
 * @param [in]  sa      SA buffer FIXME
 * @param [out] ptk     PTK buffer
 * @param [in]  length  Output/PTK length
 */
static void compute_ptk(uint8_t *pmk,
                        uint8_t *anonce, uint8_t *aa,
                        uint8_t *snonce, uint8_t *sa,
                        uint8_t *ptk, uint32_t length)
{
  uint8_t concatenation[128];
  uint32_t position = 0;

  /* Get min */
  if(memcmp(sa, aa, IEEE80211_ADDR_LEN) > 0)
    memcpy(&concatenation[position], aa, IEEE80211_ADDR_LEN);
  else
    memcpy(&concatenation[position], sa, IEEE80211_ADDR_LEN);
  position += IEEE80211_ADDR_LEN;

  /* Get max */
  if(memcmp(sa, aa, IEEE80211_ADDR_LEN) > 0)
    memcpy(&concatenation[position], sa, IEEE80211_ADDR_LEN);
  else
    memcpy(&concatenation[position], aa, IEEE80211_ADDR_LEN);
  position += IEEE80211_ADDR_LEN;

  /* Get min */
  if(memcmp(snonce, anonce, EAPOL_NONCE_LENGTH) > 0)
    memcpy(&concatenation[position], anonce, EAPOL_NONCE_LENGTH);
  else
    memcpy(&concatenation[position], snonce, EAPOL_NONCE_LENGTH);
  position += EAPOL_NONCE_LENGTH;

  /* Get max */
  if(memcmp(snonce, anonce, EAPOL_NONCE_LENGTH) > 0)
    memcpy(&concatenation[position], snonce, EAPOL_NONCE_LENGTH);
  else
    memcpy(&concatenation[position], anonce, EAPOL_NONCE_LENGTH);
  position += EAPOL_NONCE_LENGTH;

  prf(pmk, EAPOL_MASTER_KEY_LENGTH,
      ptk_prefix, sizeof(ptk_prefix),
      concatenation, position,
      ptk, length);
}

const uint8_t eapol_llc[LLC_LENGTH] =
  { 0xaa, 0xaa, 0x03, 0x00, 0x00, 0x00, 0x88, 0x8e };

static const uint8_t wpa_rsn[/*EAPOL_RSN_LENGTH*/] = {
  0xdd,                   // WPA IE
  0x16,                   // Length
  0x00, 0x50, 0xf2, 0x01, // OUI
  0x01, 0x00,             // Version
  0x00, 0x50, 0xf2, 0x00, // Multicast, type offset: 11
  0x01, 0x00,             // Number of Unicast
  0x00, 0x50, 0xf2, 0x00, // Unicast               : 17
  0x01, 0x00,             // Number of authentication methods
  0x00, 0x50, 0xf2, IEEE80211_AUTH_PSK  // Authentication
};

static const uint8_t wpa2_rsn[/*EAPOL_RSN_LENGTH*/] = {
  0x30,                   // RSN IE
  0x14,                   // Length
  0x01, 0x00,             // Version
  0x00, 0x0f, 0xac, 0x00, // group cipher, CCMP type offset: 7
  0x01, 0x00,             // number of pairwise
  0x00, 0x0f, 0xac, 0x00, // unicast CCMP                  : 13
  0x01, 0x00,             // number of authentication method
  0x00, 0x0f, 0xac, IEEE80211_AUTH_PSK, // authentication PSK
  0x00, 0x00,             // RSN capability
};

eapol_state_t eapol_state;
static uint8_t pmk[EAPOL_MASTER_KEY_LENGTH];
static uint8_t replay_active;
static uint8_t replay_counter[EAPOL_RPC_LENGTH];
static uint8_t anonce[EAPOL_NONCE_LENGTH];
static uint8_t snonce[EAPOL_NONCE_LENGTH];
//static uint8_t ptk[EAPOL_PTK_LENGTH+20];
static union
{
  struct
  {
    uint8_t kck[16];  // EAPOL Key Confirmation Key
    uint8_t kek[16];  // EAPOL Key Encryption Key
    uint8_t tk[16];   // Temporal Key
    struct
    {
        uint8_t rx[8];  // Not used in CCMP;
        uint8_t tx[8];  // Not used in CCMP;
    } mick; // MIC Key
  } s;
  uint8_t b[64];
} ptk;  // Pairwise Transient Key

static union
{
  struct
  {
    uint8_t ek[16];
    struct
    {
        uint8_t rx[8];  // Not used in CCMP;
        uint8_t tx[8];  // Not used in CCMP;
    } mick; // MIC Key
  } s;
  uint8_t b[32];
} gtk;  // Group Transient Key

//uint8_t gtk[EAPOL_MICK_LENGTH+EAPOL_EK_LENGTH];
uint8_t ptk_tsc[EAPOL_TSC_LENGTH];

/**
 * @brief Init EAPOL
 */
void eapol_init(void)
{
  eapol_state = EAPOL_S_MSG1;
  replay_active = 0;
  /* Derive PMK */
  sprintf(dbg_buffer, "Computing PMK...AP '%s'"EOL,ieee80211_assoc_ssid);
  DBG_WIFI(dbg_buffer);
  //uint8_t buffer[64];
  //password_to_pmk(psk, ieee80211_assoc_ssid, strlen(ieee80211_assoc_ssid), buffer);
  //memcpy(pmk, buffer, EAPOL_MASTER_KEY_LENGTH);
  memcpy(pmk, ieee80211_key, EAPOL_MASTER_KEY_LENGTH);

#ifdef DEBUG_WIFI
  uint16_t i;
  DBG_WIFI("done, ");
  for(i=0;i<EAPOL_MASTER_KEY_LENGTH;i++) {
    sprintf(dbg_buffer, "0x%02x,", pmk[i]);
    DBG_WIFI(dbg_buffer);
  }
  DBG_WIFI(EOL);
#endif
}

/**
 * @brief Process EAPOL Message 1
 *
 * @param [in]  frame   Frame buffer
 * @param [in]  length  Frame length
 */
static void eapol_input_msg1(uint8_t *frame, uint32_t length)
{
  struct eapol_frame *fr_in = (struct eapol_frame *)frame;
  struct {
    struct eapol_frame llc_eapol;
    uint8_t rsn[sizeof(wpa_rsn)];
  } fr_out;
  uint8_t ptk_buffer[80];

  DBG_WIFI("Received EAPOL message 1/4"EOL);
  dump(frame,length);

  /* EAPOL_S_MSG3 can happen if the AP did not receive our reply */
  if((eapol_state != EAPOL_S_MSG1) && (eapol_state != EAPOL_S_MSG3)) {
    DBG_WIFI("Inappropriate message"EOL);
    return;
  }
  eapol_state = EAPOL_S_MSG1;

  if((ieee80211_encryption&0x0F) == 0x00)
  {
    switch(fr_in->key_frame.key_info.key_desc_ver)
    {
      case 0x01:
        ieee80211_encryption |= IEEE80211_CIPHER_TKIP;
        DBG_WIFI("AP Uses TKIP so... Switch to TKIP"EOL);
        break;
      case 0x02:
        ieee80211_encryption |= IEEE80211_CIPHER_CCMP;
        DBG_WIFI("AP Uses AES so... Switch to CCMP"EOL);
        break;
      default:
        DBG_WIFI("Unknown Key_desc_ver"EOL);
        break;
    }
  }

  uint16_t rsn_size = sizeof(wpa_rsn);
  if((ieee80211_encryption&0xF0) == IEEE80211_CRYPT_WPA2)
    rsn_size = sizeof(wpa2_rsn);

  /* Save ANonce */
  memcpy(anonce, fr_in->key_frame.key_nonce, EAPOL_NONCE_LENGTH);
#ifdef DEBUG_WIFI
  DBG_WIFI("ANonce: ");
  uint16_t i;
  for(i=0;i<EAPOL_NONCE_LENGTH;i++) {
    sprintf(dbg_buffer, "%02x", anonce[i]);
    DBG_WIFI(dbg_buffer);
  }
  DBG_WIFI(EOL);
#endif

  /* Generate SNonce */
  randbuffer(snonce, EAPOL_NONCE_LENGTH);
#ifdef DEBUG_WIFI
  DBG_WIFI("SNonce: ");
  for(i=0;i<EAPOL_NONCE_LENGTH;i++) {
    sprintf(dbg_buffer, "%02x", snonce[i]);
    DBG_WIFI(dbg_buffer);
  }
  DBG_WIFI(EOL);
#endif

  /* Derive PTK */
  compute_ptk(pmk,
              anonce, ieee80211_assoc_mac,
              snonce, rt2501_mac,
              ptk_buffer, EAPOL_PTK_LENGTH);
  memcpy(ptk.b, ptk_buffer, EAPOL_PTK_LENGTH);

#ifdef DEBUG_WIFI
  DBG_WIFI("PTK computed, ");
  for(i=0;i<EAPOL_PTK_LENGTH;i++) {
    sprintf(dbg_buffer, "%02x", ptk.b[i]);
    DBG_WIFI(dbg_buffer);
  }
  DBG_WIFI(EOL);
#endif

  /* Make response frame */
  memcpy(fr_out.llc_eapol.llc, eapol_llc, LLC_LENGTH);
  if(ieee80211_encryption == IEEE80211_CRYPT_WPA2)
    fr_out.llc_eapol.protocol_version = EAPOL_VERSION2;
  else
    fr_out.llc_eapol.protocol_version = EAPOL_VERSION;
  fr_out.llc_eapol.packet_type = EAPOL_TYPE_KEY;
  fr_out.llc_eapol.body_length[0] = ((sizeof(struct eapol_key_frame)+
                                      rsn_size
                                     ) & 0xff00) >> 8;
  fr_out.llc_eapol.body_length[1] = ((sizeof(struct eapol_key_frame)+
                                      rsn_size
                                     ) & 0x00ff);

  fr_out.llc_eapol.key_frame.descriptor_type = EAPOL_DTYPE_WPAKEY;
  if((ieee80211_encryption&0xF0) == IEEE80211_CRYPT_WPA2)
    fr_out.llc_eapol.key_frame.descriptor_type = EAPOL_DTYPE_WPA2KEY;
  fr_out.llc_eapol.key_frame.key_info.reserved = 0;

  fr_out.llc_eapol.key_frame.key_info.key_desc_ver = 1;
  if((ieee80211_encryption&0x0F) == IEEE80211_CIPHER_CCMP)
    fr_out.llc_eapol.key_frame.key_info.key_desc_ver = 2;
  fr_out.llc_eapol.key_frame.key_info.key_type = 1;
  fr_out.llc_eapol.key_frame.key_info.key_index = 0;
  fr_out.llc_eapol.key_frame.key_info.install = 0;
  fr_out.llc_eapol.key_frame.key_info.key_ack = 0;
  fr_out.llc_eapol.key_frame.key_info.key_mic = 1;
  fr_out.llc_eapol.key_frame.key_info.secure = 0;
  fr_out.llc_eapol.key_frame.key_info.error = 0;
  fr_out.llc_eapol.key_frame.key_info.request = 0;
  fr_out.llc_eapol.key_frame.key_info.ekd = 0;
  fr_out.llc_eapol.key_frame.key_length[0] = 0;
  fr_out.llc_eapol.key_frame.key_length[1] = 0;
  memcpy(fr_out.llc_eapol.key_frame.replay_counter,
                                    replay_counter, EAPOL_RPC_LENGTH);
  memcpy(fr_out.llc_eapol.key_frame.key_nonce, snonce, EAPOL_NONCE_LENGTH);
  memset(fr_out.llc_eapol.key_frame.key_iv, 0, EAPOL_KEYIV_LENGTH);
  memset(fr_out.llc_eapol.key_frame.key_rsc, 0, EAPOL_KEYRSC_LENGTH);
  memcpy(fr_out.llc_eapol.key_frame.key_id,
                   fr_in->key_frame.key_id, EAPOL_KEYID_LENGTH);
  fr_out.llc_eapol.key_frame.key_data_length[0] = (rsn_size&0xff00) >> 8;
  fr_out.llc_eapol.key_frame.key_data_length[1] = (rsn_size&0x00ff);
  if((ieee80211_encryption&0xF0) == IEEE80211_CRYPT_WPA2)
  {
    memcpy(fr_out.llc_eapol.key_frame.key_data, wpa2_rsn, rsn_size);
    *(fr_out.llc_eapol.key_frame.key_data+7)  = (ieee80211_encryption&0x0F);
    *(fr_out.llc_eapol.key_frame.key_data+13) = (ieee80211_encryption&0x0F);
  }
  else
  {
    memcpy(fr_out.llc_eapol.key_frame.key_data, wpa_rsn, rsn_size);
    *(fr_out.llc_eapol.key_frame.key_data+11) = (ieee80211_encryption&0x0F);
    *(fr_out.llc_eapol.key_frame.key_data+17) = (ieee80211_encryption&0x0F);
  }

  /* Compute MIC */
  memset(fr_out.llc_eapol.key_frame.key_mic, 0, EAPOL_KEYMIC_LENGTH);
  if((ieee80211_encryption&0x0F) == IEEE80211_CIPHER_CCMP)
  {
    uint8_t kt[20]={0};
    //DBG_WIFI("Before MIC"EOL);
    //dump((uint8_t *)&fr_out+LLC_LENGTH,sizeof(struct eapol_frame)+rsn_size-LLC_LENGTH);
    hmac_sha1(ptk.s.kck, EAPOL_MICK_LENGTH,
              (uint8_t *)&fr_out+LLC_LENGTH,
              sizeof(struct eapol_frame)+ rsn_size-LLC_LENGTH,
              kt);
    //DBG_WIFI("MIC:");
    //dump(kt,EAPOL_MICK_LENGTH);
    memcpy(fr_out.llc_eapol.key_frame.key_mic,kt,EAPOL_MICK_LENGTH);
    //DBG_WIFI("After MIC"EOL);
    //dump((uint8_t *)&fr_out+LLC_LENGTH,sizeof(struct eapol_frame)+rsn_size-LLC_LENGTH);
  }
  else
    hmac_md5(ptk.s.kck, EAPOL_MICK_LENGTH,
              (uint8_t *)&fr_out+LLC_LENGTH, sizeof(struct eapol_frame)+
              rsn_size-LLC_LENGTH, fr_out.llc_eapol.key_frame.key_mic);

  //DBG_WIFI("Response computed 2/4"EOL);

  /* Send the response */

  rt2501_send((uint8_t *)&fr_out, sizeof(fr_out)-(sizeof(wpa_rsn)-rsn_size), ieee80211_assoc_mac, 1, 1);

  DBG_WIFI("Response sent 2/4"EOL);

  /* Install pairwise encryption and MIC keys */
  if((ieee80211_encryption&0x0F) == IEEE80211_CIPHER_CCMP)  // FIXME
  {
    rt2501_set_key(0, ptk.s.tk, ptk.s.mick.tx, ptk.s.mick.rx, RT2501_CIPHER_AES);
  }
  else
  {
    rt2501_set_key(0, ptk.s.tk, ptk.s.mick.tx, ptk.s.mick.rx, RT2501_CIPHER_TKIP);
  }
  memset(ptk_tsc, 0, EAPOL_TSC_LENGTH);

  eapol_state = EAPOL_S_MSG3;
}

/**
 * @brief Process EAPOL Message 3
 *
 * @param [in]  frame   Frame buffer
 * @param [in]  length  Frame length
 */
static void eapol_input_msg3(uint8_t *frame, uint32_t length)
{
  // FIXME Check length before cast ?
  (void)length;
  struct eapol_frame *fr_in = (struct eapol_frame *)frame;
  uint8_t old_mic[EAPOL_KEYMIC_LENGTH];
  struct {
    struct eapol_frame llc_eapol;
  } fr_out;

  DBG_WIFI("Received EAPOL message 3/4"EOL);

  /* EAPOL_S_GROUP can happen if the AP did not receive our reply */
  if((eapol_state != EAPOL_S_MSG3) && (eapol_state != EAPOL_S_GROUP)) {
    DBG_WIFI("Inappropriate message"EOL);
    return;
  }

  if(fr_in->key_frame.key_info.key_type != 1)
    return;

  /* Check ANonce */
  if(memcmp(fr_in->key_frame.key_nonce, anonce, EAPOL_NONCE_LENGTH) != 0)
  {
    DBG_WIFI("ANonce NOK. Drop !"EOL);
    return;
  }

  DBG_WIFI("ANonce OK"EOL);

  /* Check MIC */
  memcpy(old_mic, fr_in->key_frame.key_mic, EAPOL_KEYMIC_LENGTH);
  memset(fr_in->key_frame.key_mic, 0, EAPOL_KEYMIC_LENGTH);
  if((ieee80211_encryption&0x0F) == IEEE80211_CIPHER_CCMP)
  {
    uint8_t kt[20]={0};
    //DBG_WIFI("Before MIC"EOL);
    //dump((uint8_t *)&fr_out+LLC_LENGTH,sizeof(struct eapol_frame)+rsn_size-LLC_LENGTH);
     hmac_sha1(ptk.s.kck, EAPOL_MICK_LENGTH,
     frame+LLC_LENGTH,
     ((fr_in->body_length[0] << 8)|fr_in->body_length[1])+4,
     kt);
    //DBG_WIFI("MIC:");
    //dump(kt,EAPOL_MICK_LENGTH);
    memcpy(fr_in->key_frame.key_mic,kt,EAPOL_KEYMIC_LENGTH);
    //DBG_WIFI("After MIC"EOL);
    //dump((uint8_t *)&fr_out+LLC_LENGTH,sizeof(struct eapol_frame)+rsn_size-LLC_LENGTH);
  }
  else
    hmac_md5(ptk.s.kck, EAPOL_MICK_LENGTH,
     frame+LLC_LENGTH,
     ((fr_in->body_length[0] << 8)|fr_in->body_length[1])+4,
     fr_in->key_frame.key_mic);

  if(memcmp(fr_in->key_frame.key_mic, old_mic, EAPOL_KEYMIC_LENGTH) != 0)
  {
    DBG_WIFI("Old MIC:"EOL);
    dump(old_mic,EAPOL_KEYMIC_LENGTH);
    DBG_WIFI("Old MIC:"EOL);
    dump(fr_in->key_frame.key_mic,EAPOL_KEYMIC_LENGTH);
    DBG_WIFI("MIC NOK. Drop !"EOL);
    return;
  }
  DBG_WIFI("MIC OK"EOL);

  eapol_state = EAPOL_S_MSG3;

  /* Make response frame */
  memcpy(fr_out.llc_eapol.llc, eapol_llc, LLC_LENGTH);
  //if(ieee80211_encryption == IEEE80211_CRYPT_WPA2)
  //  fr_out.llc_eapol.protocol_version = EAPOL_VERSION2;
  //else
    fr_out.llc_eapol.protocol_version = EAPOL_VERSION;
  fr_out.llc_eapol.packet_type = EAPOL_TYPE_KEY;
  fr_out.llc_eapol.body_length[0] = (sizeof(struct eapol_key_frame)&0xff00)>> 8;
  fr_out.llc_eapol.body_length[1] = (sizeof(struct eapol_key_frame)&0x00ff);

  fr_out.llc_eapol.key_frame.descriptor_type = EAPOL_DTYPE_WPAKEY;
  if((ieee80211_encryption&0xF0) == IEEE80211_CRYPT_WPA2)
      fr_out.llc_eapol.key_frame.descriptor_type = EAPOL_DTYPE_WPA2KEY;
  fr_out.llc_eapol.key_frame.key_info.reserved = 0;
  fr_out.llc_eapol.key_frame.key_info.key_desc_ver = 1;
  if((ieee80211_encryption&0x0F) == IEEE80211_CIPHER_CCMP)
    fr_out.llc_eapol.key_frame.key_info.key_desc_ver = 2;

  fr_out.llc_eapol.key_frame.key_info.key_type = 1;
  fr_out.llc_eapol.key_frame.key_info.key_index = 0;
  fr_out.llc_eapol.key_frame.key_info.install = 0;
  fr_out.llc_eapol.key_frame.key_info.key_ack = 0;
  fr_out.llc_eapol.key_frame.key_info.key_mic = 1;
  fr_out.llc_eapol.key_frame.key_info.secure = 0;
  if((ieee80211_encryption&0xF0) == IEEE80211_CRYPT_WPA2) //FIXME
      fr_out.llc_eapol.key_frame.key_info.secure = 1;
  fr_out.llc_eapol.key_frame.key_info.error = 0;
  fr_out.llc_eapol.key_frame.key_info.request = 0;
  fr_out.llc_eapol.key_frame.key_info.ekd = 0;
  fr_out.llc_eapol.key_frame.key_length[0] = 0;
  fr_out.llc_eapol.key_frame.key_length[1] = 0;
  //if((ieee80211_encryption&0xF0) != IEEE80211_CRYPT_WPA2) //FIXME
  //{
  //  fr_out.llc_eapol.key_frame.key_length[0] = fr_in->key_frame.key_length[0];
  //  fr_out.llc_eapol.key_frame.key_length[1] = fr_in->key_frame.key_length[1];
  //}
  memcpy(fr_out.llc_eapol.key_frame.replay_counter,
                                    replay_counter, EAPOL_RPC_LENGTH);
  memset(fr_out.llc_eapol.key_frame.key_nonce, 0, EAPOL_NONCE_LENGTH);
  memset(fr_out.llc_eapol.key_frame.key_iv, 0, EAPOL_KEYIV_LENGTH);
  memset(fr_out.llc_eapol.key_frame.key_rsc, 0, EAPOL_KEYRSC_LENGTH);
  memset(fr_out.llc_eapol.key_frame.key_id, 0, EAPOL_KEYID_LENGTH);
  fr_out.llc_eapol.key_frame.key_data_length[0] = 0;
  fr_out.llc_eapol.key_frame.key_data_length[1] = 0;

  /* Compute MIC */
  memset(fr_out.llc_eapol.key_frame.key_mic, 0, EAPOL_KEYMIC_LENGTH);
  if((ieee80211_encryption&0x0F) == IEEE80211_CIPHER_CCMP)
  {
    uint8_t kt[20]={0};
    //DBG_WIFI("Before MIC"EOL);
    //dump((uint8_t *)&fr_out+LLC_LENGTH,sizeof(struct eapol_frame)-LLC_LENGTH);
    hmac_sha1(ptk.s.kck, EAPOL_MICK_LENGTH,
          (uint8_t *)&fr_out+LLC_LENGTH, sizeof(struct eapol_frame)-LLC_LENGTH,
          kt);
    //DBG_WIFI("MIC:");
    //dump(kt,EAPOL_MICK_LENGTH);
    memcpy(fr_out.llc_eapol.key_frame.key_mic,kt,EAPOL_MICK_LENGTH);
    //DBG_WIFI("After MIC"EOL);
    //dump((uint8_t *)&fr_out+LLC_LENGTH,sizeof(struct eapol_frame)-LLC_LENGTH);
  }
  else
    hmac_md5(ptk.s.kck, EAPOL_MICK_LENGTH,
     (uint8_t *)&fr_out+LLC_LENGTH, sizeof(struct eapol_frame)-LLC_LENGTH,
     fr_out.llc_eapol.key_frame.key_mic);

  DBG_WIFI("Response computed 4/4"EOL);

  /* Send the response */
  rt2501_send((uint8_t *)&fr_out, sizeof(fr_out), ieee80211_assoc_mac, 1, 1);

  //DBG_WIFI("Response sent"EOL);

  eapol_state = EAPOL_S_GROUP;
  ieee80211_state = IEEE80211_S_RUN;
  ieee80211_timeout = IEEE80211_RUN_TIMEOUT;
}

/**
 * @brief Process GTK message
 *
 * @param [in]  frame   Frame buffer
 * @param [in]  length  Frame length
 */
static void eapol_input_group_msg1(uint8_t *frame, uint32_t length)
{
  // FIXME Check length before cast ?
  (void)length;
  uint32_t i;
  struct eapol_frame *fr_in = (struct eapol_frame *)frame;
  uint8_t old_mic[EAPOL_KEYMIC_LENGTH];
  uint8_t key[32];
  struct rc4_context rc4;
  struct aes128_context aes;
  struct eapol_frame fr_out;

  DBG_WIFI("Received GTK message"EOL);

  /* EAPOL_S_RUN can happen if the AP did not receive our reply, */
  /* or in case of GTK renewal */
  if((eapol_state != EAPOL_S_GROUP) && (eapol_state != EAPOL_S_RUN)) {
    DBG_WIFI("Inappropriate message"EOL);
    return;
  }

  /* Check MIC */
  memcpy(old_mic, fr_in->key_frame.key_mic, EAPOL_KEYMIC_LENGTH);
  memset(fr_in->key_frame.key_mic, 0, EAPOL_KEYMIC_LENGTH);
  if((ieee80211_encryption&0x0F) == IEEE80211_CIPHER_CCMP)
  {
    uint8_t kt[20]={0};
    //DBG_WIFI("Before MIC"EOL);
    //dump((uint8_t *)&frame+LLC_LENGTH,((fr_in->body_length[0] << 8)|fr_in->body_length[1])+4);
     hmac_sha1(ptk.s.kck, EAPOL_MICK_LENGTH, frame+LLC_LENGTH,
     ((fr_in->body_length[0] << 8)|fr_in->body_length[1])+4,
     kt);
    //DBG_WIFI("MIC:");
    //dump(kt,EAPOL_MICK_LENGTH);
    memcpy(fr_in->key_frame.key_mic,kt,EAPOL_KEYMIC_LENGTH);
    //DBG_WIFI("After MIC"EOL);
    //dump((uint8_t *)&frame+LLC_LENGTH,((fr_in->body_length[0] << 8)|fr_in->body_length[1])+4);
  }
  else
    hmac_md5(ptk.s.kck, EAPOL_MICK_LENGTH, frame+LLC_LENGTH,
     ((fr_in->body_length[0] << 8)|fr_in->body_length[1])+4,
     fr_in->key_frame.key_mic);

  if(memcmp(fr_in->key_frame.key_mic, old_mic, EAPOL_KEYMIC_LENGTH) != 0)
  {
    DBG_WIFI("Old MIC:"EOL);
    dump(old_mic,EAPOL_KEYMIC_LENGTH);
    DBG_WIFI("Old MIC:"EOL);
    dump(fr_in->key_frame.key_mic,EAPOL_KEYMIC_LENGTH);
    DBG_WIFI("MIC NOK. Drop !"EOL);
    return;
  }
  //DBG_WIFI("MIC OK"EOL);

  /* Make response frame */
  memcpy(fr_out.llc, eapol_llc, LLC_LENGTH);
  fr_out.protocol_version = EAPOL_VERSION;
  fr_out.packet_type = EAPOL_TYPE_KEY;
  fr_out.body_length[0] = (sizeof(struct eapol_key_frame) & 0xff00) >> 8;
  fr_out.body_length[1] = (sizeof(struct eapol_key_frame) & 0x00ff);

  fr_out.key_frame.descriptor_type = EAPOL_DTYPE_WPAKEY;
  if((ieee80211_encryption&0xF0) == IEEE80211_CRYPT_WPA2)
      fr_out.key_frame.descriptor_type = EAPOL_DTYPE_WPA2KEY;
  fr_out.key_frame.key_info.reserved = 0;
  fr_out.key_frame.key_info.key_desc_ver = 1;
  if((ieee80211_encryption&0x0F) == IEEE80211_CIPHER_CCMP)
    fr_out.key_frame.key_info.key_desc_ver = 2;
  fr_out.key_frame.key_info.key_type = 0;
  fr_out.key_frame.key_info.key_index = fr_in->key_frame.key_info.key_index;
  fr_out.key_frame.key_info.install = 0;
  fr_out.key_frame.key_info.key_ack = 0;
  fr_out.key_frame.key_info.key_mic = 1;
  fr_out.key_frame.key_info.secure = 1;
  fr_out.key_frame.key_info.error = 0;
  fr_out.key_frame.key_info.request = 0;
  fr_out.key_frame.key_info.ekd = 0;
  fr_out.key_frame.key_length[0] = 0;
  fr_out.key_frame.key_length[1] = 0;
  //if(ieee80211_encryption != IEEE80211_CRYPT_WPA2) // FIXME
  //{
  //  fr_out.key_frame.key_length[0] = fr_in->key_frame.key_length[0];
  //  fr_out.key_frame.key_length[1] = fr_in->key_frame.key_length[1];
  //}
  memcpy(fr_out.key_frame.replay_counter, replay_counter, EAPOL_RPC_LENGTH);
  memset(fr_out.key_frame.key_nonce, 0, EAPOL_NONCE_LENGTH);
  memset(fr_out.key_frame.key_iv, 0, EAPOL_KEYIV_LENGTH);
  memset(fr_out.key_frame.key_rsc, 0, EAPOL_KEYRSC_LENGTH);
  memset(fr_out.key_frame.key_id, 0, EAPOL_KEYID_LENGTH);
  fr_out.key_frame.key_data_length[0] = 0;
  fr_out.key_frame.key_data_length[1] = 0;

  /* Compute MIC */
  memset(fr_out.key_frame.key_mic, 0, EAPOL_KEYMIC_LENGTH);

  if((ieee80211_encryption&0x0F) == IEEE80211_CIPHER_CCMP)
  {
    uint8_t kt[20]={0};
    //DBG_WIFI("Before MIC"EOL);
    //dump((uint8_t *)&fr_out+LLC_LENGTH,sizeof(struct eapol_frame)-LLC_LENGTH);
    hmac_sha1(ptk.s.kck, EAPOL_MICK_LENGTH,
          (uint8_t *)&fr_out+LLC_LENGTH, sizeof(struct eapol_frame)-LLC_LENGTH,
          kt);
    //DBG_WIFI("MIC:");
    //dump(kt,EAPOL_MICK_LENGTH);
    memcpy(fr_out.key_frame.key_mic,kt,EAPOL_MICK_LENGTH);
    //DBG_WIFI("After MIC"EOL);
    //dump((uint8_t *)&fr_out+LLC_LENGTH,sizeof(struct eapol_frame)-LLC_LENGTH);
  }
  else
    hmac_md5(ptk.s.kck, EAPOL_MICK_LENGTH,
     (uint8_t *)&fr_out+LLC_LENGTH, sizeof(struct eapol_frame)-LLC_LENGTH,
     fr_out.key_frame.key_mic);

  DBG_WIFI("Response computed GTK"EOL);

  /* Send the response */
  rt2501_send((uint8_t *)&fr_out, sizeof(struct eapol_frame),
              ieee80211_assoc_mac, 1, 1);

  //DBG_WIFI("Response sent GTK"EOL);

  /* Decipher and install GTK */
  switch(ieee80211_encryption&0x0F)
  {
    case IEEE80211_CIPHER_TKIP: // Decrypt RC4
      {
        memcpy(&key[0], fr_in->key_frame.key_iv, EAPOL_KEYIV_LENGTH);
        memcpy(&key[EAPOL_KEYIV_LENGTH], ptk.s.kek, 16);
        rc4_init(&rc4, key, 32);
        for(i=0;i<256;i++)
          rc4_byte(&rc4); /* discard first 256 bytes */
        rc4_cipher(&rc4, gtk.b, fr_in->key_frame.key_data,
                                                   EAPOL_MICK_LENGTH+EAPOL_EK_LENGTH);
        #ifdef DEBUG_WIFI
        DBG_WIFI("GTK is ");
        dump(gtk.b,EAPOL_MICK_LENGTH+EAPOL_EK_LENGTH);
        #endif

        // FIXME
        if(fr_in->key_frame.key_info.key_index != 0)
            rt2501_set_key(fr_in->key_frame.key_info.key_index,
                           gtk.s.ek, gtk.s.mick.tx, gtk.s.mick.rx, RT2501_CIPHER_TKIP);
      }
      break;
    case IEEE80211_CIPHER_CCMP: // Decrypt CCMP/AES
      {
        // FIXME Actually do something...
        memcpy(&key[0], ptk.s.kek, 16);
        aes128_init(&aes, key, 16); // 16 or 128 ?
        aes128_decrypt(&aes, gtk.b, fr_in->key_frame.key_data,
                                                   EAPOL_MICK_LENGTH+EAPOL_EK_LENGTH);
        #ifdef DEBUG_WIFI
        DBG_WIFI("GTK is ");
        dump(gtk.b,EAPOL_MICK_LENGTH+EAPOL_EK_LENGTH);
        #endif

        // FIXME
        if(fr_in->key_frame.key_info.key_index != 0)
            rt2501_set_key(fr_in->key_frame.key_info.key_index,
                           gtk.s.ek, gtk.s.mick.tx, gtk.s.mick.rx, RT2501_CIPHER_AES);
      }
      break;
    default:
      break;
  };
  eapol_state = EAPOL_S_RUN;
  ieee80211_state = IEEE80211_S_RUN;
  ieee80211_timeout = IEEE80211_RUN_TIMEOUT;
}

/**
 * @brief Process EAPOL Frame
 *
 * @param [in]  frame   Frame buffer
 * @param [in]  length  Frame length
 */
void eapol_input(uint8_t *frame, uint32_t length)
{
  struct eapol_frame *fr = (struct eapol_frame *)frame;

#ifdef DEBUG_WIFI
  sprintf(dbg_buffer, "Received EAPOL frame, key info 0x%02X 0x%02X (enc: 0x%X)"EOL,
    *(((uint8_t *)&fr->key_frame.key_info)+0),
    *(((uint8_t *)&fr->key_frame.key_info)+1),
    ieee80211_encryption);
    DBG_WIFI(dbg_buffer);
    //dump(frame,length);
#endif

  /*
   * drop EAPOL frames when WPA is disabled
   * other errors
   */

  if( ((ieee80211_encryption&0xF0) != IEEE80211_CRYPT_WPA &&
       (ieee80211_encryption&0xF0) != IEEE80211_CRYPT_WPA2
      )
      || length < sizeof(struct eapol_frame)-EAPOL_RSN_LENGTH
      || (fr->protocol_version != EAPOL_VERSION &&
          fr->protocol_version != EAPOL_VERSION2
         )
      || fr->packet_type != EAPOL_TYPE_KEY 
      || (fr->key_frame.descriptor_type != EAPOL_DTYPE_WPAKEY &&
          fr->key_frame.descriptor_type != EAPOL_DTYPE_WPA2KEY
         ) 
    )
      {
        #ifdef DEBUG_WIFI
          DBG_WIFI("Drop EAPOL frame");
          dump(frame,length);
          sprintf(dbg_buffer, "Checks: %d %d(%d/%d) %d %d %d"EOL
            ,((ieee80211_encryption&0xF0) != IEEE80211_CRYPT_WPA &&
              (ieee80211_encryption&0xF0) != IEEE80211_CRYPT_WPA2
             )
            ,length < sizeof(struct eapol_frame)-EAPOL_RSN_LENGTH
              , length
              , sizeof(struct eapol_frame)-EAPOL_RSN_LENGTH
            ,(fr->protocol_version != EAPOL_VERSION &&
              fr->protocol_version != EAPOL_VERSION2
             )
            , fr->packet_type != EAPOL_TYPE_KEY
            , (fr->key_frame.descriptor_type != EAPOL_DTYPE_WPAKEY &&
               fr->key_frame.descriptor_type != EAPOL_DTYPE_WPA2KEY
              )
          );
          DBG_WIFI(dbg_buffer);
        #endif
        return;
      }



  /* Validate replay counter */
  if(replay_active) {
    if(memcmp(fr->key_frame.replay_counter,
                            replay_counter, EAPOL_RPC_LENGTH) <= 0) {
      DBG_WIFI("Replay counter ERR"EOL);
      return;
    }
  }
  /* Update local replay counter */
  memcpy(replay_counter, fr->key_frame.replay_counter, EAPOL_RPC_LENGTH);
  replay_active = 1;
  DBG_WIFI("Replay counter OK"EOL);

#ifdef DEBUG_WIFI
  sprintf(dbg_buffer, "KeyInfo Key Description Version %d"EOL,
                      fr->key_frame.key_info.key_desc_ver);
  DBG_WIFI(dbg_buffer);
  sprintf(dbg_buffer, "KeyInfo Key Type %d"EOL,
                      fr->key_frame.key_info.key_type);
  DBG_WIFI(dbg_buffer);
  sprintf(dbg_buffer, "KeyInfo Key Index %d"EOL,
                      fr->key_frame.key_info.key_index);
  DBG_WIFI(dbg_buffer);
  sprintf(dbg_buffer, "KeyInfo Install %d"EOL, fr->key_frame.key_info.install);
  DBG_WIFI(dbg_buffer);
  sprintf(dbg_buffer, "KeyInfo Key Ack %d"EOL, fr->key_frame.key_info.key_ack);
  DBG_WIFI(dbg_buffer);
  sprintf(dbg_buffer, "KeyInfo Key MIC %d"EOL, fr->key_frame.key_info.key_mic);
  DBG_WIFI(dbg_buffer);
  sprintf(dbg_buffer, "KeyInfo Secure %d"EOL, fr->key_frame.key_info.secure);
  DBG_WIFI(dbg_buffer);
  sprintf(dbg_buffer, "KeyInfo Error %d"EOL, fr->key_frame.key_info.error);
  DBG_WIFI(dbg_buffer);
  sprintf(dbg_buffer, "KeyInfo Request %d"EOL, fr->key_frame.key_info.request);
  DBG_WIFI(dbg_buffer);
  sprintf(dbg_buffer, "KeyInfo EKD_DL %d"EOL, fr->key_frame.key_info.ekd);
  DBG_WIFI(dbg_buffer);
  uint16_t s = (fr->key_frame.key_data_length[0]<<8) | fr->key_frame.key_data_length[1];
  sprintf(dbg_buffer, "KeyData (%d)"EOL, s);
  DBG_WIFI(dbg_buffer);
  dump(fr->key_frame.key_data,s);
#endif

  if((fr->key_frame.key_info.key_type == 1) &&
       (fr->key_frame.key_info.key_index == 0) &&
       (fr->key_frame.key_info.key_ack == 1) &&
       (fr->key_frame.key_info.key_mic == 0) &&
       (fr->key_frame.key_info.secure == 0) &&
       (fr->key_frame.key_info.error == 0) &&
       (fr->key_frame.key_info.request == 0))
    eapol_input_msg1(frame, length);
  else if((fr->key_frame.key_info.key_type == 1) &&
      (fr->key_frame.key_info.key_index == 0) &&
      (fr->key_frame.key_info.key_ack == 1) &&
      (fr->key_frame.key_info.key_mic == 1) &&
      //(fr->key_frame.key_info.secure == 0) &&
      (fr->key_frame.key_info.error == 0) &&
      (fr->key_frame.key_info.request == 0))
    eapol_input_msg3(frame, length);
  else if((fr->key_frame.key_info.key_type == 0) &&
      //(fr->key_frame.key_info.key_index != 0) &&
      (fr->key_frame.key_info.key_ack == 1) &&
      (fr->key_frame.key_info.key_mic == 1) &&
      (fr->key_frame.key_info.secure == 1) &&
      (fr->key_frame.key_info.error == 0) &&
      (fr->key_frame.key_info.request == 0))
    eapol_input_group_msg1(frame, length);
  else
  {
    DBG_WIFI("Drop EAPOL frame..."EOL);
  }
}
