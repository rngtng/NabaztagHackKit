/**
 * @file stubs/ml60842.h
 * @brief Host stand-in for the ML60842 USB-host register map.
 *
 * Shadows sys/inc/ml60842.h (the stubs dir is first on the include path), for
 * the same reason stubs/ml674061.h shadows the SoC map: the real header puts
 * the USB block at the absolute address 0xF0000000, and `hcd.h`'s
 * disable_ohci_irq()/enable_ohci_irq() dereference it through common.h's
 * volatile casts - a segfault off-target, in code that is only masking an
 * interrupt that does not exist here.
 *
 * Exactly TWO names are needed to compile src/net/ieee80211.c against this
 * (checked with an empty stub: B_OHCIIRQ_MASK and HostCtl), so that is all it
 * models. Same rule as the sibling stub: a header mirroring the whole real one
 * would rot silently, this one fails to compile the moment something reaches
 * for a register it does not have.
 *
 * Nothing here models OHCI behaviour - masking is a write to a word nobody
 * reads. Any test that needs a live host controller belongs on hardware.
 */
#ifndef _ML60842_H_
#define _ML60842_H_

#include <stdint.h>

/* One word standing in for the whole USB register block. */
extern volatile uint32_t nab_usb_regs[4];

#define HostCtl          ((uintptr_t)&nab_usb_regs[0])
#define B_OHCIIRQ_MASK   0x00000002ul

#endif
