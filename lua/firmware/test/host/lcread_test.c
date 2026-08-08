/**
 * @file lcread_test.c
 * @brief Host-side tests for src/utils/lcread.c - the `#LC` frame reader (#328).
 *
 * The reader sits directly on the console: it pulls a hex payload off `_read`
 * one byte at a time and decides what to leave behind when it refuses a frame.
 * That second half is the part with a rule to it, and until #324 made `_read`
 * a named seam (`libc/syscalls.h`) and #328 lifted the reader out of `main.c`,
 * nothing could link it - which is exactly why its worst corner
 * (`LCFRAME_ERR_TOOLONG`) stayed open with a comment instead of a fix.
 *
 * The console here is a fake `_read` over a scripted buffer, the same
 * substitution `fmt_test.c` makes for `_write`. Every scenario therefore
 * asserts TWO things, and the second is the point:
 *
 *   1. what the reader returned - the exact decoded bytes, or the exact refusal;
 *   2. **where it left the console** - asserted as the exact remaining input,
 *      so "it refused the frame" cannot pass while the payload is still queued
 *      for the REPL to choke on line by line (#308).
 *
 * ASan is the other half of the point: the reader mallocs the chunk off the
 * heap and frees it on every error path, and each scenario below drives at
 * least one of them.
 *
 * Scenarios (argv[1] selects one; all run by default):
 *
 *   frame     a well-formed frame decodes to the exact expected bytes
 *   wrap      a payload wrapped at 64 columns round-trips
 *   truncated a short payload is refused, and the console resyncs
 *   nonhex    a non-hex byte mid-payload is refused, and the console resyncs
 *   header    a refused header still consumes the payload behind it (#308)
 *   toolong   the over-long frame: the corner #328 closed, pinned both ways
 *   checksum  a damaged frame is refused with its payload consumed
 */
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "utils/lcframe.h"
#include "utils/lcread.h"

/* --- the fake console ----------------------------------------------------- */

/* What the sender has queued behind the header line. The device's real _read
 * blocks for one byte and reports EOT as EOF; this one hands out one byte per
 * call from the script and reports end-of-script as EOF, which is what a
 * simulator run does too. */
static char   script[4096];
static size_t script_len;
static size_t script_pos;

int _read(int fd, char *ptr, int len)
{
  (void)fd;
  if (len <= 0 || script_pos >= script_len)
    return 0; /* EOF */
  ptr[0] = script[script_pos++];
  return 1;   /* one char per call, exactly like the UART console */
}

static void console(const char *text)
{
  script_len = strlen(text);
  if (script_len >= sizeof script) {
    printf("  FAIL: test script too long for the fake console\n");
    exit(2);
  }
  memcpy(script, text, script_len + 1);
  script_pos = 0;
}

/* What the REPL's line reader would see next. */
static const char *rest(void)
{
  return script + script_pos;
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

static void eq_str(const char *got, const char *want, const char *label)
{
  if (strcmp(got, want) != 0) {
    printf("  FAIL: %s: got \"%s\", want \"%s\"\n", label, got, want);
    failures++;
  }
}

/* The assertion this file exists for: not "the frame was refused" but "and the
 * console is where the next REPL line starts". */
static void eq_rest(const char *want, const char *label)
{
  eq_str(rest(), want, label);
}

static void eq_bytes(const char *got, const char *want, long n,
                     const char *label)
{
  long i;

  for (i = 0; i < n; i++) {
    if (got[i] != want[i]) {
      printf("  FAIL: %s: byte %ld is 0x%02x, want 0x%02x\n", label, i,
             (unsigned)(unsigned char)got[i], (unsigned)(unsigned char)want[i]);
      failures++;
      return;
    }
  }
}

/* --- a frame builder, matching tools/luac/replpipe.py's frame_bytes() ------ */

/* hex, wrapped every `cols` chars with '\n', each line terminated - exactly
 * what the sender puts on the wire behind the header. */
static void hexwrap(char *out, size_t outsz, const uint8_t *data, long n,
                    int cols)
{
  static const char digits[] = "0123456789abcdef";
  size_t o = 0;
  long i;
  int col = 0;

  for (i = 0; i < n; i++) {
    if (o + 4 >= outsz)
      break;
    out[o++] = digits[(data[i] >> 4) & 0xF];
    if (++col == cols) { out[o++] = '\n'; col = 0; }
    out[o++] = digits[data[i] & 0xF];
    if (++col == cols) { out[o++] = '\n'; col = 0; }
  }
  if (col != 0)
    out[o++] = '\n';
  out[o] = '\0';
}

/* --- scenarios ------------------------------------------------------------ */

/* The happy path, with a header pinned as a literal: "abcde" and its
 * Fletcher-32 are the same vector lcframe_test.c pins, so a drift in the
 * checksum shows up there and a drift in the READER shows up here. */
static void scen_frame(void)
{
  char *buf;
  long len;
  const char *msg;
  lcread_status st;

  printf("scenario frame: a well-formed frame decodes to its exact bytes\n");

  console("6162636465\n#LC:99:00000000\n");
  st = lcread_frame("#LC:5:05c301ef\n", &buf, &len, &msg);

  CHECK(st == LCREAD_OK, "a well-formed frame is accepted");
  CHECK(len == 5, "...with its declared length");
  eq_str(msg, "ok", "...and no complaint");
  if (buf != NULL) {
    eq_bytes(buf, "abcde", 5, "...decoded byte for byte");
    free(buf);
  } else {
    CHECK(0, "...and a buffer to show for it");
  }
  eq_rest("#LC:99:00000000\n", "the console is left at the next line");

  /* Upper-case hex is as valid on the wire as lower (the reader takes both);
   * the senders emit lower. */
  console("6162636465\n");
  st = lcread_frame("#LC:5:05C301EF\n", &buf, &len, &msg);
  CHECK(st == LCREAD_OK, "an upper-case checksum is accepted");
  if (buf != NULL) {
    eq_bytes(buf, "abcde", 5, "...and decodes the same");
    free(buf);
  }

  /* A 0-byte frame has NO payload line behind it (the sender wraps 0 hex
   * chars into 0 lines), so the reader must not reach for a trailing newline
   * that was never sent - it would eat the line after it. */
  console("print(1)\n");
  st = lcread_frame("#LC:0:ffffffff\n", &buf, &len, &msg);
  CHECK(st == LCREAD_OK, "an empty frame is well-formed");
  CHECK(len == 0, "...with no payload");
  CHECK(buf == NULL, "...and no buffer");
  eq_rest("print(1)\n", "...and the following line is untouched");
}

/* The wire format wraps at 64 columns, so a real chunk arrives as many lines
 * of hex whose newlines are framing, not data. */
static void scen_wrap(void)
{
  uint8_t want[174];   /* the size the bytecode-robustness subject chunk is */
  char hex[1024];
  char head[64];
  char wire[2048];
  char *buf;
  long len;
  const char *msg;
  lcread_status st;
  size_t i;
  int lines = 0;

  printf("scenario wrap: a payload wrapped at 64 columns round-trips\n");

  for (i = 0; i < sizeof want; i++)
    want[i] = (uint8_t)(i * 7 + 3);

  hexwrap(hex, sizeof hex, want, (long)sizeof want, 64);
  for (i = 0; hex[i]; i++)
    if (hex[i] == '\n')
      lines++;
  CHECK(lines == 6, "the fixture really is wrapped (174 B -> 348 hex -> 6 lines)");

  snprintf(head, sizeof head, "#LC:%u:%08x\n", (unsigned)sizeof want,
           (unsigned)lcframe_checksum(want, sizeof want));
  snprintf(wire, sizeof wire, "%s#LC:99:00000000\n", hex);

  console(wire);
  st = lcread_frame(head, &buf, &len, &msg);

  CHECK(st == LCREAD_OK, "a wrapped frame is accepted");
  CHECK(len == (long)sizeof want, "...with its full length");
  if (buf != NULL) {
    eq_bytes(buf, (const char *)want, (long)sizeof want,
             "...and every byte survives the wrapping");
    free(buf);
  } else {
    CHECK(0, "...and a buffer to show for it");
  }
  eq_rest("#LC:99:00000000\n", "the console is left at the next line");
}

/* A payload that stops early. The reader cannot know the frame was short until
 * it reads the byte that proves it - and there is no pushback on this console,
 * so that byte is gone. What must NOT happen is the rest of the next line
 * going with it. */
static void scen_truncated(void)
{
  char *buf;
  long len;
  const char *msg;
  lcread_status st;

  printf("scenario truncated: a short payload is refused, and resyncs\n");

  console("6162\n#LC:5:05c301ef\n");
  st = lcread_frame("#LC:5:05c301ef\n", &buf, &len, &msg);

  CHECK(st == LCREAD_ERR_PAYLOAD, "a truncated payload is refused");
  CHECK(buf == NULL, "...with nothing handed back");
  CHECK(len == 0, "...and no length");
  eq_str(msg, "truncated or non-hex #LC frame payload", "...and says why");
  /* The '#' that ended the drain is the price of having no pushback; the frame
   * behind it is intact, so the console resyncs within one line instead of
   * reading the whole payload as REPL input. */
  eq_rest("LC:5:05c301ef\n", "the console is one byte into the next line");

  /* EOF mid-payload must terminate, not spin. */
  console("61");
  st = lcread_frame("#LC:5:05c301ef\n", &buf, &len, &msg);
  CHECK(st == LCREAD_ERR_PAYLOAD, "a payload cut off by EOF is refused");
  eq_rest("", "...and the console is at EOF");
}

/* A byte that is neither hex nor framing whitespace: line noise on a UART with
 * no flow control, which is the failure this whole frame format exists for. */
static void scen_nonhex(void)
{
  char *buf;
  long len;
  const char *msg;
  lcread_status st;

  printf("scenario nonhex: a non-hex byte mid-payload is refused, and resyncs\n");

  console("6162z36465\n#LC:5:05c301ef\n");
  st = lcread_frame("#LC:5:05c301ef\n", &buf, &len, &msg);

  CHECK(st == LCREAD_ERR_PAYLOAD, "a non-hex payload byte is refused");
  CHECK(buf == NULL, "...with nothing handed back");
  eq_str(msg, "truncated or non-hex #LC frame payload", "...and says why");
  /* The reader stops AT the bad byte, having eaten it, and leaves the rest of
   * that line alone - the remaining hex is read as one REPL line, not as one
   * per wrapped line. */
  eq_rest("36465\n#LC:5:05c301ef\n", "the console stops at the offending byte");
}

/* #308, now at the reader level: rejecting a header is only half of rejecting
 * it. lcframe_test.c pins that the PARSE reports the queued payload; this pins
 * that the reader actually eats it. */
static void scen_header(void)
{
  char *buf;
  long len;
  const char *msg;
  lcread_status st;

  printf("scenario header: a refused header still consumes its payload\n");

  /* Exactly what tools/simui emitted: a valid frame in the pre-#298 format. */
  console("6162636465\nprint(1)\n");
  st = lcread_frame("#LC:5\n", &buf, &len, &msg);
  CHECK(st == LCREAD_ERR_HEADER, "a checksum-less header is refused");
  CHECK(buf == NULL, "...with nothing handed back");
  eq_str(msg, "#LC frame header carries no checksum", "...and says why");
  eq_rest("print(1)\n", "...and the payload is gone, not left for the REPL");

  console("6162636465\nprint(1)\n");
  st = lcread_frame("#LC:5:zzzzzzzz\n", &buf, &len, &msg);
  CHECK(st == LCREAD_ERR_HEADER, "a malformed checksum is refused");
  eq_str(msg, "malformed #LC frame checksum", "...and says why");
  eq_rest("print(1)\n", "...and its payload is consumed too");

  /* No parseable length means no sender: this was typed at the prompt, and
   * there is nothing queued behind it. Consuming here would eat the user's
   * next line - including the correction they are about to type. */
  console("print(1)\n");
  st = lcread_frame("#LC:oops\n", &buf, &len, &msg);
  CHECK(st == LCREAD_ERR_HEADER, "a header with no length is refused");
  eq_str(msg, "malformed #LC frame header", "...and says why");
  eq_rest("print(1)\n", "...and eats NOTHING, so the next line survives");
}

/* The corner #328 exists for.
 *
 * lcframe_parse_header reports *len == 0 for a length over the cap on purpose,
 * so a header claiming 4 GB cannot make the device read 8 GB of hex. main.c
 * used to take that as "drop one line", which left the REST of the frame's hex
 * to be read as REPL input - one "bytecode-only build" reply per wrapped line,
 * the console desync in its last corner. Its comment said the fix was to drain
 * to the first non-hex byte and that it was written down rather than done
 * "because it is a loop over attacker-paced input in main.c, where nothing can
 * link it to test it". This is that test.
 *
 * The drain never trusts the declared length: it reads what the sender
 * actually sent and stops on content. The one byte it cannot give back is the
 * same no-pushback cost as `truncated` above. */
static void scen_toolong(void)
{
  uint8_t junk[200];
  char hex[1024];
  char wire[2048];
  char *buf;
  long len;
  const char *msg;
  lcread_status st;
  size_t i;

  printf("scenario toolong: an over-long frame drains by content, not by length\n");

  for (i = 0; i < sizeof junk; i++)
    junk[i] = (uint8_t)(i * 3 + 1);
  hexwrap(hex, sizeof hex, junk, (long)sizeof junk, 64);
  snprintf(wire, sizeof wire, "%s#LC:5:05c301ef\n", hex);

  console(wire);
  st = lcread_frame("#LC:99999999:05c301ef\n", &buf, &len, &msg);

  CHECK(st == LCREAD_ERR_HEADER, "an over-long frame is refused");
  CHECK(buf == NULL, "...with nothing handed back");
  CHECK(len == 0, "...and no length");
  eq_str(msg, "#LC frame too large", "...and says why");
  /* Every one of the 400 hex chars is gone - that is the fix. The '#' is the
   * accepted cost; a later `_read` with one byte of pushback would make this
   * "#LC:5:05c301ef\n" and this assertion is the one it flips. */
  eq_rest("LC:5:05c301ef\n", "the whole payload is drained, at a cost of one byte");

  /* The drain must not need a terminator to stop: a sender that dies mid-frame
   * leaves EOF, and a loop over attacker-paced input that spins on EOF would be
   * a worse bug than the desync it replaces. */
  console(hex);
  st = lcread_frame("#LC:99999999:05c301ef\n", &buf, &len, &msg);
  CHECK(st == LCREAD_ERR_HEADER, "a drain that hits EOF still returns");
  eq_rest("", "...having consumed the lot");
}

/* The transport guard doing its job: the payload is read (so the console stays
 * in sync) and only then checked (so the loader never sees it). */
static void scen_checksum(void)
{
  char *buf;
  long len;
  const char *msg;
  lcread_status st;

  printf("scenario checksum: a damaged frame is refused with its payload eaten\n");

  /* "abcde" with one nibble flipped: 0x61 -> 0x71. */
  console("7162636465\nprint(1)\n");
  st = lcread_frame("#LC:5:05c301ef\n", &buf, &len, &msg);

  CHECK(st == LCREAD_ERR_CHECKSUM, "a frame that fails its checksum is refused");
  CHECK(buf == NULL, "...and is NOT handed to the loader");
  CHECK(len == 0, "...with no length");
  eq_str(msg, "#LC frame checksum mismatch - frame corrupted in transit",
         "...and says why");
  eq_rest("print(1)\n", "...and the console is left at the next line");

  /* Guard against a vacuous pass: the same payload with the RIGHT checksum
   * must be accepted, or the scenario above would pass on any bug that
   * rejects everything. */
  console("7162636465\nprint(1)\n");
  st = lcread_frame("#LC:5:061301ff\n", &buf, &len, &msg);
  CHECK(st == LCREAD_OK, "the same payload with its own checksum is accepted");
  if (buf != NULL) {
    eq_bytes(buf, "\x71" "bcde", 5, "...and decodes to the damaged bytes");
    free(buf);
  }
}

int main(int argc, char **argv)
{
  const char *only = (argc > 1) ? argv[1] : NULL;
  int ran = 0;

  if (!only || strcmp(only, "frame") == 0)     { scen_frame();     ran++; }
  if (!only || strcmp(only, "wrap") == 0)      { scen_wrap();      ran++; }
  if (!only || strcmp(only, "truncated") == 0) { scen_truncated(); ran++; }
  if (!only || strcmp(only, "nonhex") == 0)    { scen_nonhex();    ran++; }
  if (!only || strcmp(only, "header") == 0)    { scen_header();    ran++; }
  if (!only || strcmp(only, "toolong") == 0)   { scen_toolong();   ran++; }
  if (!only || strcmp(only, "checksum") == 0)  { scen_checksum();  ran++; }

  /* A selector that matches nothing must FAIL, not report a green run having
   * tested nothing - the repo rule, and the trap the SCENARIO= plumbing walked
   * into once already (see test/host/README.md). */
  if (ran == 0) {
    printf("lcread_test: no scenario matches \"%s\"\n", only);
    return 2;
  }

  if (failures) {
    printf("lcread_test: %d check(s) FAILED\n", failures);
    return 1;
  }
  printf("lcread_test: all checks passed\n");
  return 0;
}
