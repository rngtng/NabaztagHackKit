/**
 * @file irq.h
 * @author Oki Electric Industry Co., LTD. - 2005 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief Manage the IRQ
 */
#ifndef _IRQ_H_
#define _IRQ_H_

typedef void IRQ_HANDLER(void);
typedef IRQ_HANDLER *pIRQ_HANDLER;

void init_irq(void);

#define IRQSIZE 64

extern pIRQ_HANDLER IRQ_HANDLER_TABLE[IRQSIZE];

#endif
