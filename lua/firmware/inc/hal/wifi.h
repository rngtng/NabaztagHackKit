/**
 * @file hal/wifi.h
 * @brief RT2501 802.11 join, as a reusable HAL over the vendored USB+wifi stack.
 *
 * The mechanism that wifiprobe proved on hardware (#119): USB-host bring-up +
 * RT2501 driver install, a directed/broadcast scan, and a full WPA/WPA2 join
 * (auth -> assoc -> EAPOL 4-way -> RUN). wifiprobe drives these with diagnostic
 * callbacks; the `nab.wifi` Lua binding calls the one-shot wifi_connect().
 *
 * Requires the tick IRQ running (init_irq() + init_tick()) - the whole stack
 * measures time in `counter_timer` ms. None of these may be called from
 * interrupt context (they pump usbhost_events / rt2501_timer).
 */
#ifndef _HAL_WIFI_H_
#define _HAL_WIFI_H_

#include <stdint.h>

#include "usb/rt2501usb.h"   /* struct rt2501_scan_result, RT2501_S_* */

/* Bring up the USB host controller + RT2501 driver: wire the EXINT2 IRQ, drop
 * VBUS so the (internal, host-port-powered) dongle cold-boots, re-init the HCD,
 * enumerate (this loads the 8051 firmware + does BBP/RF init inside the
 * driver's connect()), pumped until the driver claims the dongle. Retries the
 * whole cycle a few times - enumeration control transfers intermittently time
 * out right after reset on this hardware. A freshly reset radio then gets a
 * settle pump before returning (it scans nothing for the first moments).
 * Returns 0 on success, <0 if the driver never left RT2501_S_BROKEN. */
int8_t wifi_up(void);

/* Cooperative heartbeat: pump USB events + drain/free RX buffers + fire the
 * ~200 ms driver timer for `ms`. Call in a loop to interleave other work;
 * wifi_scan/wifi_join pump this internally. Not from interrupt context. */
void wifi_pump(uint32_t ms);

/* Scan for `ssid` (directed - the probe request carries it, so an AP answers
 * even when its passive beacon would be missed) or NULL (broadcast). Retries a
 * few passes, stopping early once the directed target is captured. Results are
 * deduped into a private table (see wifi_seen); returns the number of distinct
 * networks seen. The scan callback runs in the RX IRQ path, so it records only
 * - print the results afterwards from the main loop via wifi_seen(). */
int32_t wifi_scan(const char *ssid);

/* If wifi_scan captured the requested SSID, copy it into *out and return 1;
 * else return 0. */
int8_t wifi_target(struct rt2501_scan_result *out);

/* Deduped scan results from the last wifi_scan, for diagnostic listing from the
 * main loop. wifi_seen_count() is the number available; wifi_seen(i) returns
 * entry i (0-based) or NULL if out of range. */
int32_t wifi_seen_count(void);
const struct rt2501_scan_result *wifi_seen(int32_t i);

/* Optional per-pump progress callback for wifi_join, invoked from the main loop
 * (safe to print) after each pump slice with the current rt2501 state
 * (RT2501_S_*), 802.11 state (IEEE80211_S_*), EAPOL state, and the latched AP
 * reject code (ieee80211_reject; 0 = none). May be NULL. */
typedef void (*wifi_step_cb)(int32_t rt_state, int32_t dot11_state,
                             int32_t eapol_state, uint16_t reject, void *ud);

/* Associate + WPA/WPA2 4-way handshake with `target` using `psk` (ignored for
 * an open network). Derives the PMK (PBKDF2, seconds on-device), authenticates
 * to the AP - the auth frame is addressed to target->mac, NOT our own MAC - and
 * pumps until connected (RT2501_S_CONNECTED) or `timeout_ms`, re-authing on an
 * IDLE fall-back (a weak AP regularly drops one frame). Returns 0 connected,
 * <0 on timeout; on failure ieee80211_reject holds the last AP reject. */
int8_t wifi_join(const struct rt2501_scan_result *target, const char *psk,
                 uint32_t timeout_ms, wifi_step_cb on_step, void *ud);

/* One-shot for the nab binding: wifi_up() + wifi_scan(ssid) + wifi_join() with
 * no tracing. Returns 0 connected, <0 if bring-up, scan (SSID not found) or the
 * join failed. */
int8_t wifi_connect(const char *ssid, const char *psk, uint32_t timeout_ms);

/* rt2501_state() passthrough (RT2501_S_*). */
int32_t wifi_state(void);

/* ---- master (AP) mode + raw frame TX/RX (#216) ---------------------------- */

/* Largest wifi_send payload (the 802.3 payload after the MAC header; an
 * Ethernet-MTU-sized bound well under the driver's 2048-byte frame cap). */
#define WIFI_SEND_MAX 1500

/* Switch the radio to master (AP) mode: beacon `ssid` (<= 32 chars) on
 * `channel` as an OPEN network (setup-mode only, like Violet's original).
 * Runs wifi_up() first only if the dongle isn't up yet, so switching an
 * already-joined STA doesn't cold-boot the radio. Also enables RX capture
 * (see wifi_recv_frame). Returns 0, <0 on bad ssid / bring-up failure. */
int8_t wifi_ap(const char *ssid, uint8_t channel);

/* Send one data frame carrying `length` payload bytes to `dst_mac` (6 bytes) -
 * the same 802.3-payload seam rt2501_send speaks (payload starts at LLC; no
 * 802.11 details cross here). Works associated (STA, encrypted with the
 * session keys by the driver) or in AP mode. Returns 0, <0 when not in a
 * TX-capable state or the driver rejected the frame. */
int8_t wifi_send(const uint8_t *dst_mac, const uint8_t *payload,
                 uint32_t length);

/* Pop the oldest captured RX data frame, pumping until one arrives or
 * `timeout_ms` elapses (0 = single poll). The first call enables capture:
 * from then on the pump queues drained data frames (bounded; overflow drops)
 * instead of freeing them. Caller owns the returned buffer and must release
 * it with wifi_frame_free(). NULL when nothing arrived. */
struct rt2501buffer *wifi_recv_frame(uint32_t timeout_ms);

/* Release a frame returned by wifi_recv_frame (hcd_free under the OHCI IRQ
 * guard - the ownership rule every drained RX buffer follows). */
void wifi_frame_free(struct rt2501buffer *r);

#endif /* _HAL_WIFI_H_ */
