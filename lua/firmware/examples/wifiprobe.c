/**
 * @file wifiprobe.c
 * @brief RT2501 802.11 bring-up probe: USB host + driver bring-up, an AP scan
 *        with per-network results, and (with WIFI_SSID set) STA association +
 *        WPA handshake - proves USB bulk transfers, the 8051 firmware upload,
 *        radio RX, the beacon-parse path and the join path end-to-end.
 *
 * The join mechanism itself lives in hal/wifi.c (shared with the `nab.wifi`
 * binding); this probe is the diagnostic oracle over it - it prints the EEPROM
 * MAC, the full scan list, and every 802.11 state transition + AP reject code,
 * which the bare binding does not.
 *
 * Stages:
 *   [1] tick IRQ up (counter_timer advancing).
 *   [2][3][4] wifi_up(): USB host + RT2501 driver (firmware upload + BBP/RF
 *       init inside the driver connect()); prints the EEPROM MAC + rt2501_state.
 *   [5] wifi_scan(): directed (WIFI_SSID) or broadcast scan; the deduped list
 *       is printed last (after the verdict - ~30 lines at 38400 baud take
 *       longer than the association itself).
 *   [6] (WIFI_SSID only) wifi_join(): associate + 4-way handshake, tracing each
 *       transition.
 *
 * Run: task lua:firmware:flash EXAMPLE=wifiprobe CAPTURE=1 (SSID/PSK from .env)
 * flashes and captures its UART transcript until <<FV_DONE>>; or flash without
 * CAPTURE and read /dev/serial0 by hand (UART0, 38400 8N1).
 * Hardware-only (the sim has no OHCI model).
 */
#include <string.h>

#include "ml674061.h"
#include "common.h"
#include "irq.h"

#include "utils/delay.h"

#include "usb/rt2501usb.h"   /* rt2501_mac, struct rt2501_scan_result, RT2501_S_* */
#include "net/eapol.h"       /* eapol_state */
#include "net/ieee80211.h"   /* ieee80211_state/timeout/reject, IEEE80211_* */

#include "hal/spi.h"
#include "hal/led.h"
#include "hal/uart.h"
#include "hal/wifi.h"

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

/* Full scan listing, from the HAL's deduped table. Printed from the main loop
 * (the scan callback runs in the RX IRQ path and must not touch the UART). */
static void print_scan_results(void)
{
  int32_t i, n = wifi_seen_count();
  for (i = 0; i < n; i++) {
    const struct rt2501_scan_result *r = wifi_seen(i);
    sh_puts("    ssid=\"");
    sh_puts((const char *)r->ssid);
    sh_puts("\" bssid=");
    sh_putmac(r->bssid);
    sh_puts(" ch=");
    sh_putdec(r->channel);
    sh_puts(" rssi=");
    sh_putdec(r->rssi);
    sh_puts(" enc=");
    sh_putdec(r->encryption);
    sh_puts("\n");
  }
}

#ifdef WIFI_SSID
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

/* rt2501_state() flattens AUTH/ASSOC/EAPOL into CONNECTING; the 802.11 state
 * says which of those the machine is actually in. */
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

/* Decode the reject code the 802.11 stack latched at a silent fall-back to
 * IDLE - the one datum the state trace alone can't show (why the AP dropped
 * us). AUTH-deny/ASSOC-reject codes are 802.11 status codes; DEAUTH is a
 * reason code. See ieee80211_reject in ieee80211.c. */
static void print_reject(uint16_t r)
{
  if (!r)
    return;
  sh_puts(" reject=");
  switch (r >> 12) {
  case 1:  sh_puts("AUTH-deny");    break;
  case 2:  sh_puts("ASSOC-reject"); break;
  case 3:  sh_puts("DEAUTH");       break;
  default: sh_puts("?");            break;
  }
  sh_puts(" code=");
  sh_putdec(r & 0x0FFF);
}

/* wifi_join step callback: print every 802.11/rt2501/eapol transition. */
static uint32_t join_t0;
static void join_step(int32_t s, int32_t d, int32_t eap, uint16_t reject, void *ud)
{
  (void)ud;
  sh_puts("    t=");
  sh_putdec((int32_t)((counter_timer - join_t0) / 1000));
  sh_puts("s state=");
  sh_puts(state_name(s));
  sh_puts(" 802.11=");
  sh_puts(dot11_name(d));
  sh_puts(" timeout=");
  sh_putdec((int32_t)ieee80211_timeout);
  sh_puts(" eapol=");
  sh_putdec(eap);
  print_reject(reject);
  sh_puts("\n");
}
#endif

int main(void)
{
  volatile uint32_t spin;

  init_uart();

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

  /* [1] tick IRQ (see usbprobe.c) - the wifi stack needs counter_timer. */
  init_irq();
  init_tick();

  /* The JTAG flash resets the CPU immediately, but the UART capture tool
   * (uart_repl.py) only stops the serial-getty and attaches to /dev/serial0
   * ~2-4 s later - until it does, the getty consumes everything the device
   * prints. That silently ate the banner and the whole [1]-[4] bring-up trace
   * (first captured line was [5]). Stay quiet until the reader owns the port.
   * DelayMs needs the tick, so this must sit after init_tick(). */
  DelayMs(6000);

  sh_puts("#119 RT2501 802.11 bring-up probe\n");

  for (spin = 0; spin < 2000000; spin++)
    CLR_WDT;
  sh_puts("[1] tick counter_timer=");
  sh_putdec((int32_t)counter_timer);
  sh_puts(counter_timer ? " PASS\n" : " FAIL - IRQ dead, aborting\n");
  if (!counter_timer)
    goto done;

  /* [2][3][4] USB host + RT2501 driver bring-up (retried inside wifi_up). */
  sh_puts("[2][3][4] USB host + RT2501 bring-up...\n");
  if (wifi_up() != 0) {
    sh_puts("[4] FAIL - driver never came up\n");
    goto done;
  }
  sh_puts("[4] rt2501_state=");
  sh_putdec(wifi_state());
  sh_puts(" mac=");
  sh_putmac(rt2501_mac);
  sh_puts(" PASS\n");

  /* [5] scan. With a target SSID, scan DIRECTED so the AP answers our probe
   * request even when we would miss its passive beacon (a weak ch13 guest AP). */
  {
#ifdef WIFI_SSID
    const char *scan_ssid = WIFI_SSID;
#else
    const char *scan_ssid = NULL;
#endif
    sh_puts("[5] scanning");
    sh_puts(scan_ssid ? " (directed)...\n" : " (broadcast)...\n");
    sh_puts("[5] scan finished: ");
    sh_putdec(wifi_scan(scan_ssid));
    sh_puts(" networks, rt2501_state=");
    sh_putdec(wifi_state());
    sh_puts("\n");
  }

#ifdef WIFI_SSID
  /* [6] STA association + WPA handshake against the test AP (SSID/PSK from
   * the environment at build time - see Makefile). */
  {
    struct rt2501_scan_result target;
    int8_t have_target = wifi_target(&target);
    int8_t r;

#ifdef WIFI_BSSID
    if (!have_target) {
      /* Scan missed it (weak band-edge AP) - synthesize the target from the
       * known BSSID/channel so the association path is tested regardless.
       * Parse "aa:bb:cc:dd:ee:ff" -> 6 bytes. */
      const char *b = WIFI_BSSID;
      int i, j;
      for (i = 0; i < 6; i++) {
        uint8_t hi = (b[0] <= '9') ? b[0] - '0' : (b[0] | 0x20) - 'a' + 10;
        uint8_t lo = (b[1] <= '9') ? b[1] - '0' : (b[1] | 0x20) - 'a' + 10;
        target.bssid[i] = (uint8_t)((hi << 4) | lo);
        b += 3; /* two hex digits + separator */
      }
      memcpy(target.mac, target.bssid, 6);
      for (j = 0; WIFI_SSID[j]; j++)
        target.ssid[j] = (uint8_t)WIFI_SSID[j];
      target.ssid[j] = 0;
      target.channel = WIFI_CHANNEL;
      target.rssi = 0;
      /* All 802.11b/g rates; the AP takes the subset it supports. */
      target.rateset = 0xFFF;
      target.encryption = IEEE80211_CRYPT_WPA2 | (IEEE80211_CIPHER_CCMP << 1);
      have_target = 1;
      sh_puts("[6] SSID not scanned - using known BSSID " WIFI_BSSID "\n");
    }
#endif
    if (!have_target) {
      sh_puts("[6] SSID \"" WIFI_SSID "\" not in scan results - skipping\n");
      goto done;
    }

    sh_puts("[6] target found: bssid=");
    sh_putmac(target.bssid);
    sh_puts(" ch=");
    sh_putdec(target.channel);
    sh_puts(" enc=");
    sh_putdec(target.encryption);
    sh_puts("\n[6] joining (PMK PBKDF2 on-device, a few seconds)...\n");

    join_t0 = counter_timer;
    r = wifi_join(&target, WIFI_PSK, 30000, join_step, NULL);
    if (r == 0) {
      sh_puts("[6] ASSOCIATED - WPA handshake complete\n");
    } else {
      sh_puts("[6] FAIL - not connected after 30s (last");
      print_reject(ieee80211_reject);
      sh_puts(")\n");
    }
  }
#endif

done:
  print_scan_results();
  sh_puts("<<FV_DONE>>\n");
  for (;;)
    CLR_WDT;
}
