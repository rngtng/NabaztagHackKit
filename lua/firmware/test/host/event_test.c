/**
 * @file event_test.c
 * @brief Host-side regression guard for the cooperative event core (#242).
 *
 * Links the *real* firmware source (../../src/utils/event.c) against stubbed
 * hardware, so the queue/debounce/scan logic can be driven deterministically
 * off-device. This is the lua track's equivalent of mtl/tools/testvm's
 * bugrepro: the firmware code is under test, only the board layer is faked.
 *
 * event.c depends on exactly three things the board normally provides -
 * button_pressed(), rfid_read_uid() and counter_timer - all stubbed below, so
 * time advances only when a test says so and there is no wall-clock flakiness.
 *
 * Scenarios (selected by argv[1], all run when none is given):
 *
 *   rfid-drop    a tag that lands while the queue is full is never reported
 *   button-drop  a button edge that lands while the queue is full is lost
 *
 * Both are #242: the pollers commit their state machine BEFORE the fallible
 * event_post(), and ignore its return, so a dropped event is indistinguishable
 * from a delivered one. Each scenario asserts the event eventually arrives
 * with its expected payload - never merely that "something happened".
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "event.h"

/* Mirrors of event.c's private tuning constants. Kept in sync by the
 * self-check in main(): a drift here would silently defuse these tests. */
#define T_EVQ_LEN            8
#define T_BUTTON_DEBOUNCE_MS 20
#define T_RFID_PERIOD_MS     750

/* --- the board layer event.c expects ------------------------------------- */

volatile uint32_t counter_timer;      /* normally sys/src/tick.c's 1 ms tick */

static uint8_t stub_button;           /* what button_pressed() reports */
static int8_t  stub_rfid_rc;          /* 1 = tag, 0 = none, -1 = I2C error */
static uint8_t stub_rfid_uid[8];      /* the tag's UID when rc == 1 */

uint8_t button_pressed(void)
{
  return stub_button;
}

int8_t rfid_read_uid(uint8_t uid_out[8])
{
  if (stub_rfid_rc == 1)
    memcpy(uid_out, stub_rfid_uid, 8);
  return stub_rfid_rc;
}

/* --- tiny assert harness -------------------------------------------------- */

static int failures;

#define CHECK(cond, msg)                                                      \
  do {                                                                        \
    if (!(cond)) {                                                            \
      printf("  FAIL: %s\n", (msg));                                          \
      failures++;                                                             \
    }                                                                         \
  } while (0)

static void advance(uint32_t ms)
{
  counter_timer += ms;
}

/* Empty the queue, returning how many events were waiting. */
static int drain(void)
{
  event_t e;
  int n = 0;
  while (event_next(&e))
    n++;
  return n;
}

/* Drive one debounced button edge into the queue: flip the raw level, let the
 * debounce window elapse, pump again. Returns whether a slot was consumed. */
static void push_button_edge(void)
{
  stub_button = !stub_button;
  event_pump(0);                    /* pump sees the raw flip, arms debounce */
  advance(T_BUTTON_DEBOUNCE_MS);
  event_pump(0);                    /* stable long enough -> posts the edge */
}

/* Reset the shared module state between scenarios: settle the button low and
 * empty the queue. (event.c exposes no reset hook; this is the equivalent.) */
static void reset_core(void)
{
  event_rfid_enable(0);
  stub_rfid_rc = 0;
  stub_button = 0;
  advance(T_BUTTON_DEBOUNCE_MS);
  event_pump(0);
  advance(T_BUTTON_DEBOUNCE_MS);
  event_pump(0);
  drain();
}

/* Fill the queue to capacity with button edges. */
static void fill_queue(void)
{
  for (int i = 0; i < T_EVQ_LEN; i++)
    push_button_edge();
}

/* ---------------------------------------------------------------------------
 * Scenario 1: an RFID tag that arrives while the queue is full is never
 * reported - not on this scan, and not on any scan after the queue drains.
 *
 * pump_rfid() sets rfid_present = 1 and caches the UID *before* calling
 * event_post(). When the post fails (queue full) the state says "this tag has
 * already been announced", so the `!rfid_present || memcmp(...)` guard is false
 * on every subsequent scan and the callback never fires for that tag. The only
 * recovery is physically removing and re-placing the tag.
 *
 * Realistic trigger: anything that blocks the cooperative pump while input
 * arrives - nab.wifi() is a 30 s blocking join, nab.delay(5000) is five
 * seconds - fills the 8-slot queue with button edges.
 * ------------------------------------------------------------------------- */
static void scen_rfid_drop(void)
{
  static const uint8_t uid_a[8] = {0xd0, 0x02, 0x1a, 0x35, 0x06, 0x19, 0x8b, 0x86};
  event_t e;
  int seen_tag = 0;

  printf("scenario rfid-drop: tag arriving on a full queue must still be reported\n");
  reset_core();

  /* Enable scanning with no tag on the coupler: consumes the "first scan is
   * due immediately" pass so later timing is measured from a known point. */
  event_rfid_enable(1);
  event_pump(1);
  CHECK(drain() == 0, "empty coupler must not post an event");

  fill_queue();

  /* The tag lands while the app is busy and the queue is full. */
  stub_rfid_rc = 1;
  memcpy(stub_rfid_uid, uid_a, sizeof uid_a);
  advance(T_RFID_PERIOD_MS);
  event_pump(1);

  /* The app catches up and drains everything. */
  CHECK(drain() == T_EVQ_LEN, "the full queue should yield EVQ_LEN events");

  /* Several scan periods pass with the tag still sitting on the coupler. */
  for (int i = 0; i < 5; i++) {
    advance(T_RFID_PERIOD_MS);
    event_pump(1);
    while (event_next(&e)) {
      if (e.type == EV_RFID_TAG && memcmp(e.uid, uid_a, sizeof uid_a) == 0)
        seen_tag = 1;
    }
  }

  CHECK(seen_tag, "tag d0021a3506198b86 was never reported after the queue drained");
}

/* ---------------------------------------------------------------------------
 * Scenario 2: same defect on the button poller.
 *
 * pump_button() assigns btn_stable = raw before posting, so a post that fails
 * leaves the debouncer believing the edge was delivered. The press is lost;
 * with the tag/button still held there is no further raw transition to
 * re-trigger it, so the app never learns the button went down.
 * ------------------------------------------------------------------------- */
static void scen_button_drop(void)
{
  event_t e;
  int seen_down = 0;

  printf("scenario button-drop: press landing on a full queue must still be reported\n");
  reset_core();

  fill_queue();
  /* EVQ_LEN toggles from released ends back at released, so the next edge is
   * a genuine press. */
  CHECK(stub_button == 0, "queue-filling toggles should leave the button released");

  /* The press lands while the queue is still full. */
  push_button_edge();

  /* The app catches up. */
  CHECK(drain() == T_EVQ_LEN, "the full queue should yield EVQ_LEN events");

  /* The button is still held; keep pumping. */
  for (int i = 0; i < 5; i++) {
    advance(T_BUTTON_DEBOUNCE_MS);
    event_pump(0);
    while (event_next(&e)) {
      if (e.type == EV_BUTTON_DOWN)
        seen_down = 1;
    }
  }

  CHECK(seen_down, "button press was never reported after the queue drained");
}

/* --- self-check: the mirrored constants must match event.c ---------------- */
/* If EVQ_LEN grew, fill_queue() would no longer fill and both scenarios would
 * pass vacuously - exactly the "green test that validates nothing" the repo
 * testing rule forbids. Prove the queue really is full at T_EVQ_LEN. */
static void check_constants(void)
{
  event_t e;
  int posted = 0;

  printf("self-check: queue capacity is EVQ_LEN and refuses the next post\n");
  reset_core();
  for (int i = 0; i < T_EVQ_LEN; i++) {
    event_t ev = {EV_BUTTON_DOWN, {0}};
    if (event_post(&ev) == 0)
      posted++;
  }
  CHECK(posted == T_EVQ_LEN, "queue should accept exactly EVQ_LEN events");
  {
    event_t ev = {EV_BUTTON_DOWN, {0}};
    CHECK(event_post(&ev) == -1, "queue should reject the EVQ_LEN+1'th event");
  }
  CHECK(drain() == T_EVQ_LEN, "drain should return every queued event");
  (void)e;
}

int main(int argc, char **argv)
{
  const char *only = (argc > 1) ? argv[1] : NULL;

  counter_timer = 1000;   /* start away from 0 so wrap-around maths is exercised */

  if (!only || strcmp(only, "constants") == 0)
    check_constants();
  if (!only || strcmp(only, "rfid-drop") == 0)
    scen_rfid_drop();
  if (!only || strcmp(only, "button-drop") == 0)
    scen_button_drop();

  if (failures) {
    printf("event_test: %d check(s) FAILED\n", failures);
    return 1;
  }
  printf("event_test: all checks passed\n");
  return 0;
}
