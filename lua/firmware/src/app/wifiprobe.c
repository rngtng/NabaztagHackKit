/**
 * @file wifiprobe.c
 * @brief RT2501 802.11 bring-up probe: firmware blob load, MAC from EEPROM, an
 *        AP scan with per-network results, and (with WIFI_SSID set) STA
 *        association + WPA handshake - proves USB bulk transfers, the 8051
 *        firmware upload, radio RX, the beacon-parse path and the join path
 *        end-to-end.
 *
 * Stages ([1]-[3] identical to usbprobe.c):
 *   [4] rt2501_driver_install + pump until the driver claims the dongle
 *       (firmware upload + BBP/RF init happen inside its connect()) -
 *       prints the EEPROM MAC and rt2501_state.
 *   [5] rt2501_scan(NULL, ...) broadcast scan, pumping
 *       usbhost_events/rt2501_receive/rt2501_timer exactly like V1 main.c;
 *       each callback prints SSID/channel/RSSI/encryption; ends when the
 *       state machine leaves RT2501_S_SCAN.
 *   [6] (WIFI_SSID only) associate + 4-way handshake against the test AP.
 *
 * Run: task lua:firmware:flash APP=wifiprobe
 * Output is on UART0 (38400 8N1), read on the Pi's /dev/serial0 (see
 * uartprobe.c for the flash+listen recipe).
 * Hardware-only (the sim has no OHCI model).
 */
#include <string.h>

#include "ml674061.h"
#include "ml60842.h"
#include "common.h"
#include "irq.h"

#include "utils/delay.h"
#include "utils/mem.h"

#include "usb/usbh.h"
#include "usb/usbctrl.h"
#include "usb/rt2501usb.h"
#include "net/eapol.h"

#include "hal/spi.h"
#include "hal/led.h"
#include "hal/uart.h"

static void sh_puts(const char *s)
{
  putst_uart((uint8_t *)s);
}

static void sh_puthex8(uint8_t v)
{
  const char *hex = "0123456789abcdef";
  char b[3] = {hex[(v >> 4) & 0xF], hex[v & 0xF], '\0'};
  sh_puts(b);
}

static void sh_putdec(int32_t v)
{
  char b[12];
  int i = 11;
  uint32_t u;
  b[i] = '\0';
  u = (v < 0) ? (uint32_t)-v : (uint32_t)v;
  do {
    b[--i] = '0' + (u % 10);
    u /= 10;
  } while (u && i > 1);
  if (v < 0)
    b[--i] = '-';
  sh_puts(&b[i]);
}

static void sh_putmac(const uint8_t *m)
{
  int i;
  for (i = 0; i < 6; i++) {
    sh_puthex8(m[i]);
    if (i < 5)
      sh_puts(":");
  }
}

/* ---- [5] scan callback --------------------------------------------------- */
/* The callback fires from the USB rx IRQ path, once per received
 * beacon/probe-response. It must NOT print: putst_uart spins on THR-empty at
 * 38400 baud, so dozens of blocking char writes per beacon stall the rx IRQ
 * path - starving the tick IRQ and the OHCI ISR, timing out the scan's own
 * control transfers and potentially wedging the interrupt controller. So the
 * callback only records deduped results; main prints them afterwards. */
#define MAX_SEEN 32
static struct rt2501_scan_result seen[MAX_SEEN];
static volatile int scan_hits;

static void scan_cb(struct rt2501_scan_result *r, void *userparam)
{
  int i, j;
  (void)userparam;
  for (i = 0; i < scan_hits && i < MAX_SEEN; i++) {
    for (j = 0; j < 6; j++)
      if (seen[i].bssid[j] != r->bssid[j])
        break;
    if (j == 6)
      return;
  }
  if (scan_hits < MAX_SEEN)
    seen[scan_hits] = *r;
  scan_hits++;
}

static void print_scan_results(void)
{
  int i;
  for (i = 0; i < scan_hits && i < MAX_SEEN; i++) {
    sh_puts("    ssid=\"");
    sh_puts((const char *)seen[i].ssid);
    sh_puts("\" bssid=");
    sh_putmac(seen[i].bssid);
    sh_puts(" ch=");
    sh_putdec(seen[i].channel);
    sh_puts(" rssi=");
    sh_putdec(seen[i].rssi);
    sh_puts(" enc=");
    sh_putdec(seen[i].encryption);
    sh_puts("\n");
  }
}

/* V1 main.c's pump: usb events + rx drain + the ~100 ms driver timer */
static void pump_ms(uint32_t ms)
{
  uint32_t t0 = counter_timer, last_timer = counter_timer;
  struct rt2501buffer *r;

  while (counter_timer - t0 < ms) {
    usbhost_events();
    while ((r = rt2501_receive()) != NULL)
      ; /* drain; driver frees via buffer queue - nothing to do with data here */
    if (counter_timer - last_timer >= 100) {
      last_timer = counter_timer;
      rt2501_timer();
    }
  }
}

/* usbhcore.c's init-once guard; cleared for the [4] retry re-init cycle */
extern uint8_t usbhost_init_status;

#ifdef WIFI_SSID
/* eapol.c's PBKDF2 (4096-iter HMAC-SHA1 x2); no header declares it - V1's
 * vm/vnet.c declared it the same way at its netPmk call site. */
void mypassword_to_pmk(const uint8_t *password, uint8_t *ssid,
                       int32_t ssidlength, uint8_t *pmk);

static const char *state_name(int32_t s)
{
  switch (s) {
  case RT2501_S_BROKEN:     return "BROKEN";
  case RT2501_S_IDLE:       return "IDLE";
  case RT2501_S_SCAN:       return "SCAN";
  case RT2501_S_CONNECTING: return "CONNECTING";
  case RT2501_S_CONNECTED:  return "CONNECTED";
  case RT2501_S_MASTER:     return "MASTER";
  default:                  return "?";
  }
}

/* The 802.11 state machine itself (ieee80211.h extern) - rt2501_state()
 * flattens AUTH/ASSOC/EAPOL into CONNECTING, but *which* of those the
 * machine dies in is what we want to see. */
static const char *dot11_name(int32_t s)
{
  switch (s) {
  case IEEE80211_S_IDLE:  return "IDLE";
  case IEEE80211_S_SCAN:  return "SCAN";
  case IEEE80211_S_AUTH:  return "AUTH";
  case IEEE80211_S_ASSOC: return "ASSOC";
  case IEEE80211_S_EAPOL: return "EAPOL";
  case IEEE80211_S_RUN:   return "RUN";
  default:                return "?";
  }
}
#endif

int main(void)
{
  volatile uint32_t spin;
  uint32_t t0;
  int8_t ret;
  int32_t state;
  int attempt;

  init_uart();

  sh_puts("#119 RT2501 802.11 bring-up probe\n");

  /* Park the audio amplifier: probes never init audio, so the amp pin sits
   * in its power-on state and USB traffic on the ExtRAM/EMC bus couples into
   * the speaker as clicks/static. Amp off is enough - full init_vlsi would
   * hang here (its wait_dreq needs init_spi first). */
  CS_AUDIO_AMP_AS_OUTPUT;
  TURN_OFF_AUDIO_AMPLIFIER;

  /* SPI + LED driver: eapol.c's PBKDF2/handshake blinks the nose LEDs as
   * progress (set_led every 64 iterations) - with SPI uninitialized that
   * set_led polls SPIF forever and wedges the PMK derivation. */
  init_spi();
  init_led_rgb_driver();

  /* [1] tick IRQ (see usbprobe.c) */
  init_irq();
  init_tick();
  for (spin = 0; spin < 2000000; spin++)
    CLR_WDT;
  sh_puts("[1] tick counter_timer=");
  sh_putdec((int32_t)counter_timer);
  sh_puts(counter_timer ? " PASS\n" : " FAIL - IRQ dead, aborting\n");
  if (!counter_timer)
    goto done;

  /* [2][3][4] usb host bring-up + rt2501 driver (enumeration loads the 8051
   * firmware + BBP/RF init inside the driver's connect()). Enumeration
   * control transfers intermittently time out right after reset on this
   * hardware (V1's answer was a watchdog reboot when wifi came up BROKEN),
   * so the whole ML60842-reset -> hcd-init -> enumerate cycle is retried. */
  IRQ_HANDLER_TABLE[INT_EXINT2] = usbctrl_interrupt;
  clr_hbit(EXIDM, IDM_IDM36 & IDM_IDMP36);
  set_hbit(EXIRQB, IRQB_IRQ36);
  set_wbit(EXILCB, ILC_ILC36 & ILC_INT_LV7);
  usbctrl_host_driver_set(NULL, usbhost_interrupt);
  rt2501_driver_install(); /* once - re-installing duplicates the list entry */

  state = RT2501_S_BROKEN;
  for (attempt = 1; attempt <= 3 && state == RT2501_S_BROKEN; attempt++) {
    sh_puts("[2][3] usb host bring-up, attempt ");
    sh_putdec(attempt);
    sh_puts("\n");
    if (attempt > 1) {
      /* ML60842 re-init alone doesn't recover a wedged dongle - its 8051
       * keeps running whatever state the last session left. Drop VBUS so
       * the (internal, host-port-powered) module actually cold-boots. */
      sh_puts("    VBUS power-cycle\n");
      usbctrl_vbus_set(VBUS_OFF);
      DelayMs(1000);
    }
    if (usbctrl_init(USB_HOST) != 0) {
      sh_puts("    usbctrl_init FAIL\n");
      continue;
    }
    setup_usb_malloc();
    usbhost_init_status = 0; /* force full hcd re-init on retry */
    ret = usbhost_init();
    if (ret != 0) {
      sh_puts("    usbhost_init FAIL\n");
      continue;
    }
    t0 = counter_timer;
    do {
      pump_ms(100);
      state = rt2501_state();
    } while (state == RT2501_S_BROKEN && counter_timer - t0 < 10000);
    if (state == RT2501_S_BROKEN)
      sh_puts("    BROKEN after 10s\n");
  }

  sh_puts("[4] rt2501_state=");
  sh_putdec(state);
  sh_puts(" mac=");
  sh_putmac(rt2501_mac);
  sh_puts(state != RT2501_S_BROKEN ? " PASS\n" : " FAIL - driver never came up\n");
  if (state == RT2501_S_BROKEN)
    goto done;

  /* [5] scan. With a target SSID, scan DIRECTED (pass the SSID): the probe
   * request carries it, so the AP answers with a probe-response even when we
   * would miss its passive beacon in the per-channel window - the broadcast
   * scan kept missing a weak ch13 guest AP. Retry until the target shows up
   * (or a cap), since even directed catch is probabilistic per pass. */
  {
    int pass;
#ifdef WIFI_SSID
    const char *scan_ssid = WIFI_SSID;
    int want_target = 1;
#else
    const char *scan_ssid = NULL;
    int want_target = 0;
#endif
    scan_hits = 0;
    for (pass = 1; pass <= 2; pass++) {
      sh_puts("[5] scan pass ");
      sh_putdec(pass);
      sh_puts(scan_ssid ? " (directed)...\n" : " (broadcast)...\n");
      rt2501_scan((const uint8_t *)scan_ssid, scan_cb, NULL);
      t0 = counter_timer;
      while (rt2501_state() == RT2501_S_SCAN && counter_timer - t0 < 30000)
        pump_ms(100);
      if (!want_target)
        break;
#ifdef WIFI_SSID
      {
        int i, j, found = 0;
        for (i = 0; i < scan_hits && i < MAX_SEEN; i++) {
          const char *w = WIFI_SSID;
          for (j = 0; w[j] && (char)seen[i].ssid[j] == w[j]; j++)
            ;
          if (!w[j] && !seen[i].ssid[j]) { found = 1; break; }
        }
        if (found)
          break;
      }
#endif
    }
    sh_puts("[5] scan finished: ");
    sh_putdec(scan_hits);
    sh_puts(" networks, rt2501_state=");
    sh_putdec(rt2501_state());
    sh_puts("\n");
  }
  /* Full listing printed LAST (after [6]): ~30 lines at 38400 baud take
   * longer than the whole association - don't let the console output eat
   * the verdict. */

#ifdef WIFI_SSID
  /* [6] STA association + WPA handshake against the test AP (SSID/PSK from
   * the environment at build time - see Makefile). This is the path that
   * drives V1 to BROKEN. */
  {
    static uint8_t pmk[32];
    static struct rt2501_scan_result synth;
    struct rt2501_scan_result *target = NULL;
    int32_t last_state, s;
    int i, j;

    for (i = 0; i < scan_hits && i < MAX_SEEN; i++) {
      const char *w = WIFI_SSID;
      for (j = 0; w[j] && (char)seen[i].ssid[j] == w[j]; j++)
        ;
      if (!w[j] && !seen[i].ssid[j]) {
        target = &seen[i];
        break;
      }
    }
#ifdef WIFI_BSSID
    if (target == NULL) {
      /* Scan missed it (weak band-edge AP) - synthesize the target from the
       * known BSSID/channel so the association path is tested regardless.
       * Parse "aa:bb:cc:dd:ee:ff" -> 6 bytes. */
      const char *b = WIFI_BSSID;
      for (i = 0; i < 6; i++) {
        uint8_t hi = (b[0] <= '9') ? b[0] - '0' : (b[0] | 0x20) - 'a' + 10;
        uint8_t lo = (b[1] <= '9') ? b[1] - '0' : (b[1] | 0x20) - 'a' + 10;
        synth.bssid[i] = (uint8_t)((hi << 4) | lo);
        b += 3; /* two hex digits + separator */
      }
      memcpy(synth.mac, synth.bssid, 6);
      for (j = 0; WIFI_SSID[j]; j++)
        synth.ssid[j] = (uint8_t)WIFI_SSID[j];
      synth.ssid[j] = 0;
      synth.channel = WIFI_CHANNEL;
      synth.rssi = 0;
      /* All 802.11b/g rates; the AP takes the subset it supports. */
      synth.rateset = 0xFFF;
      synth.encryption = IEEE80211_CRYPT_WPA2 | (IEEE80211_CIPHER_CCMP << 1);
      target = &synth;
      sh_puts("[6] SSID not scanned - using known BSSID " WIFI_BSSID "\n");
    }
#endif
    if (target == NULL) {
      sh_puts("[6] SSID \"" WIFI_SSID "\" not in scan results - skipping\n");
      goto done;
    }

    sh_puts("[6] target found: bssid=");
    sh_putmac(target->bssid);
    sh_puts(" ch=");
    sh_putdec(target->channel);
    sh_puts(" enc=");
    sh_putdec(target->encryption);
    sh_puts("\n[6] deriving PMK (PBKDF2 on-device, a few seconds)...\n");
    if ((target->encryption & 0xF0) != IEEE80211_CRYPT_NONE)
      mypassword_to_pmk((const uint8_t *)WIFI_PSK, target->ssid,
                        (int32_t)sizeof(WIFI_SSID) - 1, pmk);

    sh_puts("[6] rt2501_auth...\n");
    rt2501_auth(target->ssid, rt2501_mac, target->bssid, target->channel,
                target->rateset, IEEE80211_AUTH_OPEN, target->encryption, pmk);

    /* Pump and report every state-machine transition; the auth/assoc/EAPOL
     * exchange runs inside the stack (rx IRQ + rt2501_timer retries).
     * ieee80211_state is tracked separately - which sub-state dies tells us
     * where (AUTH: no/bad auth response; ASSOC: assoc rejected, e.g. RSN IE;
     * EAPOL: 4-way handshake). */
    t0 = counter_timer;
    last_state = -1;
    {
      int32_t last_dot11 = -1, d;
      do {
        pump_ms(100);
        s = rt2501_state();
        d = ieee80211_state;
        if (s != last_state || d != last_dot11) {
          last_state = s;
          last_dot11 = d;
          sh_puts("    t=");
          sh_putdec((int32_t)((counter_timer - t0) / 1000));
          sh_puts("s state=");
          sh_puts(state_name(s));
          sh_puts(" 802.11=");
          sh_puts(dot11_name(d));
          sh_puts(" timeout=");
          sh_putdec((int32_t)ieee80211_timeout);
          sh_puts(" eapol=");
          sh_putdec(eapol_state);
          sh_puts("\n");
        }
      } while (s != RT2501_S_CONNECTED && counter_timer - t0 < 30000);
    }

    sh_puts(s == RT2501_S_CONNECTED
              ? "[6] ASSOCIATED - WPA handshake complete\n"
              : "[6] FAIL - not connected after 30s\n");
  }
#endif

done:
  print_scan_results();
  sh_puts("<<FV_DONE>>\n");
  for (;;)
    CLR_WDT;
}
