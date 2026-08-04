/**
 * @file ieee80211_test.c
 * @brief Host-side guard for the mtl 802.11 parser's frame-length handling.
 *
 * The mtl copy of `src/net/ieee80211.c` is the twin of the lua one, and it
 * still carried the three remote defects #293/#294/#295 fixed there (#307).
 * This file is the lua track's `test/host/ieee80211_test.c` retargeted at the
 * mtl source, plus one scenario the lua track cannot have: mtl still parses the
 * WPA1 vendor IE that #124 scavenged out of the lua stack, and that walk has
 * the same unbounded suite counts as the RSN one.
 *
 * Who can reach this: a probe request and a beacon are unauthenticated
 * management frames, so anything in radio range can send one and
 * `rt2501_receive` hands it straight to `ieee80211_input`. AP mode - the
 * `rx-*` probe-request scenarios - is the mode the boot image's setup portal
 * puts the rabbit in to be provisioned, so the exposure is the provisioning
 * path, not a corner. There is no MMU on this part.
 *
 * ieee80211.c compiles host-native as-is (20 externals, all board or driver
 * entry points), so the REAL parser runs here with `hcd_malloc` mapped onto
 * `malloc` and ASan reports each over-read precisely.
 *
 * Scenarios (argv[1] selects one; all run by default):
 *
 *   short       a legal 32-byte SSID must build a probe request cleanly
 *   long        an over-long SSID must not run off the probe allocation
 *   rx-legal    AP mode: a well-formed probe request is answered, and freed
 *   rx-ssid     AP mode: a probe request's SSID IE length comes off the air
 *   rx-walk     the IE walk must not step past the end of the frame
 *   rx-rsn      STA scan: an RSN IE's suite count comes off the air
 *   rx-rsn-adv  STA scan: an accepted RSN IE must not derail the walk after it
 *   rx-rsn-ver  STA scan: the RSN IE's first field is Version, not a count -
 *               and no bail-out in that parse may report an encrypted AP open
 *   assoc-ssid  the stored SSID is copied into a 33-byte global
 *   rx-wpa1     STA scan: the WPA1 vendor IE's suite counts (mtl only)
 *
 * The `short`, `long`, `rx-legal` and `rx-rsn-adv` scenarios also carry the
 * non-vacuous half: they assert the builder and the receive paths really
 * execute here, so an ASan report in the guards is that code and not an early
 * bail-out.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "usb/rt2501usb.h"
#include "net/ieee80211.h"
#include "net/eapol.h"

/* stubs/ml60842.h's fake OHCI window. */
volatile uint32_t nab_usb_regs[4];

/* --- the board + driver layer ieee80211.c expects ------------------------- */
/* hcd_malloc is the one that matters: routed to malloc so the allocation has
 * ASan redzones around it, exactly as COMRAM has neighbours on the device. */

uint8_t rt2501_mac[IEEE80211_ADDR_LEN] = {2, 0, 0, 0, 0, 1};
PDEVINFO rt2501_dev;
eapol_state_t eapol_state;
uint8_t ptk_tsc[EAPOL_TSC_LENGTH];
char dbg_buffer[DBG_BUFFER_LENGTH];   /* mtl's debug.h keeps sprintf real */

static unsigned tx_calls;
static unsigned malloc_calls;
static unsigned free_calls;

/* Live hcd_malloc blocks. rt2501_auth() hcd_mallocs its auth frame and returns
 * without ever freeing it - the leak shape rx-legal describes for probe
 * responses, on a path that still has it - so a scenario that drives the join
 * would fail the whole binary on a LeakSanitizer report that belongs to the
 * vendored driver rather than to the test. hcd_drain() releases whatever the
 * driver still holds, and the malloc/free counters stay exact for rx-legal. */
#define HCD_LIVE_MAX 16
static void *hcd_live[HCD_LIVE_MAX];

void *hcd_malloc(uint32_t size, uint8_t type, uint8_t tag)
{
  void *p = malloc(size);

  (void)type; (void)tag;
  malloc_calls++;
  for (int i = 0; i < HCD_LIVE_MAX; i++)
    if (hcd_live[i] == NULL) { hcd_live[i] = p; break; }
  return p;
}

void hcd_free(void *p)
{
  free_calls++;
  for (int i = 0; i < HCD_LIVE_MAX; i++)
    if (hcd_live[i] == p) { hcd_live[i] = NULL; break; }
  free(p);
}

static void hcd_drain(void)
{
  for (int i = 0; i < HCD_LIVE_MAX; i++)
    if (hcd_live[i] != NULL) { free(hcd_live[i]); hcd_live[i] = NULL; }
}

void DelayMs(uint16_t ms) { (void)ms; }   /* the 350 ms per-channel dwell */

void eapol_init(void) {}
uint8_t rt2501_beacon(void *b, uint32_t len) { (void)b; (void)len; return 1; }
struct rt2501buffer *rt2501_receive(void) { return NULL; }
uint8_t rt2501buffer_new(const uint8_t *data, uint32_t len,
                         const uint8_t *src, const uint8_t *dst)
{ (void)data; (void)len; (void)src; (void)dst; return 1; }
uint8_t rt2501_set_bssid(const uint8_t *b) { (void)b; return 1; }
int32_t rt2501_set_key(uint8_t i, uint8_t *k, uint8_t *tx, uint8_t *rx, uint8_t c)
{ (void)i; (void)k; (void)tx; (void)rx; (void)c; return 1; }
void rt2501_switch_channel(uint8_t ch) { (void)ch; }
int8_t rt2501_tx(void *frame, uint32_t len)
{ (void)frame; (void)len; tx_calls++; return 1; }
uint16_t rt2501_txtime(uint32_t len, uint8_t rate) { (void)len; (void)rate; return 100; }
uint8_t rt2501_write(PDEVINFO dev, uint16_t reg, uint32_t val)
{ (void)dev; (void)reg; (void)val; return 1; }
void rt2501_make_tx_descriptor(PTXD_STRUC txd, uint8_t CipherAlg,
                               uint8_t KeyTable, uint8_t KeyIdx, uint8_t Ack,
                               uint8_t Fragment, uint8_t InsTimestamp,
                               uint8_t RetryMode, uint8_t Ifs, uint32_t Rate,
                               uint32_t Length, uint8_t QueIdx, uint8_t PacketId)
{
  (void)txd; (void)CipherAlg; (void)KeyTable; (void)KeyIdx; (void)Ack;
  (void)Fragment; (void)InsTimestamp; (void)RetryMode; (void)Ifs; (void)Rate;
  (void)Length; (void)QueIdx; (void)PacketId;
}

/* --- assert harness ------------------------------------------------------- */

static int failures;

#define CHECK(cond, msg)                                                      \
  do {                                                                        \
    if (!(cond)) {                                                            \
      printf("  FAIL: %s\n", (msg));                                          \
      failures++;                                                             \
    }                                                                         \
  } while (0)

static int scan_hits;
static struct rt2501_scan_result last_result;

static void on_result(struct rt2501_scan_result *r, void *ud)
{
  (void)ud;
  last_result = *r;
  scan_hits++;
}

/* rt2501_scan only runs in managed mode; put the state machine there first. */
static void as_station(void)
{
  ieee80211_init();
}

static void run_scan(const char *ssid)
{
  tx_calls = malloc_calls = scan_hits = 0;
  rt2501_scan((const uint8_t *)ssid, on_result, NULL);
}

/* --- a legal SSID: the builder works, so `long` is not passing on a stub --- */

static void scen_short(void)
{
  char ssid[IEEE80211_SSID_MAXLEN + 1];

  printf("scenario short: a legal 32-byte SSID builds a probe request\n");

  memset(ssid, 'a', IEEE80211_SSID_MAXLEN);
  ssid[IEEE80211_SSID_MAXLEN] = '\0';

  as_station();
  run_scan(ssid);

  CHECK(malloc_calls == RT2501_MAX_NUM_OF_CHANNELS,
        "a probe frame is allocated for every channel");
  CHECK(tx_calls == RT2501_MAX_NUM_OF_CHANNELS,
        "a probe request is sent on every channel");
}

/* --- an over-long SSID: must not run off the allocation ------------------- */

static void scen_long(void)
{
  /* 18 fixed IE bytes + the SSID must fit probe[2+32+2+8+2+4] plus the 7 slack
   * bytes hcd_malloc is asked for. 64 is comfortably past that, and well short
   * of the 256 where the uint8_t length would wrap back to zero. */
  char ssid[65];

  printf("scenario long: an over-long SSID must not overrun the probe frame\n");

  memset(ssid, 'a', sizeof ssid - 1);
  ssid[sizeof ssid - 1] = '\0';

  as_station();
  run_scan(ssid);

  CHECK(tx_calls == RT2501_MAX_NUM_OF_CHANNELS,
        "an over-long SSID is either refused or clamped, never overrun");
}

/* --- received frames ------------------------------------------------------ */
/* ieee80211_input takes a raw 802.11 frame. These build the two management
 * subtypes that carry information elements, with every field the parser reads
 * set the way a real radio would - only the IE length bytes are hostile. */

#define FC0_MGT_PROBE_REQ  (IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_PROBE_REQ)
#define FC0_MGT_BEACON     (IEEE80211_FC0_TYPE_MGT | IEEE80211_FC0_SUBTYPE_BEACON)

/* Built oversized, then handed to ieee80211_input in an EXACTLY-sized heap
 * copy: on the device a frame arrives in an hcd_malloc'd buffer of its own
 * length, so a parser that walks past frame_end walks into the next allocation.
 * A generous static buffer would hide precisely that. */
static uint8_t rxbuf[2048];

static void rx_input(uint32_t len, int16_t rssi)
{
  uint8_t *exact = malloc(len);
  memcpy(exact, rxbuf, len);
  ieee80211_input(exact, len, rssi);
  free(exact);
}

/* Fill the 802.11 header and return the offset where the IEs begin. */
static uint32_t mgt_header(uint8_t fc0)
{
  struct ieee80211_frame *fr = (struct ieee80211_frame *)rxbuf;
  memset(rxbuf, 0, sizeof rxbuf);
  fr->i_fc[0] = fc0;
  fr->i_fc[1] = IEEE80211_FC1_DIR_NODS;   /* mgt frames must be "No DS" */
  memset(fr->i_addr1, 0xFF, IEEE80211_ADDR_LEN);
  memset(fr->i_addr2, 0x22, IEEE80211_ADDR_LEN);   /* the sender */
  memset(fr->i_addr3, 0x33, IEEE80211_ADDR_LEN);
  return sizeof(struct ieee80211_frame);
}

/* Append one information element with an explicit length byte. `declared` is
 * what goes on the wire; `actual` is how many payload bytes really follow. A
 * real sender sets them equal - the point of these tests is that the parser
 * must not trust `declared`. */
static uint32_t put_ie(uint32_t at, uint8_t id, uint8_t declared, uint32_t actual,
                       uint8_t fill)
{
  rxbuf[at] = id;
  rxbuf[at + 1] = declared;
  memset(rxbuf + at + 2, fill, actual);
  return at + 2 + actual;
}

/* Re-arm the scan branch. rt2501_scan is what installs the result callback (a
 * file-static in ieee80211.c) and it leaves the state at IDLE, so every beacon
 * scenario runs one scan first and then puts the state back. */
static void as_scanning(void)
{
  as_station();
  run_scan(NULL);
  ieee80211_state = IEEE80211_S_SCAN;
  ieee80211_mode = IEEE80211_M_MANAGED;
  scan_hits = 0;
}

/* --- rx-legal: a well-formed probe request is answered -------------------- */

static void scen_rx_legal(void)
{
  uint32_t n;

  printf("scenario rx-legal: a well-formed probe request gets a response\n");

  as_station();
  rt2501_setmode(IEEE80211_M_MASTER, (const uint8_t *)"nabtest", 1);
  tx_calls = malloc_calls = free_calls = 0;

  n = mgt_header(FC0_MGT_PROBE_REQ);
  n = put_ie(n, IEEE80211_ELEMID_SSID, 7, 7, 'n');   /* honest length */
  memcpy(rxbuf + n - 7, "nabtest", 7);
  rx_input(n, -40);

  /* Proves the AP-mode probe-request path really executes here, so an ASan
   * report in rx-ssid is that path and not some earlier bail-out. */
  CHECK(tx_calls == 1, "a matching probe request is answered with a response");

  /* ieee80211_send_probe_response hcd_mallocs its frame and never frees it, on
   * any path - unlike rt2501_scan, which frees each probe after the TX. The
   * pool this comes out of is not large: hcd.c does
   *
   *     hcd_malloc_init(ComRAMAddr, ComRAMSize, 16, COMRAM);   // 0x1000 = 4 KB
   *
   * and the leaked frame is ~115 bytes, rounded to the allocator's 16-byte
   * boundary = 128. So roughly THIRTY-TWO answered probe requests exhaust
   * COMRAM - fewer in practice, since the OHCI descriptors, endpoint state and
   * RX/TX buffers already live there. The parser answers a probe request whose
   * SSID matches OR that carries no SSID IE at all, i.e. every broadcast probe
   * from every scanning device in range, and the boot image's AP mode stays up
   * for as long as it takes a user to find the network and type a password.
   * What runs dry is the USB allocator, so what stops is the radio (#295). */
  CHECK(free_calls == malloc_calls,
        "the probe-response frame is released after it is sent");
}

/* --- rx-ssid: the SSID IE length is attacker-controlled ------------------- */

static void scen_rx_ssid(void)
{
  uint32_t n;

  printf("scenario rx-ssid: a probe request must not overflow the SSID buffer\n");

  as_station();
  rt2501_setmode(IEEE80211_M_MASTER, (const uint8_t *)"nabtest", 1);

  /* The parser copies frame_current[1] bytes into a char ssid[33] on its own
   * stack, with no check. 200 is a legal byte to put in a length field and a
   * legal frame to transmit; nothing about it is malformed at the radio
   * layer (#293). */
  n = mgt_header(FC0_MGT_PROBE_REQ);
  n = put_ie(n, IEEE80211_ELEMID_SSID, 200, 200, 'A');
  rx_input(n, -40);

  CHECK(1 == 1, "reaching here means the copy was bounded");
}

/* --- rx-walk: the IE walk must stay inside the frame ---------------------- */

static void scen_rx_walk(void)
{
  uint32_t n;

  printf("scenario rx-walk: the IE walk must not step past the frame end\n");

  as_station();
  rt2501_setmode(IEEE80211_M_MASTER, (const uint8_t *)"nabtest", 1);

  /* The walk tests `frame_current < frame_end` and then reads BOTH
   * frame_current[0] and frame_current[1]. One trailing byte after the 802.11
   * header satisfies the condition while the length byte it then reads is
   * already past the end of the frame. Nothing exotic: that is what a frame
   * with a single pad byte, or one clipped by a byte, looks like (#293). */
  n = mgt_header(FC0_MGT_PROBE_REQ);
  rxbuf[n] = IEEE80211_ELEMID_RATES;   /* an element id, and then nothing */
  n += 1;
  rx_input(n, -40);

  CHECK(1 == 1, "reaching here means the walk was bounded");
}

/* --- rx-rsn: the RSN suite count is attacker-controlled ------------------- */

static void scen_rx_rsn(void)
{
  uint32_t n, at;

  printf("scenario rx-rsn: a beacon's RSN suite count must be bounded\n");

  /* The scan path, reached on every join - not AP mode. */
  as_scanning();

  n = mgt_header(FC0_MGT_BEACON);
  n += 12;                          /* timestamp + beacon interval + capinfo */
  at = n;
  /* A well-formed RSN IE up to the PAIRWISE suite count, which says 0x4000
   * while the IE carries one suite. The parser reads that count out of the
   * frame and walks 4 bytes per iteration; before #294 nothing was checked
   * against the IE's end, so 0x4000 suites walked 256 KB past it.
   *
   * The hostile value sits on the pairwise count because that is a count. It
   * used to sit on the IE's first two bytes, which the parser called the group
   * suite count and 802.11 calls the Version field (#310) - the walk there is
   * gone now, so a hostile value in it no longer means anything. */
  rxbuf[at] = IEEE80211_ELEMID_RSN;
  rxbuf[at + 1] = 12;
  rxbuf[at + 2] = 0x01; rxbuf[at + 3] = 0x00;         /* RSN version 1 */
  rxbuf[at + 4] = 0x00; rxbuf[at + 5] = 0x0F;
  rxbuf[at + 6] = 0xAC; rxbuf[at + 7] = 0x04;         /* group suite: CCMP */
  rxbuf[at + 8] = 0x00; rxbuf[at + 9] = 0x40;         /* pairwise count: 0x4000 */
  rxbuf[at + 10] = 0x00; rxbuf[at + 11] = 0x0F;
  rxbuf[at + 12] = 0xAC; rxbuf[at + 13] = 0x04;       /* one CCMP suite */
  n = at + 14;
  rx_input(n, -40);

  /* Reaching here is the assertion - ASan reports the over-read otherwise.
   * The result must also have been delivered, so the scenario cannot pass by
   * the parser bailing out before it ever reads the suite counts. */
  CHECK(scan_hits == 1, "the beacon was parsed through to a scan result");
}

/* --- rx-rsn-adv: a valid RSN IE must not derail the walk that follows ----- */

static void scen_rx_rsn_adv(void)
{
  uint32_t n, at;

  printf("scenario rx-rsn-adv: an accepted RSN IE leaves the walk on the next IE\n");

  as_scanning();

  n = mgt_header(FC0_MGT_BEACON);
  n += 12;
  at = n;
  /* A well-formed WPA2-PSK/CCMP RSN IE, exactly as a consumer AP beacons it.
   * (The parser reads the version field as the group-suite count, which is 1
   * for version 1 - accidentally right, and not what this scenario is about.)
   *   [2,3]    01 00          version 1     -> read as group suite count = 1
   *   [4..7]   00 0F AC 04    group: CCMP
   *   [8,9]    01 00          pairwise count
   *   [10..13] 00 0F AC 04    pairwise: CCMP
   *   [14,15]  01 00          AKM count
   *   [16..19] 00 0F AC 02    AKM: PSK
   * On success the case used to do `frame_current += frame_current[1]` on top
   * of the walk's own advance, so it skipped 2*len+2 bytes - and took the
   * second length byte from a position that had already moved. The SSID IE
   * that follows was then parsed from the middle of the RSN IE. */
  rxbuf[at] = IEEE80211_ELEMID_RSN;
  rxbuf[at + 1] = 18;
  rxbuf[at + 2] = 0x01; rxbuf[at + 3] = 0x00;
  rxbuf[at + 4] = 0x00; rxbuf[at + 5] = 0x0F;
  rxbuf[at + 6] = 0xAC; rxbuf[at + 7] = 0x04;
  rxbuf[at + 8] = 0x01; rxbuf[at + 9] = 0x00;
  rxbuf[at + 10] = 0x00; rxbuf[at + 11] = 0x0F;
  rxbuf[at + 12] = 0xAC; rxbuf[at + 13] = 0x04;
  rxbuf[at + 14] = 0x01; rxbuf[at + 15] = 0x00;
  rxbuf[at + 16] = 0x00; rxbuf[at + 17] = 0x0F;
  rxbuf[at + 18] = 0xAC; rxbuf[at + 19] = 0x02;
  n = at + 20;
  n = put_ie(n, IEEE80211_ELEMID_SSID, 7, 7, 0);
  memcpy(rxbuf + n - 7, "wpa2net", 7);
  rx_input(n, -40);

  CHECK(scan_hits == 1, "the beacon was parsed through to a scan result");
  CHECK((last_result.encryption & IEEE80211_CRYPT_WPA2) != 0,
        "the RSN IE was accepted as WPA2");
  CHECK(strcmp((char *)last_result.ssid, "wpa2net") == 0,
        "the IE after the RSN IE is parsed from its own start");
}

/* --- rx-rsn-version: the first two bytes of an RSN IE are the Version ------ */

/* Append an RSN IE declaring `version`, one CCMP group suite, one CCMP
 * pairwise suite, one PSK AKM suite and empty capabilities - a WPA2-PSK AP's
 * beacon, byte for byte, apart from the version value. */
static uint32_t put_rsn(uint32_t at, uint8_t version)
{
  static const uint8_t body[] = {
    0x00, 0x00,                     /* [0..1] version, patched below */
    0x00, 0x0F, 0xAC, 0x04,         /* group cipher suite: CCMP */
    0x01, 0x00,                     /* pairwise suite count */
    0x00, 0x0F, 0xAC, 0x04,         /* pairwise cipher suite: CCMP */
    0x01, 0x00,                     /* AKM suite count */
    0x00, 0x0F, 0xAC, 0x02,         /* AKM suite: PSK */
    0x00, 0x00,                     /* RSN capabilities */
  };

  rxbuf[at] = IEEE80211_ELEMID_RSN;
  rxbuf[at + 1] = (uint8_t)sizeof body;
  memcpy(rxbuf + at + 2, body, sizeof body);
  rxbuf[at + 2] = version;
  return at + 2 + sizeof body;
}

/* The three bits the RSN branch ORs together on a full WPA2-PSK/CCMP match. */
#define RSN_WPA2_CCMP \
  ((uint8_t)((IEEE80211_CIPHER_CCMP << 1) | (IEEE80211_CIPHER_CCMP >> 1) \
             | IEEE80211_CRYPT_WPA2))

/* A beacon with the capinfo PRIVACY bit set, carrying an RSN IE truncated to
 * `keep` payload bytes. Returns the frame length. */
static uint32_t privacy_beacon_rsn(const uint8_t *body, uint8_t keep)
{
  uint32_t n = mgt_header(FC0_MGT_BEACON);

  rxbuf[n + 10] = IEEE80211_CAPINFO_PRIVACY & 0xFF;   /* capinfo, little-endian */
  rxbuf[n + 11] = IEEE80211_CAPINFO_PRIVACY >> 8;
  n += 12;
  rxbuf[n] = IEEE80211_ELEMID_RSN;
  rxbuf[n + 1] = keep;
  memcpy(rxbuf + n + 2, body, keep);
  return n + 2 + keep;
}

static void scen_rx_rsn_version(void)
{
  uint32_t n;
  /* version + group suite + pairwise count + pairwise suite. */
  static const uint8_t body[] = {
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x04,
    0x01, 0x00,
    0x00, 0x0F, 0xAC, 0x04,
  };
  /* 2 = version and nothing else (dies at the group suite); 6 = + group suite
   * (dies at the pairwise count); 12 = + pairwise count and suite (dies at the
   * AKM count). All three are legal byte counts to put in a length field. */
  static const uint8_t trunc_at[] = {2, 6, 12};
  unsigned t;

  printf("scenario rx-rsn-version: the RSN Version field is not a suite count\n");

  /* Control: version 1, the only value that exists in the wild. The parse has
   * always got this one right - by accident, since running a "group suite
   * count" of 1 for one iteration lands on exactly the four bytes that are in
   * fact the group suite. It is here so the version-2 check below cannot pass
   * on a parser that has simply stopped finding CCMP anywhere. */
  as_scanning();
  n = mgt_header(FC0_MGT_BEACON);
  n += 12;
  n = put_rsn(n, 1);
  rx_input(n, -40);
  CHECK(scan_hits == 1, "the version-1 beacon reached a scan result");
  CHECK(last_result.encryption == RSN_WPA2_CCMP,
        "a version-1 RSN IE is read as WPA2/CCMP");

  /* The same IE with the Version field set to 2. Per 802.11 the group cipher
   * suite follows the version and there is no count in front of it, so nothing
   * about this IE's layout has moved - only the value of a field the parser
   * should not be walking on. Reading it as a count walks TWO 4-byte "group
   * suites", which eats the real group suite plus the pairwise count and the
   * first half of the pairwise suite, and every field after it is then read at
   * the wrong offset. */
  as_scanning();
  n = mgt_header(FC0_MGT_BEACON);
  n += 12;
  n = put_rsn(n, 2);
  rx_input(n, -40);
  CHECK(scan_hits == 1, "the version-2 beacon reached a scan result");
  CHECK(last_result.encryption == RSN_WPA2_CCMP,
        "the RSN suites are read at the same offsets whatever the version says");

  /* Every point the RSN parse can run out of IE has to leave the encryption
   * label no weaker than the capinfo PRIVACY bit already made it. The
   * bail-outs walk out carrying a half-built label - group CCMP bit set, no
   * CRYPT_WPA2 - and `encryption & 0xF0` is what rt2501_auth switches on
   * (ieee80211.c:1975), so 0x08 and 0x0A both land in its
   * `case IEEE80211_CRYPT_NONE:` arm: authmode OPEN, rt2501_set_key(NONE).
   * rt2501_assoc's own switch (line 683) then takes its `default:` arm and
   * sends an association request with no RSN IE at all. A privacy-flagged AP
   * would be joined as an open one (#310). */
  for (t = 0; t < sizeof trunc_at / sizeof *trunc_at; t++) {
    as_scanning();
    n = privacy_beacon_rsn(body, trunc_at[t]);
    rx_input(n, -40);

    CHECK(scan_hits == 1, "the truncated-RSN beacon reached a scan result");
    CHECK((last_result.encryption & 0xF0) != IEEE80211_CRYPT_NONE,
          "a truncated RSN IE must not downgrade an encrypted AP to open");
  }
}

/* --- assoc-ssid: the stored SSID is copied into a 33-byte global ---------- */

static void scen_assoc_ssid(void)
{
  /* Long enough to run well past ieee80211_assoc_ssid[IEEE80211_SSID_MAXLEN+1]
   * and short of the 256 where a uint8_t length would wrap. */
  char ssid[65];
  static const uint8_t mac[IEEE80211_ADDR_LEN]   = {0x22, 0, 0, 0, 0, 1};
  static const uint8_t bssid[IEEE80211_ADDR_LEN] = {0x33, 0, 0, 0, 0, 1};
  static const uint8_t key[16] = {0};

  printf("scenario assoc-ssid: the stored SSID must not overrun its global\n");

  memset(ssid, 'a', sizeof ssid - 1);
  ssid[sizeof ssid - 1] = '\0';

  /* AP mode. Both stores are strcpy() into a 33-byte global with the length
   * checked - where it is checked at all - a layer up, in the VM's netAP /
   * netConnect glue. This is the buffer, so this is where the bound cannot be
   * bypassed: the argument #296 used to justify clamping inside rt2501_scan's
   * probe[], which was not applied here. ASan reports the
   * global-buffer-overflow. */
  as_station();
  rt2501_setmode(IEEE80211_M_MASTER, (const uint8_t *)ssid, 1);
  CHECK(strlen((char *)ieee80211_assoc_ssid) == IEEE80211_SSID_MAXLEN,
        "AP mode clamps the stored SSID to 32 bytes");

  /* The join path: rt2501_auth's callers pass a parser-bounded
   * scan_result.ssid today, so this is the same latent copy one layer over. */
  as_station();
  rt2501_auth((const uint8_t *)ssid, mac, bssid, 1, IEEE80211_RATEMASK_1,
              IEEE80211_AUTH_OPEN, IEEE80211_CRYPT_NONE, key);
  CHECK(strlen((char *)ieee80211_assoc_ssid) == IEEE80211_SSID_MAXLEN,
        "the join path clamps the stored SSID to 32 bytes");
  hcd_drain();                      /* rt2501_auth keeps its auth frame */
}

/* --- rx-wpa1: the WPA1 vendor IE's suite counts, likewise (mtl only) ------ */

static void scen_rx_wpa1(void)
{
  uint32_t n, at;

  printf("scenario rx-wpa1: a beacon's WPA1 vendor-IE suite count must be bounded\n");

  as_scanning();

  n = mgt_header(FC0_MGT_BEACON);
  n += 12;
  at = n;
  /* The WPA1 vendor IE the lua track no longer parses (#124), so this defect
   * is mtl's alone. Layout the parser expects:
   *   [0]      0xDD vendor id
   *   [1]      length (< 22 is skipped, so 22 is the smallest that gets in)
   *   [2..7]   00-50-F2-01 01 00   the WPA IE id (OUI + type + version)
   *   [8..11]  multicast cipher suite OUI
   *   [12,13]  unicast suite count   <- off the air, walked unchecked
   *   [14..]   unicast suites, then the auth suite count and its suites
   * A count of 0x4000 with one suite present walks 256 KB past the frame. */
  rxbuf[at] = IEEE80211_ELEMID_VENDOR;
  rxbuf[at + 1] = 22;
  rxbuf[at + 2] = 0x00; rxbuf[at + 3] = 0x50;
  rxbuf[at + 4] = 0xF2; rxbuf[at + 5] = 0x01;
  rxbuf[at + 6] = 0x01; rxbuf[at + 7] = 0x00;        /* WPA IE id, version 1 */
  rxbuf[at + 8] = 0x00; rxbuf[at + 9] = 0x50;
  rxbuf[at + 10] = 0xF2; rxbuf[at + 11] = 0x02;      /* group: TKIP */
  rxbuf[at + 12] = 0x00; rxbuf[at + 13] = 0x40;      /* unicast suite count */
  rxbuf[at + 14] = 0x00; rxbuf[at + 15] = 0x50;
  rxbuf[at + 16] = 0xF2; rxbuf[at + 17] = 0x02;      /* one TKIP suite */
  rxbuf[at + 18] = 0x00; rxbuf[at + 19] = 0x00;      /* auth suite count */
  rxbuf[at + 20] = 0x00; rxbuf[at + 21] = 0x00;
  rxbuf[at + 22] = 0x00; rxbuf[at + 23] = 0x00;
  n = at + 24;
  rx_input(n, -40);

  CHECK(scan_hits == 1, "the beacon was parsed through to a scan result");
}

int main(int argc, char **argv)
{
  const char *only = (argc > 1) ? argv[1] : NULL;
  int matched = 0;

#define RUN(name, fn)                                                         \
  do {                                                                        \
    if (!only || strcmp(only, (name)) == 0) { matched = 1; fn(); }             \
  } while (0)

  RUN("short",      scen_short);
  RUN("long",       scen_long);
  RUN("rx-legal",   scen_rx_legal);
  RUN("rx-ssid",    scen_rx_ssid);
  RUN("rx-walk",    scen_rx_walk);
  RUN("rx-rsn",     scen_rx_rsn);
  RUN("rx-rsn-adv", scen_rx_rsn_adv);
  RUN("rx-rsn-ver", scen_rx_rsn_version);
  RUN("assoc-ssid", scen_assoc_ssid);
  RUN("rx-wpa1",    scen_rx_wpa1);

  /* An unmatched selector must FAIL, not report a pass having run nothing -
   * the vacuous-pass rule applies to the runner as much as to the test. */
  if (!matched) {
    printf("ieee80211_test: no such scenario \"%s\"\n", only);
    return 2;
  }
  if (failures) {
    printf("ieee80211_test: %d check(s) FAILED\n", failures);
    return 1;
  }
  printf("ieee80211_test: all checks passed\n");
  return 0;
}
