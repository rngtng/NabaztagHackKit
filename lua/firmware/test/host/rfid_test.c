/**
 * @file rfid_test.c
 * @brief Host-side bounds guard for the CRX14 scan retry logic (#253).
 *
 * rfid.c is board-independent - it drives the coupler entirely through
 * write_i2c/read_i2c - so the whole anti-collision scan can run natively with
 * the I2C boundary stubbed. That is the only practical way to test the
 * wedged-bus path: it needs a fault (SDA held low) that cannot be provoked on
 * real hardware on demand, which is exactly why it went unnoticed.
 *
 * Scenarios (argv[1] selects one; all run by default):
 *
 *   wedged   a bus that never answers must fail fast, not spin for minutes
 *   nack     a device that NAcks is a different fault and must stay cheap
 *   happy    a tag still reads back correctly through the retry path
 */
#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "ml674061.h"
#include "common.h"
#include "hal/i2c.h"
#include "hal/rfid.h"
#include "utils/delay.h"

/* --- the board layer rfid.c expects --------------------------------------- */

volatile uint32_t counter_timer;
volatile uint32_t counter_timer_s;

void DelayMs(uint16_t ms)
{
  counter_timer += ms;   /* the settle waits are the only clock consumer here */
}

/* Stubbed I2C. `i2c_answers` = 1 makes every transfer succeed, 0 makes it
 * fail like a wedged/absent bus. Every call is counted: the count IS the
 * bound under test. */
static int      i2c_answers;
static unsigned i2c_calls;
static uint8_t  frame_reply[20];   /* what read_i2c hands back */
static uint8_t  frame_reply_len;

uint8_t write_i2c(uint8_t addr, uint8_t *data, uint8_t n)
{
  (void)addr; (void)data; (void)n;
  i2c_calls++;
  return (uint8_t)(i2c_answers ? 1 : 0);
}

uint8_t read_i2c(uint8_t addr, uint8_t *data, uint8_t n)
{
  (void)addr;
  i2c_calls++;
  if (!i2c_answers)
    return 0;
  for (uint8_t i = 0; i < n; i++)
    data[i] = (i < frame_reply_len) ? frame_reply[i] : 0;
  return 1;
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

static void eq_int(long got, long want, const char *label)
{
  if (got != want) {
    printf("  FAIL: %s: got %ld, want %ld\n", label, got, want);
    failures++;
  }
}

/* A scan touches the bus a bounded number of times. The generous ceiling below
 * is not a tuning target - it is the line between "gives up promptly" and
 * "retries into a fault for minutes". Before #253, writecheck/readcheck each
 * retried 1000x, and every one of those attempts could itself spin
 * waiti2cmbb's 1,000,000 iterations with interrupts masked. */
#define I2C_CALL_CEILING 200

/* ---------------------------------------------------------------------------
 * Scenario 1: a bus that never answers.
 * ------------------------------------------------------------------------- */
static void scen_wedged(void)
{
  uint8_t uid[8];

  printf("scenario wedged: an unresponsive bus must fail fast\n");

  i2c_answers = 0;
  i2c_calls = 0;
  int8_t r = rfid_read_uid(uid);

  eq_int(r, -1, "a wedged bus reports an I2C error");
  CHECK(i2c_calls <= I2C_CALL_CEILING, "wedged bus retries stay bounded");
  if (i2c_calls > I2C_CALL_CEILING)
    printf("        (%u i2c calls, ceiling %u)\n", i2c_calls, I2C_CALL_CEILING);

  /* A second scan must be just as cheap - the driver must not have wedged
   * itself into a state where recovery costs more each time. */
  i2c_calls = 0;
  r = rfid_read_uid(uid);
  eq_int(r, -1, "a second scan on a wedged bus still reports the error");
  CHECK(i2c_calls <= I2C_CALL_CEILING, "the second wedged scan is bounded too");
}

/* ---------------------------------------------------------------------------
 * Scenario 2: an empty coupler on a healthy bus. Distinct from a wedge - the
 * CRX14 answers, it just reports no tag - and must stay cheap so the 750 ms
 * background scan cycle costs nothing when no tag is present.
 * ------------------------------------------------------------------------- */
static void scen_nack(void)
{
  uint8_t uid[8];

  printf("scenario nack: an empty coupler on a healthy bus stays cheap\n");

  i2c_answers = 1;
  frame_reply_len = 0;          /* all-zero reply = no tag detected */
  memset(frame_reply, 0, sizeof frame_reply);
  i2c_calls = 0;

  int8_t r = rfid_read_uid(uid);
  eq_int(r, 0, "an empty coupler reports no tag (not an error)");
  CHECK(i2c_calls <= I2C_CALL_CEILING, "an empty scan is bounded");
  CHECK(i2c_calls > 0, "an empty scan does still touch the bus");
}

/* ---------------------------------------------------------------------------
 * Scenario 3: the happy path still works through the retry wrapper - the
 * bound must not have been bought by breaking a real read.
 * ------------------------------------------------------------------------- */
static void scen_happy(void)
{
  uint8_t uid[8];

  printf("scenario happy: a present tag still reads back\n");

  i2c_answers = 1;
  /* Enough of a reply for check_rfid_devices to see one tag in slot 0 and
   * then read a UID: byte 0 non-zero (initiate answered), the slot bitmap in
   * bytes 1..2, chip ids from byte 3, and the UID little-endian at 1..8. */
  memset(frame_reply, 0, sizeof frame_reply);
  frame_reply[0] = 0x01;   /* initiate: something is there */
  frame_reply[1] = 0x01;   /* slot bitmap: slot 0 occupied */
  frame_reply[2] = 0x00;
  frame_reply[3] = 0x11;   /* chip id */
  for (uint8_t i = 1; i <= 8; i++)
    frame_reply[i] = (uint8_t)(0xA0 + i);
  frame_reply_len = 20;
  i2c_calls = 0;

  int8_t r = rfid_read_uid(uid);
  CHECK(r == 1, "a present tag is reported");
  CHECK(i2c_calls <= I2C_CALL_CEILING, "the happy path is bounded");
  if (r == 1) {
    /* rfid.c reverses the frame bytes into uid[]; assert the exact mapping so
     * a refactor of that loop cannot silently transpose a UID. */
    char got[17], want[17];
    static const uint8_t expect[8] = {0xA8, 0xA7, 0xA6, 0xA5,
                                      0xA4, 0xA3, 0xA2, 0xA1};
    for (int i = 0; i < 8; i++) {
      snprintf(got + i * 2, 3, "%02x", uid[i]);
      snprintf(want + i * 2, 3, "%02x", expect[i]);
    }
    if (strcmp(got, want) != 0) {
      printf("  FAIL: uid mapping: got %s, want %s\n", got, want);
      failures++;
    }
  }
}

int main(int argc, char **argv)
{
  const char *only = (argc > 1) ? argv[1] : NULL;

  if (!only || strcmp(only, "wedged") == 0) scen_wedged();
  if (!only || strcmp(only, "nack") == 0)   scen_nack();
  if (!only || strcmp(only, "happy") == 0)  scen_happy();

  if (failures) {
    printf("rfid_test: %d check(s) FAILED\n", failures);
    return 1;
  }
  printf("rfid_test: all checks passed\n");
  return 0;
}
