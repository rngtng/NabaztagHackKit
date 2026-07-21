/**
 * @file hal/wifi.c
 * @brief RT2501 802.11 join over the vendored USB + 802.11/WPA stack.
 *
 * The join sequence proven on hardware by wifiprobe (#119), extracted so both
 * the probe and the `nab.wifi` binding drive one implementation. See hal/wifi.h
 * for the contract. Nothing here prints - callers observe via wifi_seen() and
 * the wifi_join step callback, from the main loop (the RX/scan callback runs in
 * the IRQ path and must not block on the polled UART).
 */
#include <string.h>

#include "ml674061.h"
#include "common.h"
#include "irq.h"

#include "utils/delay.h"
#include "utils/mem.h"

#include "usb/usbh.h"
#include "usb/usbctrl.h"
#include "usb/hcd.h"
#include "usb/hcdmem.h"
#include "usb/rt2501usb.h"
#include "net/eapol.h"
#include "net/ieee80211.h"

#include "hal/wifi.h"

/* eapol.c's PBKDF2 (4096-iter HMAC-SHA1 x2); no header declares it - V1's
 * vm/vnet.c declared it the same way at its netPmk call site. */
void mypassword_to_pmk(const uint8_t *password, uint8_t *ssid,
                       int32_t ssidlength, uint8_t *pmk);

/* usbhcore.c's init-once guard; cleared for the bring-up retry re-init cycle. */
extern uint8_t usbhost_init_status;

/* ---- scan bookkeeping ---------------------------------------------------- */
/* The scan callback fires from the USB RX IRQ path once per received beacon/
 * probe-response, so it only records (no printing / blocking there). */
#define MAX_SEEN 32
static struct rt2501_scan_result seen[MAX_SEEN];
static volatile int32_t scan_hits;

/* Dedicated capture slot for the target AP: a dense environment sees far more
 * networks than MAX_SEEN (84 observed), so a weak guest AP whose probe-response
 * arrives late would be dropped from seen[] and the join stage never run.
 * Record the requested SSID out-of-band the moment it appears. */
static struct rt2501_scan_result target_seen;
static volatile int32_t target_hit;
static const char *scan_target_ssid;   /* set before each scan; read in IRQ cb */

static int ssid_is_target(const uint8_t *ssid)
{
  const char *w = scan_target_ssid;
  int j;
  if (w == NULL)
    return 0;
  for (j = 0; w[j] && (char)ssid[j] == w[j]; j++)
    ;
  return !w[j] && !ssid[j];
}

static void scan_cb(struct rt2501_scan_result *r, void *userparam)
{
  int i, j;
  (void)userparam;
  if (!target_hit && ssid_is_target(r->ssid)) {
    target_seen = *r;
    target_hit = 1;
  }
  for (i = 0; i < scan_hits && i < MAX_SEEN; i++) {
    for (j = 0; j < 6; j++)
      if (seen[i].bssid[j] != r->bssid[j])
        break;
    if (j == 6)
      return; /* already have this BSSID */
  }
  if (scan_hits < MAX_SEEN)
    seen[scan_hits] = *r;
  scan_hits++;
}

/* ---- pump ---------------------------------------------------------------- */
/* V1 main.c's pump: usb events + rx drain + the driver timer. V1 fires
 * rt2501_timer every 4th ~50 ms outer iteration (~200 ms); the 802.11
 * AUTH/ASSOC timeouts are tick counts, so a faster tick would halve the
 * response window V1 gives the AP. pump_last_timer persists across calls: a
 * caller pumping in 100 ms slices must not reset the 200 ms tick phase every
 * call, or the driver timer never fires at all. */
static uint32_t pump_last_timer;

/* RX capture (#216): drained data frames are freed by default (the join path
 * only needs EAPOL, which rt2501_receive consumes internally). Once capture is
 * on - wifi_ap() / first wifi_recv_frame() - the pump queues up to RX_QUEUE_MAX
 * frames for the app instead, dropping the newest on overflow so a non-polling
 * app can't exhaust the hcd pool. Main-loop only (like every rt2501_receive
 * caller); the queue links through the buffer's `next` field, which is ours
 * once rt2501_receive hands the buffer over. */
#define RX_QUEUE_MAX 8
static struct rt2501buffer *rx_head, *rx_tail;
static int32_t rx_count;
static int8_t rx_capture;

/* One pump iteration: usb events + rx drain (capture or free) + driver timer. */
static void pump_slice(void)
{
  struct rt2501buffer *r;

  usbhost_events();
  while ((r = rt2501_receive()) != NULL) {
    /* EAPOL frames are consumed (and freed) inside rt2501_receive; anything
     * returned is a data frame the caller owns - V1 main.c hcd_frees it. */
    if (rx_capture && rx_count < RX_QUEUE_MAX) {
      r->next = NULL;
      if (rx_tail)
        rx_tail->next = r;
      else
        rx_head = r;
      rx_tail = r;
      rx_count++;
    } else {
      disable_ohci_irq();
      hcd_free(r);
      enable_ohci_irq();
    }
  }
  if (counter_timer - pump_last_timer >= 200) {
    pump_last_timer = counter_timer;
    rt2501_timer();
  }
}

void wifi_pump(uint32_t ms)
{
  uint32_t t0 = counter_timer;

  while (counter_timer - t0 < ms)
    pump_slice();
}

/* ---- bring-up ------------------------------------------------------------ */
int8_t wifi_up(void)
{
  static int8_t driver_installed;
  int32_t state;
  int attempt;
  int8_t ret;
  uint32_t t0;

  /* Wire the USB host controller IRQ (EXINT2) + install the RT2501 driver
   * once - re-installing duplicates the driver-list entry. */
  IRQ_HANDLER_TABLE[INT_EXINT2] = usbctrl_interrupt;
  clr_hbit(EXIDM, IDM_IDM36 & IDM_IDMP36);
  set_hbit(EXIRQB, IRQB_IRQ36);
  set_wbit(EXILCB, ILC_ILC36 & ILC_INT_LV7);
  usbctrl_host_driver_set(NULL, usbhost_interrupt);
  if (!driver_installed) {
    rt2501_driver_install();
    driver_installed = 1;
  }

  state = RT2501_S_BROKEN;
  for (attempt = 1; attempt <= 3 && state == RT2501_S_BROKEN; attempt++) {
    /* ML60842 re-init alone doesn't recover a wedged dongle - its 8051 keeps
     * running whatever state the last session left, and a JTAG reflash resets
     * only the CPU, never the dongle. Drop VBUS unconditionally so the
     * (internal, host-port-powered) module cold-boots, like a real power-on. */
    usbctrl_vbus_set(VBUS_OFF);
    DelayMs(1000);
    if (usbctrl_init(USB_HOST) != 0)
      continue;
    setup_usb_malloc();
    usbhost_init_status = 0; /* force full hcd re-init on retry */
    ret = usbhost_init();
    if (ret != 0)
      continue;
    t0 = counter_timer;
    do {
      wifi_pump(100);
      state = rt2501_state();
    } while (state == RT2501_S_BROKEN && counter_timer - t0 < 10000);
  }

  if (state == RT2501_S_BROKEN)
    return -1;

  /* A freshly cold-booted radio scans poorly for the first moments (0-2
   * networks vs ~60-80 warm); let the BBP/RF settle before the first scan.
   * Load-bearing: without it the first scan sees nothing (#119). */
  wifi_pump(2000);
  return 0;
}

/* ---- scan ---------------------------------------------------------------- */
int32_t wifi_scan(const char *ssid)
{
  int pass;
  uint32_t t0;

  scan_hits = 0;
  target_hit = 0;
  scan_target_ssid = ssid;

  for (pass = 1; pass <= 5; pass++) {
    rt2501_scan((const uint8_t *)ssid, scan_cb, NULL);
    t0 = counter_timer;
    while (rt2501_state() == RT2501_S_SCAN && counter_timer - t0 < 30000)
      wifi_pump(100);
    if (ssid == NULL)   /* broadcast: one pass */
      break;
    if (target_hit)     /* directed: stop as soon as the AP answers */
      break;
  }
  return scan_hits;
}

int8_t wifi_target(struct rt2501_scan_result *out)
{
  if (!target_hit)
    return 0;
  *out = target_seen;
  return 1;
}

int32_t wifi_seen_count(void)
{
  return scan_hits < MAX_SEEN ? scan_hits : MAX_SEEN;
}

const struct rt2501_scan_result *wifi_seen(int32_t i)
{
  if (i < 0 || i >= wifi_seen_count())
    return NULL;
  return &seen[i];
}

/* ---- join ---------------------------------------------------------------- */
int8_t wifi_join(const struct rt2501_scan_result *target, const char *psk,
                 uint32_t timeout_ms, wifi_step_cb on_step, void *ud)
{
  static uint8_t pmk[32];
  uint32_t t0, last_try;
  int32_t s, last_state = -1, last_dot11 = -1;

  if ((target->encryption & 0xF0) != IEEE80211_CRYPT_NONE)
    mypassword_to_pmk((const uint8_t *)psk, (uint8_t *)target->ssid,
                      (int32_t)strlen((const char *)target->ssid), pmk);

  ieee80211_reject = 0;   /* clear any stale latch before this join */
  /* 2nd arg is the AP's MAC (auth frame addr1/receiver) - pass the scan
   * result's mac field, NOT our own rt2501_mac. */
  rt2501_auth(target->ssid, target->mac, target->bssid, target->channel,
              target->rateset, IEEE80211_AUTH_OPEN, target->encryption, pmk);

  t0 = counter_timer;
  last_try = counter_timer;
  do {
    wifi_pump(100);
    s = rt2501_state();
    /* V1's boot loop re-auths whenever the state machine falls back to IDLE
     * (single-shot auth/assoc regularly loses a frame to a weak AP). */
    if (ieee80211_state == IEEE80211_S_IDLE && counter_timer - last_try >= 3000) {
      rt2501_auth(target->ssid, target->mac, target->bssid, target->channel,
                  target->rateset, IEEE80211_AUTH_OPEN, target->encryption, pmk);
      last_try = counter_timer;
    }
    if (on_step && (s != last_state || ieee80211_state != last_dot11)) {
      last_state = s;
      last_dot11 = ieee80211_state;
      on_step(s, ieee80211_state, eapol_state, ieee80211_reject, ud);
    }
  } while (s != RT2501_S_CONNECTED && counter_timer - t0 < timeout_ms);

  return (s == RT2501_S_CONNECTED) ? 0 : -1;
}

/* ---- one-shot ------------------------------------------------------------ */
int8_t wifi_connect_ex(const char *ssid, const char *psk, uint32_t timeout_ms,
                       wifi_fail_t *why)
{
  struct rt2501_scan_result target;

  if (why)
    *why = WIFI_OK;
  if (wifi_up() != 0) {
    if (why) *why = WIFI_FAIL_RADIO;
    return -1;
  }
  wifi_scan(ssid);
  if (!wifi_target(&target)) {
    if (why) *why = WIFI_FAIL_NOTFOUND;
    return -1;
  }
  if (wifi_join(&target, psk, timeout_ms, NULL, NULL) != 0) {
    if (why)
      *why = ((target.encryption & 0xF0) != IEEE80211_CRYPT_NONE)
             ? WIFI_FAIL_AUTH : WIFI_FAIL_TIMEOUT;
    return -1;
  }
  return 0;
}

int8_t wifi_connect(const char *ssid, const char *psk, uint32_t timeout_ms)
{
  return wifi_connect_ex(ssid, psk, timeout_ms, NULL);
}

int32_t wifi_state(void)
{
  return rt2501_state();
}

/* ---- master (AP) mode + raw frame TX/RX (#216) ---------------------------- */
int8_t wifi_ap(const char *ssid, uint8_t channel)
{
  if (strlen(ssid) > IEEE80211_SSID_MAXLEN)
    return -1;
  /* Skip the cold boot when the dongle is already up (e.g. STA -> AP switch);
   * rt2501_setmode itself flushes the driver's RX queue on the mode change. */
  if (rt2501_state() == RT2501_S_BROKEN && wifi_up() != 0)
    return -1;
  rt2501_setmode(IEEE80211_M_MASTER, (const uint8_t *)ssid, channel);
  rx_capture = 1;
  return 0;
}

int8_t wifi_send(const uint8_t *dst_mac, const uint8_t *payload,
                 uint32_t length)
{
  if (length > WIFI_SEND_MAX)
    return -1;
  /* lowrate=1: 1 Mbps, the most robust rate (and all AP mode supports);
   * mayblock=1 retries the frame alloc, like V1's bytecode send path. Returns
   * 0 immediately when not RUN/EAPOL, so "not connected" fails cleanly. */
  return rt2501_send(payload, length, dst_mac, 1, 1) == 1 ? 0 : -1;
}

const uint8_t *wifi_mac(void)
{
  return rt2501_mac;
}

struct rt2501buffer *wifi_recv_frame(uint32_t timeout_ms)
{
  uint32_t t0 = counter_timer;

  rx_capture = 1;
  for (;;) {
    pump_slice();
    if (rx_head != NULL) {
      struct rt2501buffer *r = rx_head;
      rx_head = r->next;
      if (rx_head == NULL)
        rx_tail = NULL;
      rx_count--;
      return r;
    }
    if (counter_timer - t0 >= timeout_ms)
      return NULL;
  }
}

void wifi_frame_free(struct rt2501buffer *r)
{
  disable_ohci_irq();
  hcd_free(r);
  enable_ohci_irq();
}
