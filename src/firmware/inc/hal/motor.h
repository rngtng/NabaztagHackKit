/**
 * @file motor.h
 * @author Violet - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief Motors low level access
 */
#ifndef _MOTOR_H_
#define _MOTOR_H_

void init_pwm(void);
void run_motor(uint8_t number, uint8_t speed, uint8_t rotation);
void stop_motor(uint8_t number);
uint16_t get_motor_position(uint8_t number);

#endif
