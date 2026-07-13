/**
 * @file delay.h
 * @author Oki Electric Industry Co., LTD. - 2005 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief Delay utils
 */
 #ifndef _DELAY_H_
#define _DELAY_H_

/* Overflow in ms = ( 16 x (65536-value of TMRLR) ) / (SystemClock x 1000) */
//    put_wvalue(TMRLR, 0xFFFE);  // set TMRLR for 0.9765625us @ 32.768MHz
//    put_wvalue(TMRLR, 0xFFEC);  // set TMRLR for 9.765625us @ 32.768MHz
//    put_wvalue(TMRLR, 0x0000);  // set TMRLR for 32ms @ 32.768MHz
//    put_wvalue(TMRLR, 0xF800);  // set TMRLR for 1ms @ 32.768MHz
    //Routine de delay x 1ms => boucle soft
    //routine de delay en us => modification directe de TMRLR


void DelayMs(uint16_t cmpt_ms);
extern volatile uint32_t counter_timer;
extern volatile uint32_t counter_timer_s;

#endif
