/**
 * @file main.c
 * @brief Boot PUC-Rio Lua 5.4 and run a REPL - the first real language runtime
 *        on the ML67Q4051. Opens a trimmed stdlib, runs an embedded demo chunk,
 *        then drops into a REPL. Console = UART0 (#207); heap = 1 MB ExtRAM.
 *
 * Bare metal supplies neither, so this file also overrides the newlib syscalls
 * (_read/_write over the polled UART, _sbrk into ExtRAM) and Lua's number/printf
 * helpers to keep the buffered-FILE + libm layers out of the flash budget.
 */
#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>   /* malloc/free - #LC bytecode frame buffer */
#include <string.h>   /* memcpy - WAV header assembly */

#include "lua.h"
#include "lauxlib.h"
#include "lualib.h"

/* Hardware bindings: LED driver + head button. */
#include "ml674061.h"
#include "common.h"
#include "hal/spi.h"
#include "hal/led.h"
#include "hal/button.h"
#include "hal/audio.h"
#include "hal/adc.h"
#include "hal/i2c.h"
#include "hal/rfid.h"
#include "hal/motor.h"   /* ear motors + encoders */
#include "hal/uart.h"    /* console: polled UART0 TX/RX (#207) */
#include "event.h"       /* cooperative event core (#195): queue + pollers */
#include "utils/delay.h" /* 1 ms tick: init_tick, counter_timer, DelayMs */
#include "utils/fmt.h"   /* fmt_hex8; printf/number shims are in fmt.c */
#include "hal/wifi.h"    /* USB RT2501 802.11 join - nab.wifi() */
#include "hal/config.h"  /* internal-flash config sector - nab.config() */
#include "hal/ota.h"      /* whole-image OTA flash writer - nab.flash_firmware() */
#include "irq.h"         /* init_irq: interrupt controller + tick (wifi needs it) */

#include "tone_mp3.h"   /* nab_tone_mp3[]: built-in MP3 tone for nab.tone() */

/* ---- UART console (#207) ------------------------------------------------- */
/* The REPL console is UART0 (hal/uart.c): polled TX + polled RX, 115200 8N1.
 * init_uart() runs at boot (main); read/drive it on the Pi's /dev/serial0.
 *
 * EOF: getch_uart() is non-blocking (-1 = RX FIFO empty) and a raw UART has no
 * native end-of-stream, so _read() blocks until a byte arrives and treats EOT
 * (0x04, what Ctrl-D sends) as EOF - EOF is what ends the REPL loop and fires
 * <<FV_DONE>>. The host feeder (replpipe/flash.py/simulator) appends EOT after
 * the input it sends; hex #LC frames and source lines never contain 0x04. */
#define CONSOLE_EOF 0x04   /* EOT / Ctrl-D: end of console input */

/* Raw console write, independent of stdio buffering - used for prompts/errors. */
static void sh_puts(const char *s)
{
  while (*s)
    putch_uart((uint8_t)*s++);
}

/* ---- newlib syscalls: stdout/stdin over UART, heap in ExtRAM ------------- */
/* Our own definitions win over libnosys' stubs (object beats archive member). */

int _write(int fd, const char *ptr, int len)
{
  (void)fd;
  for (int i = 0; i < len; i++)
    putch_uart((uint8_t)ptr[i]);
  return len;
}

/* Tick timestamp of the last console RX byte: the REPL's idle pump gates the
 * ~5 ms RFID coupler scan on the console having been quiet a while, so a scan
 * can't stall RX mid-transfer and overflow the 16-byte FIFO (see repl). */
static uint32_t console_last_ms;

int _read(int fd, char *ptr, int len)
{
  (void)fd;
  if (len <= 0)
    return 0;
  int c;
  while ((c = getch_uart()) < 0)
    ;                      /* block: no byte yet (UART has no native EOF) */
  console_last_ms = counter_timer;
  if (c == CONSOLE_EOF)
    return 0;              /* EOT -> EOF, ends the REPL */
  ptr[0] = (char)c;
  return 1;               /* one char per call; sh_gets reassembles the line */
}

/* Read one line into buf (keeps the trailing '\n', NUL-terminates), built on
 * the single-char _read syscall. Replaces fgets() so the REPL needs no newlib
 * stdio FILE layer. Returns NULL on immediate EOF, like fgets. */
static char *sh_gets(char *buf, int size)
{
  int i = 0;
  for (;;) {
    char c;
    if (_read(0, &c, 1) != 1) {   /* EOF / no input (simulator) */
      if (i == 0)
        return NULL;
      break;
    }
    if (i < size - 1)
      buf[i++] = c;
    if (c == '\n')
      break;
  }
  buf[i] = '\0';
  return buf;
}

/* ---- abort: halt, don't raise(SIGABRT) ----------------------------------- */
/* newlib's abort() calls raise() + _exit(), pulling the signal machinery
 * (raise/signal/_kill_r/_getpid). Bare metal has no OS to signal, so halt in
 * place. */
void abort(void)
{
  for (;;) {
  }
}

/* Heap = the 1 MB external RAM window; IntRAM is too small for a Lua state. */
extern char __extram_start__, __extram_end__;

void *_sbrk(ptrdiff_t incr)
{
  static char *cur;
  if (cur == NULL)
    cur = &__extram_start__;
  if (cur + incr > &__extram_end__) {
    errno = ENOMEM;
    return (void *)-1;
  }
  char *prev = cur;
  cur += incr;
  return prev;
}

/* ---- hardware bindings: the `nab` module --------------------------------- */
/* Exposes the LEDs, head button, audio (speaker + microphone), RFID coupler,
 * and ear motors to Lua. */

/* The five LEDs, by the name Lua uses. Each hardware entry point wants the LED
 * under a different index, so one row of these parallel tables carries both:
 * `sel` is set_led_rgb's raw channel number (LED_RGB_n >> 24) and `logical` is
 * what set_led/led_fade take - the inverse of led.c's convled[] remap, so all
 * three bindings land on the same physical LED (map verified on LLC2_4c, see
 * inc/hal/led.h). luaL_checkoption does the name lookup for all of them. */
static const char *const led_names[] = {"nose",  "belly", "left",
                                        "right", "bottom", NULL};
static const uint8_t led_sel[]     = {5, 1, 3, 4, 2};
static const uint8_t led_logical[] = {0, 2, 1, 3, 4};

/* Args 2,3,4 -> one 0xRRGGBB word, each channel checked against `max`
 * (127 for the raw TLC5922 range, 255 for the gamma bindings). */
static uint32_t check_rgb(lua_State *L, lua_Integer max)
{
  uint32_t rgb = 0;
  for (int i = 2; i <= 4; i++) {
    lua_Integer v = luaL_checkinteger(L, i);
    luaL_argcheck(L, v >= 0 && v <= max, i, max == 127 ? "0..127" : "0..255");
    rgb = (rgb << 8) | (uint32_t)v;
  }
  return rgb;
}

/* Block ~ms on the 1 ms System Timer tick (counter_timer), feeding the
 * watchdog - the clock nab.delay and nab.beep both pace themselves off. */
static void wait_ms(uint32_t ms)
{
  uint32_t t = counter_timer;
  while ((counter_timer - t) < ms)
    CLR_WDT;
}

/* nab.led(name, r, g, b): light an RGB LED. name is one of
 * nose|belly|left|right|bottom (physical map verified on hardware, LLC2_4c -
 * see inc/hal/led.h). r/g/b are 7-bit intensities (0..127), the TLC5922 range. */
static int nab_led(lua_State *L)
{
  int i = luaL_checkoption(L, 1, NULL, led_names);
  uint32_t rgb = check_rgb(L, 127);

  set_led_rgb(((uint32_t)led_sel[i] << 24) | rgb);
  return 0;
}

/* nab.led8(name, r, g, b): like nab.led but r/g/b are 8-bit (0..255) and pass
 * through the gamma-2.2 table (#102 / #45) - so a value of 1 still lights (the
 * old table's 0..51 dead zone is gone), giving smooth low-end fades. Instant. */
static int nab_led8(lua_State *L)
{
  int i = luaL_checkoption(L, 1, NULL, led_names);
  uint32_t rgb = check_rgb(L, 255);

  set_led(led_logical[i], rgb);
  return 0;
}

/* nab.fade(name, r, g, b, ms): fade an LED from its current colour to r/g/b
 * (8-bit, gamma) over ms, in the BACKGROUND - returns immediately. The 1 ms
 * System Timer IRQ (#102) interpolates and reflushes the LED bus; start fades on
 * all five and they run at once. A nab.led/nab.led8/nab.fade on the same LED
 * replaces its fade. ms=0 sets the colour instantly. The fade only advances
 * while interrupts run; the simulator models the timer and delivers its IRQ
 * (#102), so fades animate there too. */
static int nab_fade(lua_State *L)
{
  int i = luaL_checkoption(L, 1, NULL, led_names);
  uint32_t rgb = check_rgb(L, 255);
  lua_Integer ms = luaL_checkinteger(L, 5);
  luaL_argcheck(L, ms >= 0 && ms <= 60000, 5, "0..60000");

  led_fade(led_logical[i], rgb, (uint32_t)ms);
  return 0;
}

/* nab.delay is nab.wait (see below). Until #283 it was a bare spin that fed
 * only the watchdog, so every delay was a hole in which no event was even
 * sampled, no ear stopped on its target and no connection was pumped - a press
 * and release inside one was lost for good rather than delivered late. That a
 * script's frame pacing silently disabled its own callbacks was the bug, not a
 * feature worth keeping a second primitive for, so the two collapsed into one
 * and `delay` is now an alias. Both still time off the 1 ms System Timer
 * (counter_timer, #102), the same clock the background fades use, so animation
 * pacing and fades stay in step.
 */

/* nab.button() -> boolean: true while the head button is held (polled). */
static int nab_button(lua_State *L)
{
  lua_pushboolean(L, button_pressed());
  return 1;
}

/* nab.volume(v): output volume, 0 = loudest .. 254 = quietest (VS1003). */
static int nab_volume(lua_State *L)
{
  lua_Integer v = luaL_checkinteger(L, 1);
  luaL_argcheck(L, v >= 0 && v <= 254, 1, "0..254");
  set_vlsi_volume((uint8_t)v);
  return 0;
}

/* nab.beep([freq [, ms]]): play the VS1003 built-in sine test. freq is the
 * VS10xx sine-skip byte (pitch, 0..255, default 0x44); ms is the duration,
 * timed off the 1 ms System Timer like nab.delay (default 300). Blocking: the
 * tone plays on the codec while the CPU waits out the tick. */
static int nab_beep(lua_State *L)
{
  lua_Integer freq = luaL_optinteger(L, 1, 0x44);
  lua_Integer ms = luaL_optinteger(L, 2, 300);
  luaL_argcheck(L, freq >= 0 && freq <= 255, 1, "0..255");
  luaL_argcheck(L, ms >= 0 && ms <= 10000, 2, "0..10000");

  vlsi_ampli(1);                   /* plays at the current nab.volume setting */
  vlsi_sine((uint8_t)freq, 1);
  /* Timed off the 1 ms System Timer, like nab.delay (#247). This was a
   * calibrated-by-guess spin whose comment still claimed "no timer yet" - the
   * tick has existed since #102 - so nab.beep's ms argument was simply wrong
   * whenever the core clock differed from whatever the spin was tuned against. */
  wait_ms((uint32_t)ms);
  vlsi_sine((uint8_t)freq, 0);
  vlsi_ampli(0);
  return 0;
}

/* nab.play(data): stream a byte buffer (e.g. an MP3 file's bytes) to the
 * VS1003 decoder over SDI. Unlike nab.beep this is real decoded audio and
 * nab.volume actually attenuates it. Blocking: returns once the buffer is fed
 * and flushed. */
static int nab_play(lua_State *L)
{
  size_t len;
  const char *data = luaL_checklstring(L, 1, &len);
  vlsi_play((const uint8_t *)data, (uint32_t)len);
  return 0;
}

/* nab.tone(): a small built-in MP3 tone (nab_tone_mp3, see tone_mp3.h) so
 * nab.play + nab.volume are demoable without shipping a file. It is MP3, not
 * raw PCM WAV: the VS1003B on this board decodes MP3 but NOT PCM WAV
 * (hardware-verified). Feed to nab.play. */
static int nab_tone(lua_State *L)
{
  lua_pushlstring(L, (const char *)nab_tone_mp3, sizeof nab_tone_mp3);
  return 1;
}

/* IMA-ADPCM WAV header for nab.record: 8 kHz mono, 256-byte blocks of 505
 * samples (~4055 B/s). Byte-for-byte the RIFF wrapper lib/hw/reclib.mtl's
 * _reclib_mkriff builds around the same VS1003 record stream for the V1
 * stack, so anything that accepts a V1 recording accepts this. */
#define WAV_HEADER_LEN 60

static void wav_le32(uint8_t *p, uint32_t v)
{
  p[0] = (uint8_t)v;
  p[1] = (uint8_t)(v >> 8);
  p[2] = (uint8_t)(v >> 16);
  p[3] = (uint8_t)(v >> 24);
}

static void wav_adpcm_header(uint8_t *h, uint32_t datalen)
{
  static const uint8_t fmt[32] = {
      'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
      20, 0, 0, 0,        /* fmt chunk length */
      0x11, 0,            /* format 0x0011 = IMA ADPCM */
      1, 0,               /* mono */
      0x40, 0x1F, 0, 0,   /* 8000 Hz */
      0xD7, 0x0F, 0, 0,   /* 4055 bytes/s */
      0, 1,               /* block align 256 */
      4, 0,               /* 4 bits per sample */
      2, 0,               /* extra fmt bytes */
      0xF9, 1,            /* 505 samples per block */
  };
  memcpy(h, "RIFF", 4);
  wav_le32(h + 4, datalen + 52);         /* file size - 8 */
  memcpy(h + 8, fmt, sizeof fmt);
  memcpy(h + 40, "fact", 4);
  wav_le32(h + 44, 4);
  wav_le32(h + 48, (datalen >> 8) * 505);   /* total samples: blocks * 505 */
  memcpy(h + 52, "data", 4);
  wav_le32(h + 56, datalen);
}

/* Cooperative record session state (nab.rec_start/rec_read/rec_stop). Outside
 * record mode HDAT0/HDAT1 mean decode state (stream format / bitrate), so
 * rec_read must not drain them unless a session is actually open. */
#define REC_WAIT  50000UL  /* blocking-read poll bound, ~20x one block time  */
#define REC_CHUNK 2048     /* VS1003 record FIFO: 1024 words - max one drain */

static int rec_active = 0;

/* nab.record(ms [, gain]) -> string: record ~ms milliseconds from the
 * microphone (8 kHz IMA ADPCM, like the V1 stack) and return a complete WAV
 * file. gain: 1024 = 1x, 512 = 0.5x, ...; default 0 = automatic gain control
 * (V1's setting). Blocking; no timer needed - duration is counted in encoded
 * 256-byte blocks (~63 ms each), so it is sample-clock accurate. The result
 * can be shorter than asked (header says how much) if the codec stops
 * delivering - off hardware it is header-only, see vlsi_rec_read. For
 * recording while doing other work, use nab.rec_start/rec_read/rec_stop. */
static int nab_record(lua_State *L)
{
  lua_Integer ms = luaL_checkinteger(L, 1);
  lua_Integer gain = luaL_optinteger(L, 2, 0);
  luaL_argcheck(L, ms >= 1 && ms <= 30000, 1, "1..30000");
  luaL_argcheck(L, gain >= 0 && gain <= 65535, 2, "0..65535");

  uint32_t want = ((uint32_t)ms * 4055UL / 1000 + 255) & ~255UL;
  if (want == 0)
    want = 256;

  luaL_Buffer b;
  uint8_t *out = (uint8_t *)luaL_buffinitsize(L, &b, WAV_HEADER_LEN + want);

  vlsi_rec_start(8000, (uint16_t)gain);
  rec_active = 1;   /* takes over the codec: ends any open rec_start session */
  uint32_t got = 0;
  while (got < want) {
    uint32_t n = vlsi_rec_read(out + WAV_HEADER_LEN + got, want - got, REC_WAIT);
    if (n == 0)
      break;   /* codec never delivered (simulator / wedged chip) */
    got += n;
  }
  vlsi_rec_stop();
  rec_active = 0;

  wav_adpcm_header(out, got);
  luaL_pushresultsize(&b, WAV_HEADER_LEN + got);
  return 1;
}

/* nab.rec_start([gain]): open a cooperative record session - the codec starts
 * encoding the mic into its FIFO and the CPU is free. Poll nab.rec_read()
 * often enough (the ~2 KB FIFO holds ~half a second at 8 kHz; overflow just
 * drops audio, no crash) and interleave whatever else - LEDs, ears, net. */
static int nab_rec_start(lua_State *L)
{
  lua_Integer gain = luaL_optinteger(L, 1, 0);
  luaL_argcheck(L, gain >= 0 && gain <= 65535, 1, "0..65535");
  vlsi_rec_start(8000, (uint16_t)gain);
  rec_active = 1;
  return 0;
}

/* nab.rec_read() -> string|nil: drain the record FIFO, returning immediately.
 * A string of one or more whole 256-byte ADPCM blocks, or nil when no full
 * block is buffered yet (or no session is open). Concatenate the chunks: they
 * are exactly the data section of nab.record's WAV - nab.rec_wav wraps them. */
static int nab_rec_read(lua_State *L)
{
  if (rec_active) {
    luaL_Buffer b;
    uint8_t *dst = (uint8_t *)luaL_buffinitsize(L, &b, REC_CHUNK);
    uint32_t n = vlsi_rec_read(dst, REC_CHUNK, 0);
    if (n > 0) {
      luaL_pushresultsize(&b, n);
      return 1;
    }
  }
  lua_pushnil(L);
  return 1;
}

/* nab.rec_stop(): close the record session (codec back to decode mode). */
static int nab_rec_stop(lua_State *L)
{
  (void)L;
  vlsi_rec_stop();
  rec_active = 0;
  return 0;
}

/* nab.rec_wav(data) -> string: wrap concatenated nab.rec_read chunks (whole
 * 256-byte blocks) in the same WAV header nab.record produces. */
static int nab_rec_wav(lua_State *L)
{
  size_t len;
  const char *data = luaL_checklstring(L, 1, &len);
  luaL_argcheck(L, (len & 255) == 0, 1, "length must be a multiple of 256");

  luaL_Buffer b;
  uint8_t *out = (uint8_t *)luaL_buffinitsize(L, &b, WAV_HEADER_LEN + len);
  wav_adpcm_header(out, (uint32_t)len);
  memcpy(out + WAV_HEADER_LEN, data, len);
  luaL_pushresultsize(&b, WAV_HEADER_LEN + len);
  return 1;
}

/* nab.wheel(): 8-bit ADC ch.2 reading (0..255). The back wheel is almost
 * certainly an analog pot on this channel (ADCON1_CH2, same register sequence
 * as src/firmware's get_adc_value) - not yet hardware-confirmed. To map it to
 * volume: `nab.volume(nab.wheel())` in a polling loop. */
static int nab_wheel(lua_State *L)
{
  lua_pushinteger(L, adc_read_ch2());
  return 1;
}

/* uid -> "a1b2c3d4e5f60708" (lowercase hex) on the Lua stack; shared by
 * nab.rfid and the event dispatcher. */
static void push_uid_hex(lua_State *L, const uint8_t uid[8])
{
  char hex[17];
  fmt_hex8(hex, uid);
  lua_pushstring(L, hex);
}

/* nab.rfid() -> UID as a lowercase hex string (e.g. "a1b2c3d4e5f60708"), or
 * nil if no tag is on the coupler. Scans the CRX14 (I2C 0xA0) each call - no
 * caching, so placing/removing a tag is reflected on the next poll. For
 * scripts that would call this in a loop, prefer nab.on('rfid', fn) (#195). */
static int nab_rfid(lua_State *L)
{
  uint8_t uid[8];
  int8_t found = rfid_read_uid(uid);
  if (found <= 0) {
    lua_pushnil(L);
    return 1;
  }
  push_uid_hex(L, uid);
  return 1;
}

/* ---- cooperative events (#195): nab.on / nab.wait / nab.time -------------- */
/* Principle 2 lands here: event.c polls the hardware from the cooperative
 * pump (never an ISR) and queues edge events; this block delivers them to Lua
 * callbacks under lua_pcall. Dispatch runs while the REPL prompt sits idle
 * and inside nab.wait() - Lua code never runs behind the script's back. */

#define EVENTS_TABLE "nab.events"  /* registry key: {button=fn, rfid=fn} */
#define CONSOLE_IDLE_MS 500        /* RX quiet this long before a coupler scan */

static const char *const event_names[] = {"button", "rfid", "tick", NULL};

static void report(lua_State *L);  /* defined with the REPL below */

/* Pump the pollers and deliver queued events to the registered callbacks,
 * each under lua_pcall (principle 3: a callback error prints and dispatch
 * continues; it never takes the runtime down). Not reentrant: a callback
 * that ends up back here (e.g. via nab.wait) must not dispatch recursively,
 * so nested calls no-op and the outer loop delivers anything new. */
static void dispatch_events(lua_State *L, uint8_t allow_rfid)
{
  static uint8_t busy;
  /* Sample the hardware ALWAYS, even when re-entered (#243). The guard exists
   * to stop recursive *Lua dispatch* (principle 2), and event_pump touches no
   * Lua state - the queue is precisely the buffer that decouples the two. With
   * the pump inside the guard, a nab.wait() called from a callback stopped the
   * debouncer and the scan cycle outright, so a press+release entirely inside
   * that window was never even observed, let alone queued for later. */
  event_pump(allow_rfid);
  if (busy)
    return;
  busy = 1;
  event_t e;
  /* The callback table is fetched once, not per event: it is the same table
   * throughout, and the registry lookup hashes EVENTS_TABLE every time. */
  lua_getfield(L, LUA_REGISTRYINDEX, EVENTS_TABLE);
  int cbs = lua_gettop(L);
  while (event_next(&e)) {
    lua_getfield(L, cbs,
                 (e.type == EV_RFID_TAG || e.type == EV_RFID_GONE) ? "rfid"
                                                                   : "button");
    if (!lua_isfunction(L, -1)) {
      lua_pop(L, 1); /* callback cleared after the event was queued */
      continue;
    }
    switch (e.type) {
      case EV_BUTTON_DOWN: lua_pushboolean(L, 1); break;
      case EV_BUTTON_UP:   lua_pushboolean(L, 0); break;
      case EV_RFID_TAG:    push_uid_hex(L, e.uid); break;
      default:             lua_pushnil(L); break; /* EV_RFID_GONE */
    }
    if (lua_pcall(L, 1, 0, 0) != LUA_OK)
      report(L);
  }
  /* Cooperative tick (#283): once the C event queue is drained, hand the Lua
   * reactor a slice. This is the seam that lets behaviour which must keep
   * running during a blocking call - an ear stopping on its target, a net
   * connection being pumped - actually run, without the C layer knowing what
   * any of it is. Registered with nab.on("tick", fn); under lua_pcall like
   * every other callback (principle 3), and inside the busy guard, so a
   * nab.wait() from within the tick cannot recurse into dispatch. Reuses the
   * callback table already on the stack rather than hashing EVENTS_TABLE again. */
  lua_getfield(L, cbs, "tick");
  if (lua_isfunction(L, -1)) {
    if (lua_pcall(L, 0, 0, 0) != LUA_OK)
      report(L);
  } else {
    lua_pop(L, 1);
  }
  lua_settop(L, cbs - 1); /* drop the callback table */
  busy = 0;
}

/* nab.on(name, fn|nil): register (or clear) the callback for an event source.
 * name "button": fn(pressed) on debounced press/release edges. name "rfid":
 * fn(uid) when a new tag lands on the coupler, fn(nil) when it leaves;
 * registering starts the background ~750 ms scan cycle, clearing stops it.
 * name "tick": fn() on every pump iteration, after the event queue is drained -
 * the seam the Lua reactor (sched, #283) hangs off. It is not an event source:
 * nothing is queued for it and it carries no argument.
 * Callbacks fire from the cooperative pump - while the REPL prompt is idle or
 * inside nab.wait()/nab.delay() - never from an interrupt (principle 2). */
static int nab_on(lua_State *L)
{
  int which = luaL_checkoption(L, 1, NULL, event_names);
  int has_fn = !lua_isnoneornil(L, 2);
  if (has_fn)
    luaL_checktype(L, 2, LUA_TFUNCTION);
  lua_settop(L, 2); /* materialize an absent arg 2 as nil */
  lua_getfield(L, LUA_REGISTRYINDEX, EVENTS_TABLE);
  lua_pushvalue(L, 2);
  lua_setfield(L, -2, event_names[which]);
  if (which == 1) /* "rfid" */
    event_rfid_enable((uint8_t)has_fn);
  return 0;
}

/* nab.wait(ms) - also exposed as nab.delay(ms), see above: sleep ~ms on the
 * 1 ms tick while running the event pump, so nab.on callbacks and the reactor
 * tick fire during the wait - the idiomatic script main loop is
 * `while true do nab.wait(100) end`. If the tick is not advancing at all (IRQs
 * masked, or init_tick not yet run) the wait degrades to DelayMs' bounded busy
 * fallback instead of hanging. */
static int nab_wait(lua_State *L)
{
  lua_Integer ms = luaL_checkinteger(L, 1);
  luaL_argcheck(L, ms >= 0 && ms <= 60000, 1, "0..60000");
  uint32_t start = counter_timer;
  lua_Integer fallback = ms; /* counts DelayMs slices if the tick never moves */
  dispatch_events(L, 1);
  while ((counter_timer - start) < (uint32_t)ms && fallback > 0) {
    DelayMs(1);
    if (counter_timer == start)
      fallback--;
    dispatch_events(L, 1);
  }
  return 0;
}

/* nab.time() -> milliseconds since boot: the raw 1 ms tick (counter_timer),
 * a wrapping 32-bit count - subtract two readings, don't compare absolutes.
 * Advances in the simulator too (it models the timer, #102), but on an
 * instruction-count clock - so it is approximate there, not wall time. */
static int nab_time(lua_State *L)
{
  lua_pushinteger(L, (lua_Integer)counter_timer);
  return 1;
}

/* nab.ear_move(n, dir): drive ear motor n (1 or 2) in dir ("forward"|"reverse")
 * until nab.ear_stop() or another nab.ear_move() call. There is no closed-loop
 * position control here (see hal/motor.h: the encoder is a raw hole counter,
 * not a homed position).
 *
 * Single speed by design (#179): these gearmotors have a hard torque floor
 * (~120/255, ~43% PWM duty) below which they only hum without turning, and
 * above it the rotation rate barely changes (HW-measured: ~7-9 encoder
 * counts/700ms flat from 115 to 200, ~11 at 255). So "speed" was effectively
 * stall-or-go; the old speed argument was dropped and the ear always runs at
 * full duty. */
static const char *const ear_dirs[] = {"forward", "reverse", NULL};

static int nab_ear_move(lua_State *L)
{
  lua_Integer n = luaL_checkinteger(L, 1);
  int dir = luaL_checkoption(L, 2, NULL, ear_dirs);
  luaL_argcheck(L, n == 1 || n == 2, 1, "1 or 2");

  run_motor((uint8_t)n, 255, dir == 0 ? FORWARD : REVERSE);
  return 0;
}

/* nab.ear_stop(n): stop ear motor n (1 or 2). */
static int nab_ear_stop(lua_State *L)
{
  lua_Integer n = luaL_checkinteger(L, 1);
  luaL_argcheck(L, n == 1 || n == 2, 1, "1 or 2");
  stop_motor((uint8_t)n);
  return 0;
}

/* nab.ear_pos(n) -> integer: raw 16-bit encoder pulse count for motor n (1 or
 * 2). Free-running hardware counter - watch it change while ear_move runs,
 * it is not a homed/absolute angle (see hal/motor.h). */
static int nab_ear_pos(lua_State *L)
{
  lua_Integer n = luaL_checkinteger(L, 1);
  luaL_argcheck(L, n == 1 || n == 2, 1, "1 or 2");
  lua_pushinteger(L, get_motor_position((uint8_t)n));
  return 1;
}

/* nab.sci(reg): read a VS1003 SCI register (diagnostic). e.g. HDAT1=0x09 shows
 * the decoder's detected stream format (0 = nothing decoding), SS_VER lives in
 * STATUS=0x01. Used to confirm nab.play's stream reaches the decoder. */
static int nab_sci(lua_State *L)
{
  uint8_t reg = (uint8_t)luaL_checkinteger(L, 1);
  lua_pushinteger(L, vlsi_read_sci(reg));
  return 1;
}

/* nab.sciw(reg, val): write a VS1003 SCI register (diagnostic). Pairs with
 * nab.sci to bring up / probe the codec from the REPL. */
static int nab_sciw(lua_State *L)
{
  uint8_t reg = (uint8_t)luaL_checkinteger(L, 1);
  uint16_t val = (uint16_t)luaL_checkinteger(L, 2);
  vlsi_write_sci(reg, val);
  return 0;
}

/* nab.wifi(ssid [, psk]) -> true on connect, or (nil, message, reason).
 * Blocking: brings up the USB RT2501 dongle (cold-boot + firmware upload +
 * radio settle), scans for ssid, then runs the WPA/WPA2 join (auth + assoc +
 * 4-way handshake), pumping until connected or ~30 s. psk is required for an
 * encrypted AP; omit (or "") for an open one. On failure `reason` is a stable
 * tag - "radio" (bring-up), "notfound" (SSID not seen), "auth" (encrypted AP,
 * join never completed - a bad-PSK hint, advisory) or "timeout" - that the
 * provisioning flow (#234) branches on to pick an LED/message. The whole USB +
 * 802.11 stack is pulled into the image only because this binding references
 * it (see hal/wifi.c). */
static int nab_wifi(lua_State *L)
{
  const char *ssid = luaL_checkstring(L, 1);
  const char *psk = luaL_optstring(L, 2, "");
  wifi_fail_t why = WIFI_OK;
  if (wifi_connect_ex(ssid, psk, 30000, &why) != 0) {
    /* (nil, message, reason): reason is a stable machine-readable tag the
     * provisioning flow (#234) branches on; message is for humans. */
    const char *reason = "timeout";
    switch (why) {
    case WIFI_FAIL_RADIO:    reason = "radio"; break;
    case WIFI_FAIL_NOTFOUND: reason = "notfound"; break;
    case WIFI_FAIL_AUTH:     reason = "auth"; break;
    default:                 reason = "timeout"; break;
    }
    lua_pushnil(L);
    lua_pushfstring(L, "wifi: could not connect to '%s' (%s)", ssid, reason);
    lua_pushstring(L, reason);
    return 3;
  }
  lua_pushboolean(L, 1);
  return 1;
}

/* nab.wifi_ap(ssid [, channel]) -> true | nil, message. Switch the radio to
 * master (AP) mode: beacon `ssid` on `channel` (default 1) as an OPEN network
 * - the driver path Violet's original setup mode used, setup-mode only (#216).
 * Brings up the USB dongle first when needed (~10 s cold boot); an
 * already-joined STA switches without the cold boot. Frames stations send us
 * arrive via nab.wifi_recv(). */
static int nab_wifi_ap(lua_State *L)
{
  const char *ssid = luaL_checkstring(L, 1);
  lua_Integer ch = luaL_optinteger(L, 2, 1);
  luaL_argcheck(L, ch >= 1 && ch <= 14, 2, "1..14");
  if (wifi_ap(ssid, (uint8_t)ch) != 0) {
    lua_pushnil(L);
    lua_pushliteral(L, "wifi_ap: bad SSID or radio bring-up failed");
    return 2;
  }
  lua_pushboolean(L, 1);
  return 1;
}

/* nab.wifi_up() -> true | nil, message. Cold-boot the USB dongle (~10 s) WITHOUT
 * joining or beaconing, so nab.wifi_mac() reads the real EEPROM MAC. setup.run
 * (#233) calls this before deriving the "Nabaztag-XXXX" AP name: the MAC is
 * all-zero until the radio is up, and nab.wifi_ap is itself the bring-up, so
 * naming off wifi_mac() before it yielded "Nabaztag-0000" on every device. With
 * the dongle already up, the following nab.wifi_ap skips the cold boot (its
 * BROKEN check is false) and only sets master mode - no double boot. */
static int nab_wifi_up(lua_State *L)
{
  if (wifi_up() != 0) {
    lua_pushnil(L);
    lua_pushliteral(L, "wifi_up: radio bring-up failed");
    return 2;
  }
  lua_pushboolean(L, 1);
  return 1;
}

/* nab.wifi_send(dst_mac, payload) -> true | nil, message. One raw data frame
 * at the 802.3 payload boundary: payload (a byte string, starts at LLC) goes
 * to dst_mac, a 6-byte binary string ("\xFF\xFF\xFF\xFF\xFF\xFF" = broadcast;
 * nab.wifi_recv's src_mac can be passed straight back to reply). Needs an
 * association (nab.wifi) or AP mode (nab.wifi_ap) first. */
static int nab_wifi_send(lua_State *L)
{
  size_t maclen, len;
  const char *mac = luaL_checklstring(L, 1, &maclen);
  const char *payload = luaL_checklstring(L, 2, &len);
  luaL_argcheck(L, maclen == 6, 1, "6-byte MAC");
  luaL_argcheck(L, len >= 1 && len <= WIFI_SEND_MAX, 2, "1..1500 bytes");
  if (wifi_send((const uint8_t *)mac, (const uint8_t *)payload,
                (uint32_t)len) != 0) {
    lua_pushnil(L);
    lua_pushliteral(L, "wifi_send: not connected or TX failed");
    return 2;
  }
  lua_pushboolean(L, 1);
  return 1;
}

/* nab.wifi_recv([timeout_ms]) -> src_mac, payload | nil. Pop the oldest
 * buffered RX data frame, pumping the driver up to timeout_ms (default 0 =
 * single poll) for one to arrive. src_mac is the sender as a 6-byte binary
 * string, payload the 802.3 payload bytes (from LLC). Frames are captured
 * from the main-loop pump - never in the IRQ path - once nab.wifi_ap() or the
 * first nab.wifi_recv() call enables capture; only a bounded few are buffered,
 * so poll faster than the peer sends. */
static int nab_wifi_recv(lua_State *L)
{
  lua_Integer ms = luaL_optinteger(L, 1, 0);
  luaL_argcheck(L, ms >= 0 && ms <= 60000, 1, "0..60000");
  /* Wait cooperatively (#283). wifi_recv_frame(0) is one driver pump slice, so
   * running the timeout here - with the event pump and reactor tick between
   * slices - keeps callbacks firing and ears stopping on target throughout a
   * DHCP exchange or an HTTP GET. Doing it this way rather than in net/iface.lua
   * is deliberate: iface is driver-agnostic (drv.time/recv) precisely so its
   * unit tests run on host lua with no nab at all, and threading the reactor
   * through it would break that. Every net flow is built on this one binding,
   * so fixing it here fixes all of them. */
  uint32_t t0 = counter_timer;
  struct rt2501buffer *r;
  for (;;) {
    r = wifi_recv_frame(0);
    if (r != NULL || (counter_timer - t0) >= (uint32_t)ms)
      break;
    dispatch_events(L, 1);
  }
  if (r == NULL) {
    lua_pushnil(L);
    return 1;
  }
  lua_pushlstring(L, (const char *)r->source_mac, 6);
  lua_pushlstring(L, (const char *)r->data, r->length);
  wifi_frame_free(r);
  return 2;
}

/* nab.wifi_mac() -> our 6-byte station MAC: the identity lua/lib/net's ARP/DHCP
 * put on the wire (#217). All-zero until nab.wifi()/nab.wifi_ap() has brought
 * the dongle up once (it is read from the EEPROM during bring-up). */
static int nab_wifi_mac(lua_State *L)
{
  lua_pushlstring(L, (const char *)wifi_mac(), 6);
  return 1;
}

/* Copy string field `k` of the table at index 1 into dst (missing/nil -> "").
 * Errors out on a non-string value or one that overflows the field - the
 * binding owns the sector layout, so bounds are enforced here, not in Lua. */
static void cfg_field(lua_State *L, const char *k, char *dst, size_t cap)
{
  lua_getfield(L, 1, k);
  size_t len = 0;
  const char *s = "";
  if (!lua_isnil(L, -1))
    s = lua_tolstring(L, -1, &len);
  if (s == NULL || len >= cap)
    luaL_error(L, "config: bad %s", k);
  memcpy(dst, s, len);
  dst[len] = '\0';
  lua_pop(L, 1);
}

/* nab.config() -> {ssid=,psk=,url=,fails=} or nil: the record persisted in the
 * internal-flash config sector (last 4 KB - survives power cycles; nil until
 * first written). nab.config{ssid=..., psk=..., url=..., fails=...} -> boolean:
 * persist the record (missing string keys become "", fails becomes 0); ssid <=
 * 32, psk <= 64, url <= 64 chars, fails 0..255. `fails` is the consecutive-
 * join-failure counter the provisioning flow (#234) read-modify-writes. The
 * sector is erase-cycled per write, so a record identical to what flash already
 * holds is skipped and returns false; true means erased, programmed and
 * verified by read-back (~63 ms with interrupts masked - see hal/config.h).
 * Only the struct crosses the seam: no address or length is ever taken from Lua
 * (sandbox principle 5). */
static int nab_config(lua_State *L)
{
  nab_config_t cfg;
  if (lua_isnoneornil(L, 1)) {
    if (config_load(&cfg) != 0) {
      lua_pushnil(L);
      return 1;
    }
    lua_createtable(L, 0, 4);
    lua_pushstring(L, cfg.ssid);
    lua_setfield(L, -2, "ssid");
    lua_pushstring(L, cfg.psk);
    lua_setfield(L, -2, "psk");
    lua_pushstring(L, cfg.url);
    lua_setfield(L, -2, "url");
    lua_pushinteger(L, cfg.fails);
    lua_setfield(L, -2, "fails");
    return 1;
  }
  luaL_checktype(L, 1, LUA_TTABLE);
  cfg_field(L, "ssid", cfg.ssid, sizeof cfg.ssid);
  cfg_field(L, "psk", cfg.psk, sizeof cfg.psk);
  cfg_field(L, "url", cfg.url, sizeof cfg.url);
  lua_getfield(L, 1, "fails");
  lua_Integer f = luaL_optinteger(L, -1, 0);
  luaL_argcheck(L, f >= 0 && f <= 255, 1, "fails 0..255");
  cfg.fails = (uint8_t)f;
  lua_pop(L, 1);
  int8_t rc = config_save(&cfg);
  if (rc < 0)
    return luaL_error(L, "config: write failed");
  lua_pushboolean(L, rc == 0);
  return 1;
}

/* nab.flash_firmware(image): whole-image OTA flash (#235). `image` is the
 * verified firmware .bin as a byte string - net.ota has ALREADY checked its
 * magic / target id / length / CRC, so nothing unverified reaches here. Masks
 * interrupts and hands the bytes to the .ramfunc writer, which erases internal
 * flash from address 0, programs the image and watchdog-reboots into it -
 * so on success this never returns. Refuses an empty or over-budget image
 * (returns nil, message), the only path that returns. BRICK RISK: there is no
 * A/B slot; a bad image bricks the device (JTAG recovery). The image string
 * lives in the ExtRAM Lua heap, readable while internal flash programs itself. */
static int nab_flash_firmware(lua_State *L)
{
  size_t len;
  const char *img = luaL_checklstring(L, 1, &len);
  if (len == 0 || len > OTA_MAX_IMAGE) {
    lua_pushnil(L);
    lua_pushfstring(L, "flash_firmware: image size %d out of range", (int)len);
    return 2;
  }
  __disable_interrupt();
  ota_flash_image((const uint8_t *)img, (uint32_t)len);
  __enable_interrupt(); /* unreachable on hardware (watchdog reboot) */
  return 0;
}

static const luaL_Reg nab_funcs[] = {
    {"led", nab_led},
    {"wifi", nab_wifi},
    {"wifi_ap", nab_wifi_ap},
    {"wifi_up", nab_wifi_up},
    {"wifi_send", nab_wifi_send},
    {"wifi_recv", nab_wifi_recv},
    {"wifi_mac", nab_wifi_mac},
    {"config", nab_config},
    {"flash_firmware", nab_flash_firmware},
    {"led8", nab_led8},
    {"fade", nab_fade},
    {"delay", nab_wait},   /* alias: a delay pumps the reactor too (#283) */
    {"button", nab_button},
    {"volume", nab_volume},
    {"beep", nab_beep},
    {"play", nab_play},
    {"tone", nab_tone},
    {"record", nab_record},
    {"rec_start", nab_rec_start},
    {"rec_read", nab_rec_read},
    {"rec_stop", nab_rec_stop},
    {"rec_wav", nab_rec_wav},
    {"wheel", nab_wheel},
    {"rfid", nab_rfid},
    {"on", nab_on},
    {"wait", nab_wait},
    {"time", nab_time},
    {"ear_move", nab_ear_move},
    {"ear_stop", nab_ear_stop},
    {"ear_pos", nab_ear_pos},
    {"sci", nab_sci},
    {"sciw", nab_sciw},
    {NULL, NULL},
};

static int luaopen_nab(lua_State *L)
{
  lua_newtable(L); /* callback table for nab.on / dispatch_events (#195) */
  lua_setfield(L, LUA_REGISTRYINDEX, EVENTS_TABLE);
  luaL_newlib(L, nab_funcs);
  return 1;
}

/* ---- Lua runtime --------------------------------------------------------- */
/* Trimmed stdlib for the 124 KB flash budget (see the Makefile's LUA_LIB note):
 * base + string + table + coroutine. Dropped: math (pulls ~16 KB of libm trig),
 * io/os/package/debug/loadlib (no filesystem, OS, or dynamic loading on this
 * target), and utf8. base's dofile/loadfile are removed in lua/lbaselib.c.
 *
 * coroutine costs 2,300 B (measured, #283) and buys the cooperative scheduler:
 * without it every long-running activity has to be hand-unrolled into a state
 * machine that some other loop remembers to pump, which is exactly why the four
 * workloads could not compose. */
static const luaL_Reg loadedlibs[] = {
    {LUA_GNAME, luaopen_base},
    {LUA_TABLIBNAME, luaopen_table},
    {LUA_STRLIBNAME, luaopen_string},
    {LUA_COLIBNAME, luaopen_coroutine},
    {"nab", luaopen_nab},   /* LEDs + button + audio + RFID + ears */
    {NULL, NULL},
};

static void open_trimmed_libs(lua_State *L)
{
  for (const luaL_Reg *lib = loadedlibs; lib->func != NULL; lib++) {
    luaL_requiref(L, lib->name, lib->func, 1);
    lua_pop(L, 1); /* remove the library table left on the stack */
  }
}

/* Lua's "randomness" sources (string-hash seed, table.sort's fallback pivot),
 * routed here by luaconf.h so they read the 1 ms tick instead of the C wall
 * clock - see the luai_tickseed note there. */
unsigned int luai_tickseed(void)
{
  return (unsigned int)counter_timer;
}

/* Print and clear a Lua error message sitting on the top of the stack. */
static void report(lua_State *L)
{
  const char *msg = lua_tostring(L, -1);
  sh_puts(msg ? msg : "(error with no message)");
  sh_puts("\n");
  lua_pop(L, 1);
}

/* Resident boot chunk: the M5 nab-binding demo helpers, a short LED showcase
 * (#102) that doubles as a timer/fade self-test, and an idle LED state, defined
 * at startup so the interpreter proves out with no console input (sim) and both
 * demos are one short line away on hardware. It does NOT auto-run anything long:
 * run() is a while-true RFID loop that only returns on a head-button press, and
 * ledshow() is a ~6 s animation - auto-calling either at boot would delay or
 * strand the REPL (the boot chunk eats the instruction budget before the prompt,
 * #207). Both bodies are resident, so `ledshow()` (breathe blue/magenta, then a
 * ball round the ring) exercises the timer IRQ + fade engine with a 10-char
 * feed, not a ~2 KB script.
 *
 * This image has no on-device parser (#128), so the chunk cannot be compiled
 * from source at boot. The build compiles lua/boot/boot.lua off-device
 * (tools/luac/embed.py) into gen/boot_lc.h - a `boot_lc[]` bytecode blob loaded
 * below via luaL_loadbuffer (sizeof boot_lc = chunk length). Edit boot.lua, not
 * this header. The fuller LED show is ../apps/led-demo.lua. */
#include "boot_lc.h"

#define REPL_LINE 256

/* ---- off-device luac bytecode frames ------------------------------------- */
/* This image drops lparser/llex/lcode (~18.9 KB, #128), so it can ONLY load
 * bytecode - it cannot compile source on-device. Host-side luac (tools/luac)
 * compiles every REPL line off-device and ships the chunk here as a framed hex
 * payload:
 *
 *     #LC:<len>\n            header line (len = chunk size in bytes, decimal)
 *     <2*len hex chars>      the chunk, whitespace/newlines ignored (wrapped 64c)
 *
 * Raw bytecode can't ride this line-oriented console directly (chunks contain
 * '\n'/NUL and sh_gets is line-based), hence the hex framing. #LC frames are the
 * only executable input the REPL accepts; anything else is rejected (see repl). */
#define LC_MAX 65536   /* sanity cap on a single bytecode chunk */

/* Read the next hex digit off the console, skipping the whitespace the sender
 * uses to wrap the payload. Returns 0..15, or -1 on EOF / a non-hex byte. */
static int read_hex_nibble(void)
{
  char c;
  for (;;) {
    if (_read(0, &c, 1) != 1)
      return -1; /* EOF */
    if (c == ' ' || c == '\n' || c == '\r' || c == '\t')
      continue; /* framing whitespace */
    if (c >= '0' && c <= '9')
      return c - '0';
    if (c >= 'a' && c <= 'f')
      return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
      return c - 'A' + 10;
    return -1; /* not hex -> malformed frame */
  }
}

/* Consume the console up to and including the next newline. After a frame's
 * 2*len hex chars there is still the payload's line terminator sitting in the
 * stream; dropping it here keeps the following sh_gets from reading that '\n'
 * as a spurious empty REPL line (which would double every prompt). */
static void skip_to_eol(void)
{
  char c;
  while (_read(0, &c, 1) == 1) {
    if (c == '\n')
      break;
  }
}

/* Parse a "#LC:<len>" header (already read into `line`), stream the following
 * 2*len hex chars into a fresh buffer, and load it as a Lua chunk. Leaves the
 * compiled chunk on the stack on success (like load_line), else pushes an error
 * message and returns non-LUA_OK. No strtol - a manual digit loop keeps newlib
 * out. The buffer comes from the external-RAM heap (_sbrk). */
static int load_lc_frame(lua_State *L, const char *line)
{
  const char *p = line + 4; /* past "#LC:" */
  if (*p < '0' || *p > '9') {
    lua_pushliteral(L, "malformed #LC frame header");
    return LUA_ERRSYNTAX;
  }
  long len = 0;
  for (; *p >= '0' && *p <= '9'; p++) {
    len = len * 10 + (*p - '0');
    if (len > LC_MAX) {
      lua_pushliteral(L, "#LC frame too large");
      return LUA_ERRSYNTAX;
    }
  }

  char *buf = (len > 0) ? malloc((size_t)len) : NULL;
  if (len > 0 && buf == NULL) {
    lua_pushliteral(L, "out of memory reading #LC frame");
    return LUA_ERRMEM;
  }
  for (long i = 0; i < len; i++) {
    int hi = read_hex_nibble();
    int lo = read_hex_nibble();
    if (hi < 0 || lo < 0) {
      free(buf);
      lua_pushliteral(L, "truncated or non-hex #LC frame payload");
      return LUA_ERRSYNTAX;
    }
    buf[i] = (char)((hi << 4) | lo);
  }
  skip_to_eol(); /* drop the payload's trailing newline (see skip_to_eol) */

  /* "=stdin" chunkname matches the host pipe's luaL_loadbuffer name. The chunk
   * starts with LUA_SIGNATURE, so lua_load takes the lundump (bytecode) branch;
   * a non-bytecode payload would hit the guarded f_parser text branch and error
   * (there is no parser in this image, #128). */
  int status = luaL_loadbuffer(L, buf, (size_t)len, "=stdin");
  free(buf);
  return status;
}

/* Run a compiled chunk sitting on top of the stack - from either a source line
 * or a #LC frame - by pcall + echoing any returned values through print(),
 * exactly as the stock `lua` prompt does. Shared by both input paths so their
 * transcripts stay byte-identical (the point of the round-trip test). */
static void run_chunk(lua_State *L)
{
  int base = lua_gettop(L) - 1; /* stack height below the chunk */
  if (lua_pcall(L, 0, LUA_MULTRET, 0) != LUA_OK) {
    report(L);
  } else {
    int nres = lua_gettop(L) - base; /* values the chunk returned */
    if (nres > 0) {                  /* echo them via print() */
      lua_getglobal(L, "print");
      lua_insert(L, base + 1);
      if (lua_pcall(L, nres, 0, 0) != LUA_OK)
        report(L);
    }
  }
}

static void repl(lua_State *L)
{
  char line[REPL_LINE];
  sh_puts("> ");
  for (;;) {
    /* Idle between lines: run the cooperative pump (#195) so nab.on callbacks
     * fire while the prompt waits. The ~5 ms RFID coupler scan could overflow
     * the 16-byte RX FIFO if bytes arrived mid-scan, so it is gated on the
     * console having been quiet for CONSOLE_IDLE_MS; the (cheap) button poll
     * always runs. Once a byte is pending we fall through to the blocking
     * line read - no pumping mid-line or mid-#LC-frame. */
    while (!rxrdy_uart())
      dispatch_events(L, (counter_timer - console_last_ms) > CONSOLE_IDLE_MS);
    if (sh_gets(line, sizeof line) == NULL)
      break;
    if (line[0] == '\n' || line[0] == '\0') {
      /* blank line: no-op, just re-prompt (matches the stock lua prompt) */
    } else if (line[0] == '#' && line[1] == 'L' && line[2] == 'C' && line[3] == ':') {
      if (load_lc_frame(L, line) != LUA_OK) /* off-device luac bytecode */
        report(L);                          /* frame error */
      else
        run_chunk(L);
    } else {
      /* Bytecode-only build (#128): no on-device parser. Source is compiled
       * off-device (tools/luac) and sent as an #LC frame - see luash.py. */
      sh_puts("bytecode-only build: send #LC frames (see tools/luac)\n");
    }
    sh_puts("> ");
  }
}

/* Bring up the LED bus + button for the nab bindings. LED init mirrors blink.c
 * (the LLC2_4c LED-only subset of the firmware's init_io). */
static void init_hw(void)
{
  /* #102: populate IRQ_HANDLER_TABLE + the interrupt controller before any
   * source is armed. Exactly once (#244): init_irq() is not idempotent-in-place
   * - it zeroes ILC0/ILC1/EXILC* and resets all 64 handler slots to
   * null_handler - so a second call here silently discarded anything the
   * peripherals below had registered. Nothing registered between the two calls
   * as it stood, which is the only reason it was harmless. */
  init_irq();

  /* #275: mux XD16-31 (upper external-data-bus half) to GPIO, as mtl's
   * init_io does. BWC=0xA0 runs ExtRAM as a 16-bit bank, so XD16-31 carry no
   * data - but left on their default bus function they toggle on every EMC
   * WRITE, and they share package pins with the audio-control GPIO group
   * (RST_AUDIO = PIO11.7): each Lua-heap write burst hardware-reset the
   * VS1003 (CLOCKF/MODE/VOLUME back to defaults), killing nab.record and
   * forcing vlsi_play's re-assert workaround. Reads leave the bus hi-Z,
   * which is why playback mostly survived. Isolated by examples/recprobe.c. */
  set_wbit(PORTSEL4, 0x00000040);

  CS_LED_AS_OUTPUT;
  MODE_LED_AS_OUTPUT;
  CS_LED_SET;
  MODE_LED_CLEAR;
  init_spi();
  init_led_rgb_driver();
  init_button();
  init_vlsi();   /* VS1003 audio codec on SPI0, for nab.beep/volume */
  init_adc();    /* ADC ch.2 (PD2), for nab.wheel() */
  init_i2c();    /* I2C bus, for the CRX14 RFID coupler / nab.rfid() */
  init_ears();   /* FTM PWM + encoder timers, for nab.ear_* */

  /* #102: arm the shared 1 ms System Timer tick last, once every peripheral is
   * up. init_tick() (sys/src/tick.c) reprograms the timer to 1 ms, registers its
   * ISR, and unmasks interrupts; its handler advances nab.fade's background fades
   * (led_fade_tick) and counter_timer (nab.delay's clock). The simulator models
   * the timer/IRQ too, so fades advance in the sim as well as on hardware. */
  init_tick();
}

int main(void)
{
  init_uart();                      /* console up first (#207): _read/_write, sh_puts */
  init_hw();                        /* LEDs + button, for the nab bindings */

  lua_State *L = luaL_newstate();
  if (L == NULL) {
    sh_puts("lua: cannot create state (out of memory)\n");
    for (;;) {
    }
  }
  open_trimmed_libs(L);

  /* Load + run the resident boot chunk (precompiled bytecode; see boot_lc.h). */
  if (luaL_loadbuffer(L, (const char *)boot_lc, sizeof boot_lc, "=boot") != LUA_OK
      || lua_pcall(L, 0, 0, 0) != LUA_OK)
    report(L);

  repl(L);

  lua_close(L);
  sh_puts("<<FV_DONE>>\n");   /* tell flash.py the run is done (early-exit) */
  for (;;) {
  } /* bare metal: main never returns */
  return 0;
}
