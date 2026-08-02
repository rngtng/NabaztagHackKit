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
 *   short   a legal 32-byte SSID must build cleanly (the non-vacuous half)
 *   long    an over-long SSID must not run off the probe allocation
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

void *hcd_malloc(uint32_t size, uint8_t type, uint8_t tag)
{
  (void)type; (void)tag;
  malloc_calls++;
  return malloc(size);
}

void hcd_free(void *p) { free(p); }

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

int main(int argc, char **argv)
{
  const char *only = (argc > 1) ? argv[1] : NULL;

  if (!only || strcmp(only, "short") == 0) scen_short();
  if (!only || strcmp(only, "long") == 0)  scen_long();

  if (failures) {
    printf("ieee80211_test: %d check(s) FAILED\n", failures);
    return 1;
  }
  printf("ieee80211_test: all checks passed\n");
  return 0;
}
