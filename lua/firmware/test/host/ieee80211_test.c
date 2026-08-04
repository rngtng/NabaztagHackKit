/**
 * @file ieee80211_test.c
 * @brief Host-side guard for the probe-request builder's SSID bound.
 *
 * `rt2501_scan()` (src/net/ieee80211.c) builds its probe request into a FIXED
 * allocation and copies the caller's SSID in with no length check:
 *
 *     struct { TXD_STRUC txd; struct ieee80211_frame header;
 *              uint8_t probe[2+IEEE80211_SSID_MAXLEN+2+8+2+4]; } *probe;
 *     probe = hcd_malloc(sizeof(*probe)+7, COMRAM, 14);
 *     ...
 *     j = strlen((char*)ssid);        // j is uint8_t
 *     *(write_ptr++) = j;
 *     for (i = 0; i < j; i++) *(write_ptr++) = ssid[i];
 *
 * The tail of `probe[]` is sized exactly for a 32-byte SSID plus the two rate
 * IEs (18 fixed bytes), so anything past ~40 characters runs off the end of the
 * allocation. `j` being a uint8_t is a second edge: strlen is truncated mod 256,
 * so a 256-character SSID silently advertises a zero-length one.
 *
 * That buffer is not ordinary heap - hcd_malloc hands out COMRAM, the window
 * the OHCI controller itself reads descriptors and endpoint state from, and
 * there is no MMU on this part. The overflow is a live USB host controller's
 * bookkeeping.
 *
 * It is reachable from Lua. hal/wifi.c bounds the SSID on exactly one of its
 * three entry points:
 *
 *     wifi_ap()          strlen(ssid) > IEEE80211_SSID_MAXLEN -> -1   (bounded)
 *     wifi_scan()        -> rt2501_scan(ssid, ...)                    (not)
 *     wifi_connect_ex()  -> wifi_scan(ssid) -> rt2501_scan(ssid, ...) (not)
 *
 * and src/main.c's nab_wifi / nab_wifi_scan take the string straight from
 * luaL_checkstring / luaL_optstring with no cap - while nab_config, on the same
 * seam, does enforce the 32/64-byte field caps. So `nab.wifi_scan(("x"):rep(64))`
 * or `nab.wifi(("x"):rep(64), psk)` from any script or REPL line reaches this,
 * which is design principle 5's "new bindings are bounded nab.* calls" not
 * holding. A fix at either layer closes it; the binding is the better place,
 * since the same cap already exists there for nab.config.
 *
 * ieee80211.c compiles host-native as-is - it needs 25 externals, all of them
 * board or driver entry points - so the REAL builder runs here with hcd_malloc
 * mapped onto malloc, and ASan reports the overflow precisely. Same shape as
 * rfid_test.c: firmware source under test, board layer stubbed.
 *
 * Scenarios (argv[1] selects one; all run by default):
 *
 *   short     a legal 32-byte SSID must build cleanly (the non-vacuous half)
 *   long      an over-long SSID must not run off the probe allocation
 *   rx-legal  a well-formed probe request is answered (the non-vacuous half)
 *   rx-ssid   AP mode: a probe request's SSID IE length comes off the air
 *   rx-walk   the IE walk must not step past the end of the frame
 *   rx-rsn    STA scan: an RSN IE's suite count comes off the air
 *   rx-rsn-version  STA scan: the RSN IE's first field is Version, not a count
 *   assoc-ssid      the stored SSID is copied into a 33-byte global
 *
 * The rx-* scenarios are the serious ones, and they are not local: a probe
 * request is unauthenticated management traffic, so anything in radio range can
 * send one, and rt2501_receive hands it straight to ieee80211_input. AP mode is
 * exactly what net.setup.run puts the rabbit in to be provisioned.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ml674061.h"
#include "common.h"
#include "usb/rt2501usb.h"
#include "net/ieee80211.h"

/* The fake peripheral window stubs/ml674061.h maps every register into. */
volatile uint32_t nab_regs[64];
volatile uint32_t nab_usb_regs[4];   /* stubs/ml60842.h's fake OHCI window */

/* --- the board + driver layer ieee80211.c expects ------------------------- */
/* hcd_malloc is the one that matters: routed to malloc so the allocation has
 * ASan redzones around it, exactly as COMRAM has neighbours on the device. */

uint8_t rt2501_mac[IEEE80211_ADDR_LEN] = {2, 0, 0, 0, 0, 1};
PDEVINFO rt2501_dev;
int32_t eapol_state;
uint32_t ptk_tsc;

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
static uint8_t scan_enc;            /* the last result's encryption byte */
static void on_result(struct rt2501_scan_result *r, void *ud)
{
  (void)ud;
  scan_enc = r->encryption;
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

  /* One probe per channel: proof the builder really ran, so an ASan report in
   * the next scenario is the builder's and not some earlier bail-out. */
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
   * and the leaked frame is 115 bytes, rounded to the allocator's 16-byte
   * boundary = 128. So roughly THIRTY-TWO answered probe requests exhaust
   * COMRAM - fewer in practice, since the OHCI descriptors, endpoint state and
   * RX/TX buffers already live there.
   *
   * Thirty-two is nothing. The parser answers a probe request whose SSID
   * matches OR that carries no SSID IE at all (`!ssid_present`, line 1025) -
   * i.e. every broadcast probe from every scanning device in range - and
   * net.setup.run sits in its serve loop for as long as it takes the user to
   * find the network, open the page and type a password. One phone scanning
   * nearby drains the pool before that. What runs dry is the USB allocator, so
   * what stops is the radio, not just the portal.
   *
   * (mtl/firmware's copy of this file has the same leak - a fix belongs in
   * both.) */
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
   * legal frame to transmit; nothing about it is malformed at the radio layer. */
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
   * with a single pad byte, or one clipped by a byte, looks like. */
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

  /* The scan path, reached on every nab.wifi() join - not AP mode.
   * rt2501_scan is run first because it is what installs the result callback
   * (a file-static in ieee80211.c); it leaves the state at IDLE, so the scan
   * state is re-armed afterwards to hand this frame to the scan branch. */
  as_station();
  run_scan(NULL);
  ieee80211_state = IEEE80211_S_SCAN;
  ieee80211_mode = IEEE80211_M_MANAGED;
  scan_hits = 0;

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

/* The three bits the RSN branch ORs together on a full WPA2-PSK match. */
#define RSN_WPA2_CCMP \
  ((uint8_t)((IEEE80211_CIPHER_CCMP << 1) | (IEEE80211_CIPHER_CCMP >> 1) \
             | IEEE80211_CRYPT_WPA2))

static void scen_rx_rsn_version(void)
{
  uint32_t n;

  printf("scenario rx-rsn-version: the RSN Version field is not a suite count\n");

  as_station();
  run_scan(NULL);
  ieee80211_mode = IEEE80211_M_MANAGED;

  /* Control: version 1, the only value that exists in the wild. The parse has
   * always got this one right - by accident, since running a "group suite
   * count" of 1 for one iteration lands on exactly the four bytes that are in
   * fact the group suite. It is here so the version-2 check below cannot pass
   * on a parser that has simply stopped finding CCMP anywhere. */
  ieee80211_state = IEEE80211_S_SCAN;
  scan_hits = 0; scan_enc = 0;
  n = mgt_header(FC0_MGT_BEACON);
  n += 12;
  n = put_rsn(n, 1);
  rx_input(n, -40);
  CHECK(scan_hits == 1, "the version-1 beacon reached a scan result");
  CHECK(scan_enc == RSN_WPA2_CCMP, "a version-1 RSN IE is read as WPA2/CCMP");

  /* The same IE with the Version field set to 2. Per 802.11 the group cipher
   * suite follows the version and there is no count in front of it, so nothing
   * about this IE's layout has moved - only the value of a field the parser
   * should not be walking on. Reading it as a count walks TWO 4-byte "group
   * suites", which eats the real group suite plus the pairwise count and the
   * first half of the pairwise suite, and every field after it is then read at
   * the wrong offset. */
  ieee80211_state = IEEE80211_S_SCAN;
  scan_hits = 0; scan_enc = 0;
  n = mgt_header(FC0_MGT_BEACON);
  n += 12;
  n = put_rsn(n, 2);
  rx_input(n, -40);
  CHECK(scan_hits == 1, "the version-2 beacon reached a scan result");
  CHECK(scan_enc == RSN_WPA2_CCMP,
        "the RSN suites are read at the same offsets whatever the version says");
}

/* --- assoc-ssid: the stored SSID is copied into a 33-byte global ---------- */

static void scen_assoc_ssid(void)
{
  /* Long enough to run well past ieee80211_assoc_ssid[IEEE80211_SSID_MAXLEN+1]
   * and short of the 256 where a uint8_t length would wrap. */
  char ssid[65];
  static const uint8_t mac[IEEE80211_ADDR_LEN]   = {0x22, 0, 0, 0, 0, 1};
  static const uint8_t bssid[IEEE80211_ADDR_LEN] = {0x33, 0, 0, 0, 0, 1};

  printf("scenario assoc-ssid: the stored SSID must not overrun its global\n");

  memset(ssid, 'a', sizeof ssid - 1);
  ssid[sizeof ssid - 1] = '\0';

  /* AP mode. Not reachable with an over-long SSID today - wifi_ap() rejects
   * > 32 first - but the bound is checked at the caller, not at the buffer,
   * which is the argument #296 used to justify clamping inside rt2501_scan
   * and then did not apply here. ASan reports the global-buffer-overflow. */
  as_station();
  rt2501_setmode(IEEE80211_M_MASTER, (const uint8_t *)ssid, 1);
  CHECK(strlen((char *)ieee80211_assoc_ssid) == IEEE80211_SSID_MAXLEN,
        "AP mode clamps the stored SSID to 32 bytes");

  /* The join path: rt2501_auth's callers pass a parser-bounded
   * scan_result.ssid today, so this is the same latent copy one layer over. */
  as_station();
  rt2501_auth((const uint8_t *)ssid, mac, bssid, 1, IEEE80211_RATEMASK_1,
              IEEE80211_AUTH_OPEN, IEEE80211_CRYPT_NONE, NULL);
  CHECK(strlen((char *)ieee80211_assoc_ssid) == IEEE80211_SSID_MAXLEN,
        "the join path clamps the stored SSID to 32 bytes");
  hcd_drain();                      /* rt2501_auth keeps its auth frame */
}

int main(int argc, char **argv)
{
  const char *only = (argc > 1) ? argv[1] : NULL;

  if (!only || strcmp(only, "short") == 0)    scen_short();
  if (!only || strcmp(only, "long") == 0)     scen_long();
  if (!only || strcmp(only, "rx-legal") == 0) scen_rx_legal();
  if (!only || strcmp(only, "rx-ssid") == 0)  scen_rx_ssid();
  if (!only || strcmp(only, "rx-walk") == 0)  scen_rx_walk();
  if (!only || strcmp(only, "rx-rsn") == 0)   scen_rx_rsn();
  if (!only || strcmp(only, "rx-rsn-version") == 0) scen_rx_rsn_version();
  if (!only || strcmp(only, "assoc-ssid") == 0)     scen_assoc_ssid();

  if (failures) {
    printf("ieee80211_test: %d check(s) FAILED\n", failures);
    return 1;
  }
  printf("ieee80211_test: all checks passed\n");
  return 0;
}
