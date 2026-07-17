/**
 * @file event.h
 * @brief Cooperative event core (#195): fixed-size queue + hardware pollers.
 *
 * Design principle 2 (README.md): an event-driven core - C polls the
 * hardware, posts edge events into a small fixed-size queue, and the Lua
 * layer drains it via lua_pcall'd callbacks (src/app/lua.c's nab.on).
 * Strictly single-context: event_pump()/event_post()/event_next() all run
 * from the cooperative main loop (REPL idle, nab.wait) and never from an
 * ISR, so the queue needs no IRQ masking. The day an ISR wants to post,
 * add critical sections (hal/i2c.c's __disable_interrupt pattern).
 */
#ifndef _EVENT_H_
#define _EVENT_H_

#include <stdint.h>

enum {
  EV_BUTTON_DOWN = 1,  /* head button pressed (debounced edge) */
  EV_BUTTON_UP,        /* head button released (debounced edge) */
  EV_RFID_TAG,         /* new tag UID on the coupler (uid field valid) */
  EV_RFID_GONE,        /* the tag left the coupler */
};

typedef struct {
  uint8_t type;    /* EV_* */
  uint8_t uid[8];  /* EV_RFID_TAG only */
} event_t;

int8_t event_post(const event_t *e);  /* 0 = queued, -1 = full (dropped) */
int8_t event_next(event_t *e);        /* 1 = popped into *e, 0 = empty */

/* Poll the hardware and post edge events: the (cheap) button read always;
 * the ~5 ms RFID coupler scan only when enabled AND allow_rfid AND its
 * ~750 ms period expired. allow_rfid lets the REPL keep the scan away from
 * moments when console RX bytes could overflow the 16-byte UART FIFO. */
void event_pump(uint8_t allow_rfid);

/* Turn the background RFID poll on/off (on while a Lua callback is
 * registered). Resets the tag-tracking state; the first scan is due
 * immediately after enabling. */
void event_rfid_enable(uint8_t on);

#endif
