/**
 * @file delay.c
 * @author Oki Electric Industry Co., LTD. - 2005 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief Delay utils
 */
#include "ml674061.h"
#include "common.h"

#include "utils/delay.h"

/**
 * @brief Delay routine in ms which uses hardware timer
 * @param [in] cmpt_ms Number of ms in 16bits => 65,535sec max
 */
void DelayMs(uint16_t cmpt_ms)
{
  uint32_t t=counter_timer;
  while(cmpt_ms>(counter_timer-t))
    CLR_WDT;
}
