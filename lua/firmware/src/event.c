/**
 * @file event.c
 * @brief Cooperative event core (#195) - see inc/event.h.
 *
 * Pollers, not ISRs: button debouncing and the CRX14 scan cycle run inside
 * event_pump(), called from the cooperative main loop. Timing comes from the
 * 1 ms tick (sys/src/tick.c, counter_timer). In the Unicorn simulator the
 * tick is frozen at 0, which disarms the debounce/period checks - the pump
 * degrades to a raw button read that never posts (the sim models neither
 * RFID nor time, so that is the correct no-op).
 */
#include <string.h>

#include "event.h"
#include "hal/button.h"
#include "hal/rfid.h"
#include "utils/delay.h"

#define EVQ_LEN 8               /* events; on overflow the newest is dropped */
#define BUTTON_DEBOUNCE_MS 20   /* raw state stable this long -> a real edge */
#define RFID_PERIOD_MS 750      /* CRX14 scan cycle (V1's cadence, #184) */
#define RFID_MISS_MAX 2         /* empty scans before "tag gone" - a present
                                   tag occasionally misses one scan (#180) */

static event_t q[EVQ_LEN];
static uint8_t q_read, q_count;

int8_t event_post(const event_t *e)
{
  if (q_count >= EVQ_LEN)
    return -1;
  q[(uint8_t)(q_read + q_count) % EVQ_LEN] = *e;
  q_count++;
  return 0;
}

int8_t event_next(event_t *e)
{
  if (q_count == 0)
    return 0;
  *e = q[q_read];
  q_read = (uint8_t)((q_read + 1) % EVQ_LEN);
  q_count--;
  return 1;
}

/* ---- head button: debounced press/release edges --------------------------- */

static uint8_t btn_stable;   /* last debounced state (1 = pressed) */
static uint8_t btn_raw_prev; /* last raw read */
static uint32_t btn_edge_ms; /* counter_timer when the raw state last flipped */

static void pump_button(void)
{
  uint8_t raw = button_pressed();
  if (raw != btn_raw_prev) {
    btn_raw_prev = raw;
    btn_edge_ms = counter_timer;
    return;
  }
  if (raw != btn_stable && (counter_timer - btn_edge_ms) >= BUTTON_DEBOUNCE_MS) {
    btn_stable = raw;
    event_t e = { raw ? EV_BUTTON_DOWN : EV_BUTTON_UP, {0} };
    event_post(&e);
  }
}

/* ---- RFID: scan the coupler on its own cycle, post UID transitions -------- */

static uint8_t rfid_on;      /* poll at all? (a Lua callback is registered) */
static uint8_t rfid_present; /* debounced "a tag is on the coupler" */
static uint8_t rfid_uid[8];  /* its UID */
static uint8_t rfid_miss;    /* consecutive empty scans while present */
static uint32_t rfid_last_ms;

void event_rfid_enable(uint8_t on)
{
  rfid_on = on;
  rfid_present = 0;
  rfid_miss = 0;
  rfid_last_ms = counter_timer - RFID_PERIOD_MS; /* first scan due now */
}

static void pump_rfid(void)
{
  if (!rfid_on || (counter_timer - rfid_last_ms) < RFID_PERIOD_MS)
    return;
  rfid_last_ms = counter_timer;

  uint8_t uid[8];
  int8_t r = rfid_read_uid(uid);
  if (r < 0)
    return; /* transient I2C fault: keep the last known state */
  if (r == 0) {
    if (rfid_present && ++rfid_miss >= RFID_MISS_MAX) {
      rfid_present = 0;
      event_t e = { EV_RFID_GONE, {0} };
      event_post(&e);
    }
    return;
  }
  rfid_miss = 0;
  if (!rfid_present || memcmp(uid, rfid_uid, sizeof rfid_uid) != 0) {
    rfid_present = 1;
    memcpy(rfid_uid, uid, sizeof rfid_uid);
    event_t e;
    e.type = EV_RFID_TAG;
    memcpy(e.uid, uid, sizeof e.uid);
    event_post(&e);
  }
}

void event_pump(uint8_t allow_rfid)
{
  pump_button();
  if (allow_rfid)
    pump_rfid();
}
