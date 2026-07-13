/**
 * @file motor.h
 * @brief Ear DC-motor + encoder driver over the OKI FTM timer/PWM block.
 *
 * Ported from src/firmware/src/hal/motor.c (Violet / RedoX GCC port). No
 * interrupt needed: init_pwm() only pokes the FTM0-5 timer control registers;
 * run_motor()/stop_motor() drive the PWM duty registers directly; and
 * get_motor_position() reads a free-running hardware pulse-capture counter
 * (FTM0GR/FTM1GR) - no CPU-side counting or ISR involved.
 *
 * The position counter is a raw, monotonically-wrapping 16-bit edge count, not
 * a homed/absolute position. Turning that into "the ear points here" needs the
 * hole-counting state machine in lib/hw/ears.mtl (17 holes/rev, direction +
 * timeout-based arrival detection).
 */
#ifndef _MOTOR_H_
#define _MOTOR_H_

#include <stdint.h>

/* Bring up the FTM PWM (drive) + capture (encoder) timers: configures the
 * PWM_MCC pins as outputs, calls init_pwm(), and stops both motors. Call once
 * at startup (mirrors the motor subset of src/firmware/src/main.c's init_io). */
void init_ears(void);

/** @brief Init the PWM module to drive the 2 DC brush motors and count position. */
void init_pwm(void);

/**
 * @brief Run a specified motor with speed and direction.
 * @param number   1 or 2 (ear motor id)
 * @param speed    PWM duty, 0..255 (255 = full speed)
 * @param rotation FORWARD or REVERSE (common.h)
 */
void run_motor(uint8_t number, uint8_t speed, uint8_t rotation);

/** @brief Stop a specified motor (1 or 2). */
void stop_motor(uint8_t number);

/**
 * @brief Read the raw encoder pulse-capture counter for a motor (1 or 2).
 * Free-running 16-bit hardware counter - wraps, not homed to a known position.
 */
uint16_t get_motor_position(uint8_t number);

#endif
