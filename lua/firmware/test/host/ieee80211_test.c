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

void *hcd_malloc(uint32_t size, uint8_t type, uint8_t tag)
{
  (void)type; (void)tag;
  malloc_calls++;
  return malloc(size);
}

void hcd_free(void *p) { free_calls++; free(p); }

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
static void on_result(struct rt2501_scan_result *r, void *ud)
{
  (void)r; (void)ud;
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
   * any path - unlike rt2501_scan, which frees each probe after the TX. In AP
   * mode every answered probe request therefore leaks ~115 bytes of COMRAM
   * permanently, and a phone scanning nearby sends probe requests continuously
   * for as long as net.setup.run sits in its serve loop. COMRAM is a small
   * fixed pool shared with the USB transfer descriptors, so exhausting it takes
   * the radio down, not just the portal. (Present in mtl/firmware's copy of
   * this file too - a fix belongs in both.) */
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

  /* The scan path, reached on every nab.wifi() join - not AP mode. */
  as_station();
  ieee80211_state = IEEE80211_S_SCAN;
  ieee80211_mode = IEEE80211_M_MANAGED;

  n = mgt_header(FC0_MGT_BEACON);
  n += 12;                          /* timestamp + beacon interval + capinfo */
  at = n;
  /* An RSN IE whose group-suite count says 0x4000 while the IE carries four
   * bytes. The parser reads the count out of the frame and walks 4 bytes per
   * iteration with nothing checked against frame_end. */
  rxbuf[at] = IEEE80211_ELEMID_RSN;
  rxbuf[at + 1] = 8;
  rxbuf[at + 2] = 0x00; rxbuf[at + 3] = 0x40;         /* group suite count */
  rxbuf[at + 4] = 0x00; rxbuf[at + 5] = 0x0F;
  rxbuf[at + 6] = 0xAC; rxbuf[at + 7] = 0x04;         /* one CCMP suite */
  rxbuf[at + 8] = 0x00; rxbuf[at + 9] = 0x00;
  n = at + 10;
  rx_input(n, -40);

  CHECK(1 == 1, "reaching here means the suite walk was bounded");
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

  if (failures) {
    printf("ieee80211_test: %d check(s) FAILED\n", failures);
    return 1;
  }
  printf("ieee80211_test: all checks passed\n");
  return 0;
}
