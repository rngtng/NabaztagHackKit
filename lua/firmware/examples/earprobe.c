/**
 * @file earprobe.c
 * @brief Confirm the ear motors + encoders respond before trusting the Lua
 *        nab.ear_* bindings. Cheap disqualifying test: run each motor briefly
 *        and check whether its FTM0/FTM1 pulse-capture counter
 *        (get_motor_position) actually moves.
 *
 *   task lua:firmware:flash EXAMPLE=earprobe
 *   (on the Pi) stty -F /dev/serial0 38400 raw -echo; cat /dev/serial0
 *
 * Output is on UART0 (38400 8N1), read on the Pi's /dev/serial0. No timer
 * subsystem exists, so "briefly" is a CPU busy-loop, same caveat as nab.beep's ms.
 *
 * probe_motor only ever runs at speed=255, so it never covered the `speed`
 * parameter itself (a user hitting nab.ear_move(n, 100, 'forward') got a motor
 * hum with no movement). sweep_motor() drives each motor across partial speeds
 * and reports the encoder delta per step, to find whether/where there is a
 * minimum effective duty cycle for these gearmotors.
 */
#include "ml674061.h"
#include "common.h"

#include "hal/motor.h"
#include "hal/uart.h"

static void sh_puts(const char *s)
{
  putst_uart((uint8_t *)s);
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

/* Drive `number` FORWARD at each speed in turn (~1.5 s each), reporting the
 * encoder delta over that window - a wrapping 16-bit counter, so plain
 * subtraction is correct even across a wrap. A short stopped gap between
 * steps lets the previous step's motion fully settle before the next starts. */
static void sweep_motor(uint8_t number)
{
  static const uint8_t speeds[] = {255, 200, 150, 120, 100, 80, 60, 40, 20};

  sh_puts("--- motor "); sh_puthex16(number); sh_puts(" speed sweep (FORWARD) ---\n");

  for (unsigned i = 0; i < sizeof(speeds) / sizeof(speeds[0]); i++) {
    uint8_t speed = speeds[i];
    uint16_t before = get_motor_position(number);

    run_motor(number, speed, FORWARD);
    busy_delay(1500000UL);
    stop_motor(number);

    uint16_t after = get_motor_position(number);
    uint16_t delta = (uint16_t)(after - before);

    sh_puts("speed=");
    sh_puthex8(speed);
    sh_puts(" delta=");
    sh_puthex16(delta);
    sh_puts(delta != 0 ? " MOVED\n" : " STUCK (hum only?)\n");

    busy_delay(300000UL); /* settle before the next step */
  }
}

int main(void)
{
  init_uart();

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

  /* --- partial-speed sweep --- */
  sweep_motor(1);
  sweep_motor(2);

  sh_puts("<<FV_DONE>>\n");   /* early-exit signal for flash.py */
  for (;;) {
    CLR_WDT;
  }
  return 0;
}
