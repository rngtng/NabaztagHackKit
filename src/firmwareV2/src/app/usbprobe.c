/**
 * @file usbprobe.c
 * @brief USB host bring-up probe (M11a, #143): prove the ML60842 OHCI stack
 *        port end-to-end - tick IRQ alive, controller alive, root-hub port
 *        connect, enumeration - and print the internal RT2501 dongle's
 *        device descriptor (expect VID:PID 148f:2573).
 *
 * Staged with an objective printed signal per step (hardware-debug doctrine:
 * register readbacks, not by-ear/by-eye):
 *   [1] init_irq + init_tick, busy-wait, counter_timer readback - the first
 *       live IRQ on firmwareV2; everything else depends on it (DelayMs, URB
 *       timeouts), so it gets a PASS/FAIL before USB is touched.
 *   [2] usbctrl_init(USB_HOST) + HcRevision readback (expect 0x10, OHCI 1.0).
 *   [3] usbhost_init (hcd_init: ComRAM allocator + HCCA + OHCI IRQs on) and
 *       the EXINT2 hookup (mirrors V1 main.c reg_irq_handler).
 *   [4] pump usbhost_events(); HcRhPortStatus printed once a second while
 *       waiting; on enumeration our match-all driver's connect() prints the
 *       descriptor.
 *
 * Run: task repl:firmwareV2:hw APP=usbprobe
 * Sim runs boot but see no device (sim stubs the ML60842 region, emulates no
 * OHCI) - hardware-only verification.
 */
#include "ml674061.h"
#include "ml60842.h"
#include "common.h"
#include "irq.h"

#include "utils/delay.h"
#include "utils/mem.h"

#include "usb/usbh.h"
#include "usb/usbctrl.h"

/* ---- semihosting console (M3 #91 path) ---------------------------------- */
#define SYS_WRITEC 0x03

static inline int semihost(int op, void *arg)
{
  register int r0 asm("r0") = op;
  register void *r1 asm("r1") = arg;
  asm volatile("svc #0xAB" : "+r"(r0) : "r"(r1) : "memory");
  return r0;
}

static void sh_puts(const char *s)
{
  while (*s) {
    char c = *s++;
    semihost(SYS_WRITEC, &c);
  }
}

static void sh_puthex32(uint32_t v)
{
  const char *hex = "0123456789abcdef";
  char b[9];
  int i;
  for (i = 7; i >= 0; i--) {
    b[i] = hex[v & 0xF];
    v >>= 4;
  }
  b[8] = '\0';
  sh_puts(b);
}

static void sh_puthex16(uint16_t v)
{
  const char *hex = "0123456789abcdef";
  char b[5] = {hex[(v >> 12) & 0xF], hex[(v >> 8) & 0xF],
               hex[(v >> 4) & 0xF], hex[v & 0xF], '\0'};
  sh_puts(b);
}

static void sh_putdec(uint32_t v)
{
  char b[11];
  int i = 10;
  b[i] = '\0';
  do {
    b[--i] = '0' + (v % 10);
    v /= 10;
  } while (v && i);
  sh_puts(&b[i]);
}

/* ---- [4] match-all driver: print whatever enumerates -------------------- */
static volatile int probe_dev_seen;

static void *probe_connect(PDEVINFO dev)
{
  sh_puts("[4] device enumerated: VID:PID ");
  sh_puthex16(dev->descriptor->idVendor);
  sh_puts(":");
  sh_puthex16(dev->descriptor->idProduct);
  sh_puts(" bcdDevice ");
  sh_puthex16(dev->descriptor->bcdDevice);
  sh_puts(" class ");
  sh_puthex16(dev->descriptor->bDeviceClass);
  /* V1's three accepted RT2501 module IDs (inc/usb/rt2501usb_hw.h):
   * 0df6:9712 Sitecom, 148f:2573 Ralink generic, 0db0:6877 MSI-branded */
  sh_puts((dev->descriptor->idVendor == 0x0df6 &&
           dev->descriptor->idProduct == 0x9712) ||
          (dev->descriptor->idVendor == 0x148f &&
           dev->descriptor->idProduct == 0x2573) ||
          (dev->descriptor->idVendor == 0x0db0 &&
           dev->descriptor->idProduct == 0x6877)
            ? " - RT2501 FOUND\n"
            : " - unexpected device\n");
  probe_dev_seen = 1;
  return dev; /* non-NULL: claim it, stop the driver search */
}

static void probe_disconnect(PDEVINFO dev)
{
  (void)dev;
  sh_puts("[4] device disconnected\n");
}

static struct usbh_driver probe_driver = {
  .name = "usbprobe",
  .connect = probe_connect,
  .disconnect = probe_disconnect,
};

int main(void)
{
  volatile uint32_t spin;
  uint32_t t0, last_report;
  int8_t ret;

  sh_puts("#143 USB host bring-up probe\n");

  /* [1] first live IRQ: the 1 ms tick. Busy-wait (NOT DelayMs - it would
   * hang forever if the IRQ is dead) and read the counter back. */
  init_irq();
  init_tick();
  for (spin = 0; spin < 2000000; spin++)
    CLR_WDT;
  sh_puts("[1] tick counter_timer=");
  sh_putdec(counter_timer);
  sh_puts(counter_timer ? " PASS\n" : " FAIL - IRQ dead, aborting\n");
  if (!counter_timer)
    goto done;

  /* [2] ML60842 up + revision readback */
  usbctrl_host_driver_set(NULL, usbhost_interrupt);
  ret = (int8_t)usbctrl_init(USB_HOST);
  sh_puts("[2] usbctrl_init ret=");
  sh_putdec((uint32_t)(ret == 0 ? 0 : 1));
  sh_puts(" HcRevision=");
  sh_puthex32(get_wvalue(HcRevision));
  sh_puts("\n");
  if (ret != 0) {
    sh_puts("[2] FAIL - ML60842 not responding, aborting\n");
    goto done;
  }

  /* [3] host core + allocator banks + EXINT2 hookup (V1 reg_irq_handler) */
  setup_usb_malloc();
  ret = usbhost_init();
  sh_puts("[3] usbhost_init ret=");
  sh_putdec((uint32_t)(ret == 0 ? 0 : 1));
  sh_puts("\n");
  if (ret != 0)
    goto done;

  IRQ_HANDLER_TABLE[INT_EXINT2] = usbctrl_interrupt;
  clr_hbit(EXIDM, IDM_IDM36 & IDM_IDMP36); /* low-level sensing */
  set_hbit(EXIRQB, IRQB_IRQ36);            /* clear stale EXINT2 */
  set_wbit(EXILCB, ILC_ILC36 & ILC_INT_LV7);

  usbh_driver_install(&probe_driver);

  /* [4] pump for up to 15 s; port status once a second while waiting */
  sh_puts("[4] pumping usbhost_events, HcRhPortStatus each second\n");
  t0 = counter_timer;
  last_report = t0;
  while (counter_timer - t0 < 15000) {
    usbhost_events();
    if (probe_dev_seen)
      break;
    if (counter_timer - last_report >= 1000) {
      last_report = counter_timer;
      sh_puts("    t=");
      sh_putdec((counter_timer - t0) / 1000);
      sh_puts("s HcRhPortStatus=");
      sh_puthex32(get_wvalue(HcRhPortStatus));
      sh_puts("\n");
    }
  }
  if (!probe_dev_seen)
    sh_puts("[4] TIMEOUT - no device enumerated in 15 s\n");

done:
  sh_puts("<<FV_DONE>>\n");
  for (;;)
    CLR_WDT;
}
