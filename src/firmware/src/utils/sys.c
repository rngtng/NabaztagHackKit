/**
 * @file sys.c
 * @author RedoX <dev@redox.ws> - 2015
 * @date 2015/09/07
 * @brief Misc stuff for GCC port
 */
#include <stdio.h>
#include <stdint.h>

extern int32_t __heap_start__;
extern int32_t __heap_end__;

extern void *_sbrk(int32_t incr)
{
    static uint8_t *heap = NULL;
    uint8_t *prev_heap;

    if (heap == NULL) {
        heap = (uint8_t *)&__heap_start__;
    }
    prev_heap = heap;

    if ((heap + incr) >= (uint8_t *)&__heap_end__) {
        return 0;
    }
    heap += incr;
    return (void *)prev_heap;
}

extern void _port_disable_thumb(void);
extern void _port_enable_thumb(void);

void __disable_interrupt(void)
{
  asm volatile ("swi 1");   // Disable IRQ
  //~ asm volatile ("swi 3");   // Disable FIQ
}

void __enable_interrupt(void)
{
  asm volatile ("swi 0");   // Enable IRQ
  //~ asm volatile ("swi 2");   // Enable FIQ
}
