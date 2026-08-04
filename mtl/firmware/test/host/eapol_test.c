/**
 * @file eapol_test.c
 * @brief Host-side guard for the mtl WPA 4-way handshake's frame-length handling.
 *
 * The mtl copy of `src/net/eapol.c` is the twin of the lua one and still
 * carried the pre-authentication over-read #292 fixed there (#307). This file
 * is the lua track's `test/host/eapol_test.c` retargeted at the mtl source,
 * with one extra scenario: mtl's group-key handler still carries the WPA1/TKIP
 * branches #124 removed from lua, so the group path is exercised in its own
 * right rather than assumed identical.
 *
 * `eapol_input` is fed straight from the radio, so every byte it reads is an
 * attacker's until the MIC has been verified. It performs ONE length check, on
 * the base structure:
 *
 *     length < sizeof(struct eapol_frame) - EAPOL_RSN_LENGTH
 *
 * and nothing after that re-derives a bound from it. Two fields the frame
 * carries then drive reads:
 *
 *   * `body_length` (u16, off the wire) is the length passed to hmac_sha1 (or
 *     hmac_md5, on the TKIP branch mtl keeps) when the MIC is COMPUTED - and
 *     computing it is what the comparison against the received MIC is for, so
 *     this read happens BEFORE anything is authenticated. Both handlers carried
 *     `(void)length;` and the comment "FIXME Check length before cast ?". A
 *     frame declaring 0xFFFF makes the HMAC read ~64 KB past the received frame.
 *
 *   * `key_data_length` (u16, off the wire) is checked against the size of the
 *     GTK unwrap's scratch buffer (good) but never against the frame:
 *     `key_data` is a 24-byte tail of the struct, and up to 112 bytes are read
 *     from it into aes128_unwrap. A real msg3 carries 56-88 bytes there, so the
 *     frame is genuinely longer than the struct - which is exactly why the
 *     received length is the only thing that can bound this, and it is unused.
 *
 * On this part there is no MMU and COMRAM is a small pool shared with the USB
 * transfer descriptors, so a 64 KB read walks straight off it.
 *
 * eapol.c compiles host-native as-is (23 externals, all board/driver entry
 * points), so the REAL handshake runs here against an exactly-sized heap frame
 * - as it arrives on the device - and ASan reports the read precisely.
 *
 * Scenarios (argv[1] selects one; all run by default):
 *
 *   msg1        a well-formed message 1/4 is answered (the non-vacuous half)
 *   msg3-mic    message 3/4's body_length must be bounded by the frame
 *   gtk-kd      the group message's key_data_length, likewise
 *   group-tkip  the WPA1/TKIP branch's RC4 GTK read, likewise (mtl only)
 *   group-mic   the group-key message's body_length, likewise
 *
 * The guards drive a real msg1 first, because that is how a supplicant reaches
 * the state where msg3 is accepted. They differ in what they need from the
 * attacker, and that difference is the severity:
 *
 *   msg3-mic is PRE-AUTHENTICATION. The over-read IS the MIC computation, so it
 *     happens before the comparison that would reject a forged frame. Sending
 *     msg1 and msg3 needs no key at all - only radio range and a rabbit that is
 *     mid-join.
 *   gtk-kd sits AFTER the MIC comparison, so it needs a peer whose MIC verifies
 *     - the AP we share the PSK with. That is a robustness bug against a
 *     misbehaving or compromised AP and against a frame corrupted in flight,
 *     not an unauthenticated one. The stubbed hmac_sha1 below returns a fixed
 *     value that the fixture's zeroed key_mic matches, which is how the
 *     scenario models an authenticated peer - it is not claiming the MIC can
 *     be bypassed.
 *   group-mic is pre-authentication in the same sense as msg3-mic, on the
 *     handler that also runs at every GTK rekey.
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "common.h"
#include "usb/rt2501usb.h"
#include "net/ieee80211.h"
#include "net/eapol.h"
#include "net/rc4.h"

/* stubs/ml60842.h's fake OHCI window. */
volatile uint32_t nab_usb_regs[4];

/* --- the board + driver layer eapol.c expects ----------------------------- */

uint8_t rt2501_mac[IEEE80211_ADDR_LEN] = {2, 0, 0, 0, 0, 1};
uint8_t ieee80211_assoc_mac[IEEE80211_ADDR_LEN] = {2, 0, 0, 0, 0, 2};
uint8_t ieee80211_assoc_ssid[IEEE80211_SSID_MAXLEN + 1] = "nabtest";
uint8_t ieee80211_key[64];
int32_t  ieee80211_state;
uint8_t  ieee80211_encryption;
uint32_t ieee80211_timeout;
char dbg_buffer[DBG_BUFFER_LENGTH];   /* mtl's debug.h keeps sprintf real */

static unsigned sent_frames;
static uint32_t hmac_len;      /* the length the code asked the HMAC to read */

void *hcd_malloc(uint32_t size, uint8_t type, uint8_t tag)
{ (void)type; (void)tag; return malloc(size); }
void hcd_free(void *p) { free(p); }
void dump(uint8_t *d, int32_t n) { (void)d; (void)n; }
void usbhost_events(void) {}
struct rt2501buffer *rt2501_receive(void) { return NULL; }
int32_t rt2501_set_key(uint8_t i, uint8_t *k, uint8_t *tx, uint8_t *rx, uint8_t c)
{ (void)i; (void)k; (void)tx; (void)rx; (void)c; return 1; }
void set_led(uint8_t l, uint32_t c) { (void)l; (void)c; }

int32_t rt2501_send(const uint8_t *data, uint32_t len, const uint8_t *dst,
                    int32_t lowrate, int32_t mayblock)
{
  (void)data; (void)len; (void)dst; (void)lowrate; (void)mayblock;
  sent_frames++;
  return 1;
}

/* An HMAC must read every one of the `len` bytes it is given - that is what a
 * MAC over a message is. Reading them here is not the test inventing an access:
 * it is the minimum any implementation performs, and the LENGTH comes from the
 * code under test. Recorded so the scenarios can report what was asked for.
 * Both hashes are stubbed the same way: mtl picks md5 on its TKIP branch, and
 * the bound has to hold on either. */
static void hash_read(const uint8_t *data, uint32_t data_len, uint8_t *out,
                      uint32_t out_len)
{
  volatile uint8_t sink = 0;
  hmac_len = data_len;
  for (uint32_t i = 0; i < data_len; i++)
    sink ^= data[i];
  memset(out, 0, out_len);   /* see the MIC note on scen_gtk_kd */
}

void hmac_sha1(const uint8_t *key, uint32_t key_len, const uint8_t *data,
               uint32_t data_len, uint8_t *out)
{ (void)key; (void)key_len; hash_read(data, data_len, out, 20); }

void hmac_md5(const uint8_t *key, uint32_t key_len, const uint8_t *data,
              uint32_t data_len, uint8_t *out)
{ (void)key; (void)key_len; hash_read(data, data_len, out, 16); }

/* Likewise: unwrapping N bytes means reading N bytes. */
int aes128_unwrap(const uint8_t *kek, const uint8_t *in, uint16_t len,
                  uint8_t *out)
{
  volatile uint8_t sink = 0;
  (void)kek;
  for (uint16_t i = 0; i < len; i++)
    sink ^= in[i];
  memset(out, 0, len > 8 ? (size_t)(len - 8) : 0);
  return 0;   /* integrity fail: we are not testing the unwrap itself */
}

/* The RC4 group-key path mtl still carries (#124 removed it from lua). Not
 * under test here, but eapol.c links against it. */
void rc4_init(struct rc4_context *rc4, const unsigned char *key,
              unsigned int length)
{ (void)key; (void)length; memset(rc4, 0, sizeof *rc4); }
unsigned char rc4_byte(struct rc4_context *rc4) { (void)rc4; return 0; }
void rc4_cipher(struct rc4_context *rc4, unsigned char *out,
                const unsigned char *in, unsigned int length)
{ (void)rc4; memcpy(out, in, length); }

/* --- assert harness ------------------------------------------------------- */

static int failures;

#define CHECK(cond, msg)                                                      \
  do {                                                                        \
    if (!(cond)) {                                                            \
      printf("  FAIL: %s\n", (msg));                                          \
      failures++;                                                             \
    }                                                                         \
  } while (0)

/* --- frame construction --------------------------------------------------- */
/* Built in a static template, then handed to eapol_input in an exactly-sized
 * heap copy: on the device the frame sits in its own buffer, so a parser that
 * reads past the received length reads into the next allocation. A generous
 * static buffer would hide exactly that. */

static uint8_t tmpl[4096];
static uint8_t last_nonce[EAPOL_NONCE_LENGTH];

#define BASE_LEN (sizeof(struct eapol_frame) - EAPOL_RSN_LENGTH)

static struct eapol_frame *frame_init(void)
{
  struct eapol_frame *f = (struct eapol_frame *)tmpl;
  memset(tmpl, 0, sizeof tmpl);
  f->protocol_version = EAPOL_VERSION;
  f->packet_type = EAPOL_TYPE_KEY;
  f->key_frame.descriptor_type = EAPOL_DTYPE_WPA2KEY;
  f->key_frame.key_info.key_desc_ver = 2;   /* CCMP */
  f->key_frame.key_info.key_type = 1;
  f->key_frame.key_info.key_ack = 1;
  return f;
}

static void feed(uint32_t len)
{
  uint8_t *exact = malloc(len);
  memcpy(exact, tmpl, len);
  eapol_input(exact, len);
  free(exact);
}

/* The truthful body_length for a frame of `len` bytes. hmac_sha1 is handed
 * (frame + LLC_LENGTH, body_length + 4), so an honest frame satisfies
 * len == LLC_LENGTH + 4 + body_length. Every fixture below sets this except
 * the scenarios whose whole point is that nothing checks it. */
static void set_body_length(struct eapol_frame *f, uint32_t len)
{
  uint16_t bl = (uint16_t)(len - LLC_LENGTH - 4);
  f->body_length[0] = (uint8_t)(bl >> 8);
  f->body_length[1] = (uint8_t)bl;
}

/* Monotonic replay counter, so successive frames are not dropped as replays. */
static void set_replay(struct eapol_frame *f, uint8_t n)
{
  memset(f->key_frame.replay_counter, 0, EAPOL_RPC_LENGTH);
  f->key_frame.replay_counter[EAPOL_RPC_LENGTH - 1] = n;
}

/* Drive a legitimate message 1/4, leaving the supplicant expecting msg3. */
static void do_msg1(uint8_t rc)
{
  struct eapol_frame *f = frame_init();
  ieee80211_encryption = IEEE80211_CRYPT_WPA2 | IEEE80211_CIPHER_CCMP;
  set_replay(f, rc);
  f->key_frame.key_info.key_mic = 0;          /* msg1: no MIC yet */
  memset(last_nonce, 0xA5, sizeof last_nonce);
  memcpy(f->key_frame.key_nonce, last_nonce, EAPOL_NONCE_LENGTH);
  set_body_length(f, (uint32_t)BASE_LEN);
  sent_frames = 0;
  feed((uint32_t)BASE_LEN);
}

/* Drive a legitimate message 3/4, leaving the supplicant awaiting the group
 * key. The stubbed HMAC returns zeros, which the fixture's zeroed key_mic
 * matches - the authenticated-peer model described in the header comment. */
static void do_msg3(uint8_t rc)
{
  struct eapol_frame *f = frame_init();
  set_replay(f, rc);
  f->key_frame.key_info.key_mic = 1;
  memcpy(f->key_frame.key_nonce, last_nonce, EAPOL_NONCE_LENGTH);
  set_body_length(f, (uint32_t)BASE_LEN);
  feed((uint32_t)BASE_LEN);
}

/* --- msg1: the handshake really runs here --------------------------------- */

static void scen_msg1(void)
{
  printf("scenario msg1: a well-formed message 1/4 is answered\n");

  do_msg1(1);

  /* Proves eapol_input accepts and dispatches, so an ASan report in the
   * guards below is the handler's and not an early drop somewhere. */
  CHECK(sent_frames == 1, "message 1/4 is answered with message 2/4");
}

/* --- msg3: body_length drives the MIC read -------------------------------- */

static void scen_msg3_mic(void)
{
  struct eapol_frame *f;

  printf("scenario msg3-mic: body_length must be bounded by the frame\n");

  do_msg1(1);

  f = frame_init();
  set_replay(f, 2);
  f->key_frame.key_info.key_mic = 1;          /* msg3 */
  memcpy(f->key_frame.key_nonce, last_nonce, EAPOL_NONCE_LENGTH);
  /* The only hostile byte pair in the frame. A real msg3 sets this to its own
   * body length; nothing checks that it does. */
  f->body_length[0] = 0xFF;
  f->body_length[1] = 0xFF;
  hmac_len = 0;
  feed((uint32_t)BASE_LEN);

  CHECK(hmac_len <= BASE_LEN,
        "the MIC is computed over the received frame, not over what it claims");
}

/* --- msg3: key_data_length drives the unwrap read ------------------------- */

static void scen_gtk_kd(void)
{
  struct eapol_frame *f;

  printf("scenario gtk-kd: key_data_length must be bounded by the frame\n");

  do_msg1(1);
  do_msg3(2);   /* mtl installs the GTK from the GROUP message, so get there */

  f = frame_init();
  set_replay(f, 3);
  f->key_frame.key_info.key_type = 0;         /* group-key message */
  f->key_frame.key_info.key_mic = 1;
  f->key_frame.key_info.secure = 1;
  set_body_length(f, (uint32_t)BASE_LEN);   /* honest: isolates the unwrap read */
  /* 56 is the largest the group handler accepts (sizeof(unwrapped)+8), and the
   * check that caps it is against the SCRATCH BUFFER, never against the frame.
   * This frame carries no key_data at all - a real one carries 56-88 bytes
   * there, which is precisely why the received length is the only thing that
   * could bound the read, and it is unused. */
  f->key_frame.key_data_length[0] = 0;
  f->key_frame.key_data_length[1] = 56;
  feed((uint32_t)BASE_LEN);

  /* Reaching here is the assertion: ASan reports the over-read otherwise. */
  printf("  unwrap returned without reading past the frame\n");
}

/* --- group message, TKIP branch: key_data may not be there at all --------- */

static void scen_group_tkip(void)
{
  struct eapol_frame *f;

  printf("scenario group-tkip: the RC4 GTK read must be bounded by the frame\n");

  /* The WPA1/TKIP group-key branch #124 removed from the lua twin, so this one
   * is mtl's alone. It rc4_ciphers 32 bytes straight out of key_data - the
   * OPTIONAL 24-byte tail of struct eapol_frame - with nothing checking that
   * the frame reached that far. eapol_input only guarantees the frame reaches
   * the START of key_data. */
  ieee80211_encryption = IEEE80211_CRYPT_WPA | IEEE80211_CIPHER_TKIP;
  do_msg1(1);
  ieee80211_encryption = IEEE80211_CRYPT_WPA | IEEE80211_CIPHER_TKIP;
  do_msg3(2);

  f = frame_init();
  f->key_frame.descriptor_type = EAPOL_DTYPE_WPAKEY;
  f->key_frame.key_info.key_desc_ver = 1;     /* TKIP */
  set_replay(f, 3);
  f->key_frame.key_info.key_type = 0;
  f->key_frame.key_info.key_mic = 1;
  f->key_frame.key_info.secure = 1;
  f->key_frame.key_info.key_index = 1;        /* the install guard on this path */
  set_body_length(f, (uint32_t)BASE_LEN);     /* honest: isolates the GTK read */
  feed((uint32_t)BASE_LEN);

  printf("  the RC4 branch returned without reading past the frame\n");
}

/* --- group message: body_length drives that MIC read too ------------------ */

static void scen_group_mic(void)
{
  struct eapol_frame *f;

  printf("scenario group-mic: the group message's body_length must be bounded\n");

  do_msg1(1);
  do_msg3(2);

  f = frame_init();
  set_replay(f, 3);
  f->key_frame.key_info.key_type = 0;         /* group-key message */
  f->key_frame.key_info.key_mic = 1;
  f->key_frame.key_info.secure = 1;
  f->body_length[0] = 0xFF;
  f->body_length[1] = 0xFF;
  hmac_len = 0;
  feed((uint32_t)BASE_LEN);

  CHECK(hmac_len <= BASE_LEN,
        "the group MIC is computed over the received frame, not what it claims");
}

int main(int argc, char **argv)
{
  const char *only = (argc > 1) ? argv[1] : NULL;
  int matched = 0;

#define RUN(name, fn)                                                         \
  do {                                                                        \
    if (!only || strcmp(only, (name)) == 0) {                                 \
      matched = 1; eapol_init(); fn();                                        \
    }                                                                         \
  } while (0)

  RUN("msg1",      scen_msg1);
  RUN("msg3-mic",  scen_msg3_mic);
  RUN("gtk-kd",     scen_gtk_kd);
  RUN("group-tkip", scen_group_tkip);
  RUN("group-mic",  scen_group_mic);

  /* An unmatched selector must FAIL, not report a pass having run nothing -
   * the vacuous-pass rule applies to the runner as much as to the test. */
  if (!matched) {
    printf("eapol_test: no such scenario \"%s\"\n", only);
    return 2;
  }
  if (failures) {
    printf("eapol_test: %d check(s) FAILED\n", failures);
    return 1;
  }
  printf("eapol_test: all checks passed\n");
  return 0;
}
