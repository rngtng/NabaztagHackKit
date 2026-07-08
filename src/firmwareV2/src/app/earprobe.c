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

static void busy_delay(volatile unsigned long n)
{
  while (n--)
    CLR_WDT;
}

/* Run motor `number` for a short burst and report whether its encoder moved.
 * ~0.3 s at full speed - enough to see several encoder edges without spinning
 * the ear a full turn. */
static void probe_motor(uint8_t number)
{
  sh_puts("motor ");
  sh_puthex16(number);
  sh_puts(": pos before=");
  uint16_t before = get_motor_position(number);
  sh_puthex16(before);

  run_motor(number, 255, FORWARD);
  busy_delay(1000000UL);
  stop_motor(number);

  uint16_t after = get_motor_position(number);
  sh_puts(" after=");
  sh_puthex16(after);
  sh_puts(after != before ? "  -> MOVED (encoder counting)\n"
                          : "  -> NO CHANGE (motor/encoder not responding?)\n");
}

int main(void)
{
  init_ears();

  sh_puts("M10 ear probe (FTM PWM + encoder)\n");
  probe_motor(1);
  probe_motor(2);

  sh_puts("<<FV_DONE>>\n");   /* early-exit signal for flash.py */
  for (;;) {
    /* idle; the report above is the whole probe */
  }
  return 0;
}
