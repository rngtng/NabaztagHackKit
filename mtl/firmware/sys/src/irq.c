/**
 * @file irq.c
 * @author Oki Electric Industry Co., LTD. - 2005 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief Manage the IRQ
 */
#include "ml674061.h"
#include "common.h"
#include "irq.h"

/**
 * @brief Table of IRQ handler
 *
 *    If interrupt of interrupt number N occurred,
 *      function of IRQ_HANDLER_TABLE[N] is called.
 */
pIRQ_HANDLER IRQ_HANDLER_TABLE[IRQSIZE];

/**
 * @brief Not defined interrupt handler
 */
void null_handler(void)
{
  return;
}

/**
 * @brief Initialize interrupt control registers (IRQ interrupt)
 */
void init_irq(void)
{
  uint8_t i;

  /* initialize IRQ registers */
  put_wvalue(ILC0, 0x0);  /* all interrupt level is set as 0 */
  put_wvalue(ILC1, 0x0);  /* all interrupt level is set as 0 */
  put_wvalue(EXILCA, 0x0);   /* all interrupt level is set as 0 */
  put_wvalue(EXILCB, 0x0);   /* all interrupt level is set as 0 */
  put_wvalue(EXILCC, 0x0);   /* all interrupt level is set as 0 */

  put_wvalue(FIQEN, 0x0);  /* FIQ is canceled */

  put_wvalue(IRQS, 0x0);  /* soft interrupt is canceled */

  put_wvalue(CIL, 0x0FE); /* Clear CIL register */

  /* Initialize IRQ handler table */
  for(i=0; i<IRQSIZE; i++){
      IRQ_HANDLER_TABLE[i] = null_handler; /* no interrupt handler is
                                                 defined yet */
  }
}
