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
 * @brief Enable/disable IRQ (needed by hal/i2c.c's bus-transaction critical
 * sections). ARMv4T Thumb code cannot touch CPSR's I-bit directly (no
 * CPSIE/CPSID until ARMv6), so these are ARM-mode functions that manipulate
 * CPSR directly (mrs/msr) rather than trapping through the SWI vector. These
 * matter once a timer/UART IRQ lands and could preempt an I2C transfer.
 */
__attribute__((target("arm")))
void __disable_interrupt(void)
{
  uint32_t tmp;
  asm volatile(
    "mrs %0, cpsr\n\t"
    "orr %0, %0, #0x80\n\t"
    "msr cpsr_c, %0"
    : "=r"(tmp) : : "memory");
}

__attribute__((target("arm")))
void __enable_interrupt(void)
{
  uint32_t tmp;
  asm volatile(
    "mrs %0, cpsr\n\t"
    "bic %0, %0, #0x80\n\t"
    "msr cpsr_c, %0"
    : "=r"(tmp) : : "memory");
}

/**
 * @brief Disable IRQ, returning the previous mask state (save/restore pair).
 *
 * Unlike the unconditional __enable/__disable above, this lets a critical
 * section nest without wrongly re-enabling interrupts that a caller had already
 * disabled. Used by the LED driver (#102) to fence its shadow-register writes
 * and SPI flush against the 1 ms timer IRQ that ticks the fade engine. Returns
 * the old CPSR I-bit (0 = interrupts were enabled); pass it to irq_restore.
 */
__attribute__((target("arm")))
uint32_t irq_disable_save(void)
{
  uint32_t cpsr;
  asm volatile(
    "mrs %0, cpsr\n\t"
    "orr r3, %0, #0x80\n\t"
    "msr cpsr_c, r3"
    : "=r"(cpsr) : : "r3", "memory");
  return cpsr & 0x80;
}

/**
 * @brief Restore the mask state captured by irq_disable_save().
 * @param [in] prev The value irq_disable_save() returned (old I-bit).
 */
__attribute__((target("arm")))
void irq_restore(uint32_t prev)
{
  if (!prev)
    __enable_interrupt();
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
