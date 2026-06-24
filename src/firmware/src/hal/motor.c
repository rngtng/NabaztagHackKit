/**
 * @file motor.c
 * @author Violet - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief Motors low level access
 */
#include "ml674061.h"
#include "common.h"

#include "hal/motor.h"

/**
 * @brief Init the PWM module to drive the 2 DC brush motor and count position
 */
void init_pwm(void)
{
#if (PCB_RELEASE == LLC2_3) || (PCB_RELEASE == LLC2_4c)

  //Init PWM and timers (FTM)
  put_value(FTM0CON, 0x1E);   //Timer0, capture on rising edge
  put_value(FTM1CON, 0x1E);   //Timer1, capture on rising edge
  put_value(FTM2CON, 0x10);   //Timer2, PWM with clock @ APB_CLOCK => 488Hz @32MHz with @ 16bits timer
  put_value(FTM3CON, 0x10);   //Timer3, PWM with clock @ APB_CLOCK => 488Hz @32MHz with @ 16bits timer
#ifdef MOTOR_SPEED_CONTROL
  put_value(FTM4CON, 0x10);   //Timer4, PWM with clock @ APB_CLOCK => 488Hz @32MHz with @ 16bits timer
  put_value(FTM5CON, 0x10);   //Timer5, PWM with clock @ APB_CLOCK => 488Hz @32MHz with @ 16bits timer
#endif

  //Init duty cycle of PWM outputs
  put_hvalue(FTM2GR, 0x0000); //Timer2, duty cycle = 0
  put_hvalue(FTM3GR, 0x0000); //Timer3, duty cycle = 0
#ifdef MOTOR_SPEED_CONTROL
  put_hvalue(FTM4GR, 0x0000); //Timer4, duty cycle = 0
  put_hvalue(FTM5GR, 0x0000); //Timer5, duty cycle = 0
#endif

  //Init PWM mode
  put_value(FTM0IOLV, 0x01); //TIMER0 counts on rising edge
  put_value(FTM1IOLV, 0x01); //TIMER1 counts on rising edge
  put_value(FTM2IOLV, 0x01); //Output 0 when FTM2C is equal or less than FTM2GR
  put_value(FTM3IOLV, 0x01); //Output 0 when FTM3C is equal or less than FTM3GR
#ifdef MOTOR_SPEED_CONTROL
  put_value(FTM4IOLV, 0x01); //Output 1 when FTM4C is equal or less than FTM4GR
  put_value(FTM5IOLV, 0x01); //Output 1 when FTM5C is equal or less than FTM5GR
#endif

  //Clear Timers
  put_hvalue(FTM0C, 0x0000); //Clear Timer0
  put_hvalue(FTM1C, 0x0000); //Clear Timer1
  put_hvalue(FTM2C, 0x0000); //Clear Timer2
  put_hvalue(FTM3C, 0x0000); //Clear Timer3
#ifdef MOTOR_SPEED_CONTROL
  put_hvalue(FTM4C, 0x0000); //Clear Timer4
  put_hvalue(FTM5C, 0x0000); //Clear Timer5
#endif

#ifndef MOTOR_SPEED_CONTROL
  //Turn on Timer0 to 3
  put_value(FTMEN, 0x0F);
#else
  //Turn on Timer0 to 5
  put_value(FTMEN, 0x3F);
#endif

#elif PCB_RELEASE == LLC2_2

  //Init PWM and timers (FTM)
  put_value(FTM0CON, 0x1E);   //Timer0, capture on rising edge
  put_value(FTM1CON, 0x1E);   //Timer1, capture on rising edge
  put_value(FTM2CON, 0x10);   //Timer2, PWM with clock @ APB_CLOCK => 488Hz @32MHz with @ 16bits timer
  put_value(FTM3CON, 0x10);   //Timer3, PWM with clock @ APB_CLOCK => 488Hz @32MHz with @ 16bits timer
  put_value(FTM4CON, 0x10);   //Timer4, PWM with clock @ APB_CLOCK => 488Hz @32MHz with @ 16bits timer
  put_value(FTM5CON, 0x10);   //Timer5, PWM with clock @ APB_CLOCK => 488Hz @32MHz with @ 16bits timer
  //Init duty cycle of PWM outputs
  put_hvalue(FTM2GR, 0x0000); //Timer2, duty cycle = 0
  put_hvalue(FTM3GR, 0x0000); //Timer3, duty cycle = 0
  put_hvalue(FTM4GR, 0x0000); //Timer4, duty cycle = 0
  put_hvalue(FTM5GR, 0x0000); //Timer5, duty cycle = 0
  //Init PWM mode
  put_value(FTM0IOLV, 0x01); //TIMER0 counts on rising edge
  put_value(FTM1IOLV, 0x01); //TIMER1 counts on rising edge
  put_value(FTM2IOLV, 0x01); //Output 1 when FTM2C is equal or less than FTM2GR
  put_value(FTM3IOLV, 0x01); //Output 1 when FTM3C is equal or less than FTM3GR
  put_value(FTM4IOLV, 0x01); //Output 1 when FTM4C is equal or less than FTM4GR
  put_value(FTM5IOLV, 0x01); //Output 1 when FTM5C is equal or less than FTM5GR
  //Clear Timers
  put_hvalue(FTM0C, 0x0000); //Clear Timer0
  put_hvalue(FTM1C, 0x0000); //Clear Timer1
  put_hvalue(FTM2C, 0x0000); //Clear Timer2
  put_hvalue(FTM3C, 0x0000); //Clear Timer3
  put_hvalue(FTM4C, 0x0000); //Clear Timer4
  put_hvalue(FTM5C, 0x0000); //Clear Timer5
  //Turn on Timer0 to 5
  put_value(FTMEN, 0x3F);

#endif
}

/**
 * @brief Run a specified motor with speed and direction
 *
 * @param [in] number   ID/Number of the motor
 * @param [in] speed    Speed in 8bits, max is 0xFF
 * @param [in] rotation Direction of Rotation
 */
void run_motor(uint8_t number, uint8_t speed, uint8_t rotation)
{
#if (PCB_RELEASE == LLC2_3) || (PCB_RELEASE == LLC2_4c)
#ifdef DRIVER_ST
  if(number==1){
//    put_hvalue(FTM2GR, (uint)((255-speed)<<8)); //Timer2, duty cycle = speed
//    put_hvalue(FTM2GR, ((uint)(speed<<8)|0x00ff)); //Timer2, duty cycle = speed
    PWM_MCC1_CLEAR;
    if(rotation==FORWARD){
      PHASE_MCC1_SET;
    }
    else if(rotation==REVERSE){
      PHASE_MCC1_CLEAR;
    }
  }
  else if(number==2){
//    put_hvalue(FTM3GR, (uint)((255-speed)<<8));   //Timer3, duty cycle = speed
//    put_hvalue(FTM3GR, ((uint)(speed<<8)|0x00ff));   //Timer3, duty cycle = speed
    PWM_MCC2_CLEAR;
    if(rotation==FORWARD){
      PHASE_MCC2_SET;
    }
    else if(rotation==REVERSE){
      PHASE_MCC2_CLEAR;
    }
  }
#else
#ifndef MOTOR_SPEED_CONTROL
  if(number==1){
    if(rotation==FORWARD){
      PWM_MCC2_CLEAR;
      PWM_MCC1_SET;
    }
    else if(rotation==REVERSE){
      PWM_MCC1_CLEAR;
      PWM_MCC2_SET;
    }
  }
  else if(number==2){
    if(rotation==FORWARD){
      PWM_MCC4_CLEAR;
      PWM_MCC3_SET;
    }
    else if(rotation==REVERSE){
      PWM_MCC3_CLEAR;
      PWM_MCC4_SET;
    }
  }
#else
  if(number==1){
    if(rotation==FORWARD){
      put_hvalue(FTM3GR, 0x0000);           //Timer3, duty cycle = 0
      put_hvalue(FTM2GR, ((uint16_t)(speed<<8)|0x00ff)); //Timer2, duty cycle = speed
    }
    else if(rotation==REVERSE){
      put_hvalue(FTM2GR, 0x0000);             //Timer2, duty cycle = 0
      put_hvalue(FTM3GR, ((uint16_t)(speed<<8)|0x00ff));   //Timer3, duty cycle = speed
    }
  }
  else if(number==2){
    if(rotation==FORWARD){
      put_hvalue(FTM5GR, 0x0000);           //Timer5, duty cycle = 0
      put_hvalue(FTM4GR, ((uint16_t)(speed<<8)|0x00ff)); //Timer4, duty cycle = speed
    }
    else if(rotation==REVERSE){
      put_hvalue(FTM4GR, 0x0000);             //Timer4, duty cycle = 0
      put_hvalue(FTM5GR, ((uint16_t)(speed<<8)|0x00ff));   //Timer5, duty cycle = speed
    }
  }
#endif
#endif
#elif PCB_RELEASE == LLC2_2
  if(number==1){
    if(rotation==FORWARD){
      put_hvalue(FTM3GR, 0x0000);           //Timer3, duty cycle = 0
      put_hvalue(FTM2GR, ((uint)(speed<<8)|0x00ff)); //Timer2, duty cycle = speed
    }
    else if(rotation==REVERSE){
      put_hvalue(FTM2GR, 0x0000);             //Timer2, duty cycle = 0
      put_hvalue(FTM3GR, ((uint)(speed<<8)|0x00ff));   //Timer3, duty cycle = speed
    }
  }
  else if(number==2){
    if(rotation==FORWARD){
      put_hvalue(FTM5GR, 0x0000);           //Timer5, duty cycle = 0
      put_hvalue(FTM4GR, ((uint)(speed<<8)|0x00ff)); //Timer4, duty cycle = speed
    }
    else if(rotation==REVERSE){
      put_hvalue(FTM4GR, 0x0000);             //Timer4, duty cycle = 0
      put_hvalue(FTM5GR, ((uint)(speed<<8)|0x00ff));   //Timer5, duty cycle = speed
    }
  }
#endif
}

/**
 * @brief Stop a specified motor
 *
 * @param [in] number ID/Number of the motor
 */
void stop_motor(uint8_t number)
{
#if (PCB_RELEASE == LLC2_3) || (PCB_RELEASE == LLC2_4c)
#ifdef DRIVER_ST
if(number==1){
    PWM_MCC1_SET;
//    put_hvalue(FTM2GR, 0x0000); //Timer2, duty cycle = 0
  }
  else if(number==2){
    PWM_MCC2_SET;
//    put_hvalue(FTM3GR, 0x0000); //Timer3, duty cycle = 0
  }
#else
#ifndef MOTOR_SPEED_CONTROL
    PWM_MCC1_CLEAR;
    PWM_MCC2_CLEAR;
    PWM_MCC3_CLEAR;
    PWM_MCC4_CLEAR;
#else
    if(number==1){
      put_hvalue(FTM2GR, 0x0000); //Timer2, duty cycle = 0
      put_hvalue(FTM3GR, 0x0000); //Timer3, duty cycle = 0
    }
    else if(number==2){
      put_hvalue(FTM4GR, 0x0000); //Timer4, duty cycle = 0
      put_hvalue(FTM5GR, 0x0000); //Timer5, duty cycle = 0
    }
#endif
#endif
#elif PCB_RELEASE == LLC2_2
  if(number==1){
    put_hvalue(FTM2GR, 0x0000); //Timer2, duty cycle = 0
    put_hvalue(FTM3GR, 0x0000); //Timer3, duty cycle = 0
  }
  else if(number==2){
    put_hvalue(FTM4GR, 0x0000); //Timer4, duty cycle = 0
    put_hvalue(FTM5GR, 0x0000); //Timer5, duty cycle = 0
  }
#endif
}

/**
 * @brief Get the position of a specified motor (pulse counter timer on 16bits)
 *
 * @param [in] number ID/Number of the motor
 *
 * @return Position on 16bits
 */
uint16_t get_motor_position(uint8_t number)
{
  if(number==1){
    return (get_hvalue(FTM0GR));
  }
  else if(number==2){
    return (get_hvalue(FTM1GR));
  }
  return 0;
}
