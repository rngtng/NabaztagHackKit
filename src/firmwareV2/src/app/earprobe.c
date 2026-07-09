/**
 * @file earprobe.c
 * @brief M10 probe (issue #118): confirm the ear motors + encoders respond
 *        before trusting the Lua nab.ear_* bindings.
 *
 * Unlike M6's AT45 flash (a chip the board never had) or M8's VS1003 (needing
 * an SCI round-trip to prove aliveness), the ear motors are a known-populated
 * part - the rabbit visibly has two ears. The cheap disqualifying test here is
 * simpler: run each motor briefly and check whether its FTM0/FTM1 pulse-capture
 * counter (get_motor_position) actually moves. This has NOT been run against
 * real hardware from this environment (no JTAG/Pi access) - run it once before
 * trusting nab.ear_move/nab.ear_pos:
 *
 *   task flash:firmwareV2 APP=earprobe
 *   task repl:firmwareV2:hw APP=earprobe
 *
 * Output is ARM semihosting (the M3 #91 console). No timer subsystem exists
 * (see README), so "briefly" is a CPU busy-loop, same caveat as nab.beep's ms.
 */
#include "ml674061.h"
#include "common.h"

#include "hal/motor.h"

/* ---- semihosting console (M3 #91 path) ----------------------------------- */
#define SYS_WRITEC 0x03

static inline int semihost(int op, void *arg)
{
  register int r0 asm("r0") = op;
  register void *r1 asm("r1") = arg;
  asm volatile("svc #0xAB" : "+r"(r0) : "r"(r1) : "memory");
  return r0;
}

static void sh_puts(const char *s)
{
  while (*s) {
    char c = *s++;
    semihost(SYS_WRITEC, &c);
  }
}

static void sh_puthex16(uint16_t v)
{
  const char *hex = "0123456789abcdef";
  char b[7];
  b[0] = '0'; b[1] = 'x';
  b[2] = hex[(v >> 12) & 0xF];
  b[3] = hex[(v >> 8) & 0xF];
  b[4] = hex[(v >> 4) & 0xF];
  b[5] = hex[v & 0xF];
  b[6] = '\0';
  sh_puts(b);
}

static void sh_puthex8(uint8_t v)
{
  const char *hex = "0123456789abcdef";
  char b[5];
  b[0] = '0'; b[1] = 'x';
  b[2] = hex[(v >> 4) & 0xF];
  b[3] = hex[v & 0xF];
  b[4] = '\0';
  sh_puts(b);
}

static void busy_delay(volatile unsigned long n)
{
  while (n--)
    CLR_WDT;
}

/* Read raw FTMn counter (FTMnC) and wide-use register (FTMnGR) for motor n. */
static void dump_ftm(uint8_t number)
{
  uint16_t cnt, gr;
  if (number == 1) {
    cnt = get_hvalue(FTM0C);
    gr  = get_hvalue(FTM0GR);
  } else {
    cnt = get_hvalue(FTM1C);
    gr  = get_hvalue(FTM1GR);
  }
  sh_puts("FTMnC=");
  sh_puthex16(cnt);
  sh_puts(" FTMnGR=");
  sh_puthex16(gr);
  sh_puts("\n");
}

/* Run motor `number` for ~2 s; dump GPIO state and FTM counters at each stage.
 * Long enough to observe physical ear movement. */
static void probe_motor(uint8_t number)
{
  sh_puts("--- motor ");
  sh_puthex16(number);
  sh_puts(" ---\n");

  sh_puts("FTMEN=");
  sh_puthex8(get_value(FTMEN));
  sh_puts(" PM5=");
  sh_puthex8(get_value(PM5));
  sh_puts(" PO5=");
  sh_puthex8(get_value(PO5));
  sh_puts("\n");

  sh_puts("before: ");
  dump_ftm(number);

  run_motor(number, 255, FORWARD);

  sh_puts("after run_motor: PO5=");
  sh_puthex8(get_value(PO5));
  sh_puts("\n");

  /* ~2 s delay — watch the ear */
  busy_delay(2000000UL);

  sh_puts("mid-run: ");
  dump_ftm(number);

  busy_delay(2000000UL);
  stop_motor(number);

  sh_puts("after stop: ");
  dump_ftm(number);

  uint16_t c_after = (number == 1) ? get_hvalue(FTM0C) : get_hvalue(FTM1C);
  sh_puts(c_after != 0 ? "PASS: encoder counted\n" : "FAIL: encoder STUCK\n");
}

int main(void)
{
  init_ears();

  sh_puts("M10 ear probe (FTM PWM + encoder)\n");

  /* --- sequential pass (original verification) --- */
  probe_motor(1);
  probe_motor(2);

  /* --- both-at-once pass: watch both ears spin together --- */
  sh_puts("--- both motors FORWARD (~4 s) ---\n");
  run_motor(1, 255, FORWARD);
  run_motor(2, 255, FORWARD);
  busy_delay(4000000UL);
  stop_motor(1);
  stop_motor(2);
  sh_puts("motor1 pos="); sh_puthex16(get_motor_position(1)); sh_puts("\n");
  sh_puts("motor2 pos="); sh_puthex16(get_motor_position(2)); sh_puts("\n");

  /* --- both REVERSE --- */
  sh_puts("--- both motors REVERSE (~4 s) ---\n");
  run_motor(1, 255, REVERSE);
  run_motor(2, 255, REVERSE);
  busy_delay(4000000UL);
  stop_motor(1);
  stop_motor(2);
  sh_puts("motor1 pos="); sh_puthex16(get_motor_position(1)); sh_puts("\n");
  sh_puts("motor2 pos="); sh_puthex16(get_motor_position(2)); sh_puts("\n");

  sh_puts("<<FV_DONE>>\n");   /* early-exit signal for flash.py */
  for (;;) {
    CLR_WDT;
  }
  return 0;
}
