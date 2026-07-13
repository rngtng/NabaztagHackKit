/**
 * @file hcd.c
 * @author Oki Electric Industry Co.,Ltd. - 2003 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief USB Host
 */
#include <string.h>
#include <stdio.h>
#include "ml674061.h"
#include "common.h"

#include "utils/debug.h"
#include "utils/delay.h"
#include "utils/mem.h"

#include "usb/hcd.h"
#include "usb/hcdmem.h"

#define RevisonNumber  0x00000010
#define TD_BUFFER_MAX  4096

hcd_info_t hcd_info;

void usbhost_interrupt(void);

/**
 * @brief Init the HCD
 *
 * @retval  -1  on error
 * @retval  0   on success
 */
int8_t hcd_init(void)
{
  uint32_t rev;
  uint32_t fminterval;
  uint32_t mask;

  #ifdef DEBUG_USB
  sprintf(dbg_buffer,"HCD: Controller Address = %08X"EOL,HostCtl_addr);
  DBG_USB(dbg_buffer);
  #endif

  //Check chip revision
  rev = get_wvalue(HcRevision);
  if(rev != RevisonNumber)
  {
    #ifdef DEBUG_USB
    sprintf(dbg_buffer,"Bad Revision Register : %08lX"EOL, rev);
    DBG_USB(dbg_buffer);
    #endif
    return -1;
  }

  /* Embedded RAM address setup register */
  put_wvalue(RamAdr, ComRAMAddr);

  /* Set default address for allocation in RAM of USB chip */
  hcd_malloc_init(ComRAMAddr, ComRAMSize, 16, COMRAM);

  /* Init hcd_info */
  memset(&hcd_info, 0, sizeof(hcd_info_t));

  /* Set RAM for HCCA and send address to USB chip */
  hcd_info.hcca = (hcca_t *)hcd_malloc(sizeof(hcca_t), COMRAM,2);
  if (!hcd_info.hcca)
  {
    return -1;
  }
  memset(hcd_info.hcca, 0, sizeof(hcca_t));
  put_wvalue(HcHCCA, (uintptr_t)hcd_info.hcca);

  /* Set addres of the control ED */
  put_wvalue(HcControlHeadED, 0);
  /* Set addres of the bulk ED */
  put_wvalue(HcBulkHeadED, 0);

  /* Set SOF frame interval */
  fminterval = 0x2edf;
  put_wvalue(HcPeriodicStart,(fminterval * 9) / 10);
  fminterval |= ((((fminterval - MAXIMUM_OVERHEAD) * 6) / 7) << 16);
  put_wvalue(HcFmInterval, fminterval);
  put_wvalue(HcLSThreshold, 0x628);

  INIT_LIST_ENTRY(&hcd_info.ed_control);
  INIT_LIST_ENTRY(&hcd_info.ed_bulk);
  INIT_LIST_ENTRY(&hcd_info.ed_pause0);
  INIT_LIST_ENTRY(&hcd_info.ed_pause1);

  hcd_info.hc_control = OHCI_CTRL_PLE | OHCI_USB_OPER;
  put_wvalue(HcControl, hcd_info.hc_control);

  /* Config interrupt Enable */
  mask = OHCI_INTR_MIE | OHCI_INTR_UE |
         OHCI_INTR_WDH | OHCI_INTR_RHSC | OHCI_INTR_SO;
  put_wvalue(HcInterruptEnable, mask);
  put_wvalue(HostCtl, 0x0);

  /* here comes the first interrupt ! after initialisation */
  put_wvalue(HcRhStatus, RH_HS_LPSC);

  return 0;
}

/**
 * @brief Process the HCD events
 *
 * @return Status
 */
uint8_t hcd_rh_events(void)
{
  if(hcd_info.rh_status & HcdRH_DISCONNECT)
  {
    hcd_info.rh_status &= ~HcdRH_DISCONNECT;

    DBG_USB("HCD: disconnected the device."EOL);
    usbh_disconnect(&hcd_info.rh_device);
    hcd_info.rh_device = NULL;

    return HcdRH_DISCONNECT;
  }
  else if(hcd_info.rh_status & HcdRH_CONNECT)
  {
    DelayMs(40);
    hcd_info.rh_status &= ~HcdRH_CONNECT;

    DBG_USB(" HCD: connected a new device."EOL);
    hcd_info.rh_device = usbh_connect(((hcd_info.rh_status & HcdRH_SPEED)?1:0));

    return HcdRH_CONNECT;
  }
  return 0;
}

#ifdef DEBUG_USB

/**
 * @brief Get a string corresponding to the "cc" param
 * @param [in]  cc  FIXME
 * @return  pointer to the string
 */
static char *hcd_cc_string(uint32_t cc)
{
  char *cc_s;

  switch(cc){
        case CC_NOERROR        : cc_s = "NOERROR";        break;
        case CC_CRC            : cc_s = "CRC";          break;
        case CC_BITSTUFFING    : cc_s = "BITSTUFFING";      break;
        case CC_TOGGLEMISMATCH : cc_s = "DATATOGGLEMISMATCH ";  break;
        case CC_STALL          : cc_s = "STALL";         break;
        case CC_NOTRESPONDING  : cc_s = "DEVICENOTRESPONDING";  break;
        case CC_PIDCHECKFAILURE: cc_s = "PIDCHECKFAILURE";    break;
        case CC_UNEXPECTEDPID  : cc_s = "UNEXPECTEDPID";    break;
        case CC_DATAOVERRUN    : cc_s = "DATAOVERRUN";      break;
        case CC_DATAUNDERRUN   : cc_s = "DATAUNDERRUN";      break;
        case CC_BUFFEROVERRUN  : cc_s = "BUFFEROVERRUN";    break;
        case CC_BUFFERUNDERRUN : cc_s = "BUFFERUNDERRUN ";    break;
        case CC_NOTACCESSED    : cc_s = "NOTACCESSED";      break;
        default                : cc_s = "UNKNOWN CODE";         break;
  }
  return cc_s;
}
#endif


/**
 * @brief Delete the Transfer descriptors
 *
 * @note Must be called with OHCI IRQ masked
 *
 * @param [in]  ed Endpoint descriptor struct
 */
static void hcd_delete_td(PHCD_ED ed)
{
  PURB urb;
  PHCD_TD td_next, td_prev;
  uintptr_t HeadP = ed->HcED.HeadP;
  uintptr_t TailP = ed->HcED.TailP;

  td_next = (PHCD_TD)(HeadP & 0xFFFFFFF0);
  while(td_next != (PHCD_TD)TailP)
  {
     urb = td_next->urb;
    if(urb)
    {
      list_del(&td_next->list);
      if(urb->status>=0)
      {
        DBG_USB("ABORT 1"EOL);
        urb->status = URB_ABORT;
      }
    }
    td_prev  = td_next;
    td_next = (PHCD_TD)td_prev->HcTD.NextTD;
    hcd_free(td_prev);
  }
  ed->HcED.HeadP = (HeadP & 0xF) | TailP;
}

/**
 * @brief
 *
 * @note Must be called with OHCI IRQ masked
 *
 * @param ed      Endpoint descriptor struct
 * @param control FIXME
 * @param buffer  FIXME
 * @param length  FIXME
 * @param urb     FIXME
 * @param index   FIXME
 *
 * @retval  -1  on error
 * @retval  0   on success
 */
static int8_t hcd_add_td(PHCD_ED ed, uint32_t control,
                         void *buffer, uint32_t length,
                         PURB urb, uint8_t index)
{
  PHCD_TD td_tail;
  PHCD_TD td_next;

  td_tail = (PHCD_TD)ed->HcED.TailP;
  if(!td_tail)
  {
    return -1;
  }

  td_next = (PHCD_TD)(td_tail->HcTD.NextTD);
  while(td_next)
  {
    td_tail = td_next;
    td_next = (PHCD_TD)(td_tail ->HcTD.NextTD);
  }

  td_next = (PHCD_TD)hcd_malloc(sizeof(HCD_TD), COMRAM,3);  /* new TD */
  if (!td_next)
  {
    return -1;
  }
  memset(td_next, 0, sizeof(HCD_TD));

  td_tail->HcTD.Control = control;
  td_tail->HcTD.CBP = (uintptr_t)((!buffer || !length) ? 0: buffer);
  td_tail->HcTD.NextTD = (uintptr_t)td_next;
  td_tail->HcTD.BE = (uintptr_t)((!buffer || !length ) ? 0: (uintptr_t)buffer + length - 1);
  td_tail->urb = urb;
  td_tail->index = index;

  ed->HcED.TailP = (uintptr_t)td_next;

  list_add(&td_tail->list, &urb->td_list);

  return 0;
}

/**
 * @brief Remove an endpoint from the "active list"
 *
 * @param ed  Endpoint struct pointer
 */
static void hcd_unlink_ed(PHCD_ED ed)
{
  PHCD_ED ed_pre;

  if(ed->type == USB_CTRL)
  {
    if(ed->ed_list.Blink == &hcd_info.ed_control)
    {
      put_wvalue(HcControlHeadED, ed->HcED.NextED);
    }
    else
    {
      ed_pre = list_struct_entry(ed->ed_list.Blink, HCD_ED, ed_list);
      ed_pre->HcED.NextED = ed->HcED.NextED;
    }

    list_del(&ed->ed_list);

    if(list_empty(&hcd_info.ed_bulk))
    {
      hcd_info.hc_control &= ~OHCI_CTRL_CLE;
      put_wvalue(HcControl, hcd_info.hc_control);
    }
  }
  else if(ed->type == USB_BULK)
  {
    if(ed->ed_list.Blink == &hcd_info.ed_bulk)
    {
      put_wvalue(HcBulkHeadED, ed->HcED.NextED);
    }
    else
    {
      ed_pre = list_struct_entry(ed->ed_list.Blink, HCD_ED, ed_list);
      ed_pre->HcED.NextED = ed->HcED.NextED;
    }

    list_del(&ed->ed_list);

    if(list_empty(&hcd_info.ed_bulk))
    {
      hcd_info.hc_control &= ~OHCI_CTRL_BLE;
      put_wvalue(HcControl, hcd_info.hc_control);
    }
  }

  ed->HcED.NextED = 0;
  ed->flag = ED_UNLINK;
}


/**
 * @brief "Pause" and endpoint
 * @param ed  Endpoint struct pointer
 */
static void hcd_pause_ed(PHCD_ED ed)
{
  uint32_t TailP;

  if(ed->HcED.Control & HcED_SKIP)
  {
    TailP = ed->HcED.TailP;
    if((ed->HcED.HeadP & 0xFFFFFFF0) != TailP)
      hcd_delete_td(ed);

    hcd_unlink_ed(ed);
  }
  else
  {
    TailP = ed->HcED.TailP;
    if((ed->HcED.HeadP & 0xFFFFFFF0) == TailP)
    {
      ed->HcED.Control |= HcED_SKIP;
      hcd_unlink_ed(ed);
    }
    else
    {
      ed->flag = ED_PAUSE;
      ed->HcED.Control |= HcED_SKIP;

      list_add(&ed->ed_list, &hcd_info.ed_pause0);

      put_wvalue(HcInterruptEnable, OHCI_INTR_SF);
    }
  }
}


/**
 * @brief Delete an endpoint
 *
 * @note Must be called with OHCI IRQ masked
 *
 * @param ed Endpoint struct pointer
 */
void hcd_delete_ed(PHCD_ED ed)
{
  uint8_t timeout = 0;

  #ifdef DEBUG_USB
  DBG_USB(" hcd_delete_ed:"EOL);
  #endif

  if(ed->flag == ED_LINK)
    hcd_pause_ed(ed);

  while(ed->flag == ED_LINK || ed->flag == ED_PAUSE)
  {
    DelayMs(1);
    if(timeout++ > 100)
    {
      DBG_USB(" HCD: hcd_delete_ed timeout!!"EOL);
      return;
    }
  }

  hcd_free((uintptr_t *)ed->HcED.TailP);
  hcd_free(ed);
}

/**
 * @brief Update an endpoint
 *
 * @note Must be called with OHCI IRQ masked
 *
 * @param ed Endpoint struct pointer
 */
int8_t hcd_update_ed(PHCD_ED ed, uint8_t dev_addr, uint16_t maxpacket)
{
  uint32_t control;

#ifdef DEBUG_USB
  DBG_USB(" hcd_update_ed:"EOL);
#endif

  if(ed->HcED.Control & HcED_SKIP)
  {
    control = ed->HcED.Control & (HcED_FA | HcED_EN | HcED_SPEED | HcED_SKIP);
    ed->HcED.Control = control | ((uint32_t)maxpacket << 16) | dev_addr;
    return 0;
  }
  return -1;
}

/**
 * @brief Create an endpoint
 *
 * @note Must be called with OHCI IRQ masked
 *
 * @param speed     USB speed
 * @param dev_addr  Device address
 * @param type      Device Type
 * @param ep_num    Endpoint number
 * @param maxpacket Max packet length
 * @param interval  Interval
 *
 * @return Endpoint struct pointer
 */
PHCD_ED hcd_create_ed(uint8_t speed, uint8_t dev_addr,
                      uint8_t type, uint8_t ep_num,
                      uint16_t maxpacket, uint8_t interval)
{
  PHCD_ED ed;
  PHCD_TD td;
  uint32_t dir;

  DBG_USB("hcd_create_ed"EOL);

  ed = (PHCD_ED)hcd_malloc(sizeof(HCD_ED), COMRAM,4);
  if (!ed)
  {
    return NULL;
  }

  td = (PHCD_TD)hcd_malloc(sizeof(HCD_TD), COMRAM,5);  /* dumy TD */
  if (!td)
  {
    hcd_free(ed);
    return NULL;
  }
  memset(td, 0, sizeof(HCD_TD));

  dir = (uint32_t)((type == USB_CTRL) ? 0:
                          ((ep_num & USB_DIR_IN) ? HcED_DIR_IN : HcED_DIR_OUT));
  ed->HcED.Control =  ((uint32_t)maxpacket << 16) |
                      ((uint32_t)speed << 13 ) |
                      ((uint32_t)ep_num << 7)| dir |
                      ((uintptr_t)dev_addr & 0x7Fl) | HcED_SKIP;
  ed->HcED.TailP = (uintptr_t)td;
  ed->HcED.HeadP = (uintptr_t)td;
  ed->HcED.NextED = 0;
  ed->type = type;
  ed->interval = interval;
  ed->flag = ED_IDLE;

  return ed;
}

void hcd_transfer_wait(PURB urb)
{
  uint16_t timeout = 0;

  DBG_USB("hcd_transfer_wait:"EOL);
  while(urb->status == URB_PENDING)
  {
    DelayMs(1);
    if(timeout++ > urb->timeout)
    {
      DBG_USB("HCD: hcd_transfer_wait timeout!!"EOL);
      hcd_pause_ed((PHCD_ED)urb->ed);
      urb->status = URB_TIMEOUT;
      return;
    }
  }

  if(!list_empty(&urb->td_list))
    urb->status = URB_SYSERR;
#ifdef DEBUG_RAMAC_ERR
      hcd_info.hc_control |= OHCI_CTRL_PLE;
      put_wvalue(HcControl, hcd_info.hc_control);
#endif
  DBG_USB("hcd_transfer_wait: done."EOL);
}

void hcd_control_transfer_start(PURB urb)
{
  uint32_t control;
  PHCD_ED ed = (PHCD_ED)urb->ed;
  PHCD_ED ed_list_tail;

  DBG_USB("hcd_control_transfer_start:"EOL);

  urb->result = 0;
  urb->status = URB_PENDING;
  INIT_LIST_ENTRY(&urb->td_list);

  disable_ohci_irq();

  control = HcTD_CC | HcTD_DP_SETUP | HcTD_T_DATA0 | HcTD_DI;
  if(hcd_add_td(ed, control, urb->setup, 8, urb, TD_SETUP)<0)
  {
    urb->status = URB_ADDTDERR;
    return;
  }

  if (urb->length > 0)
  {
    if(urb->setup->bmRequestType & USB_DIR_IN)
      control = HcTD_CC | HcTD_R | HcTD_DP_IN  | HcTD_T_DATA1 | HcTD_DI;
    else
      control = HcTD_CC | HcTD_R | HcTD_DP_OUT | HcTD_T_DATA1 | HcTD_DI;
    if(hcd_add_td(ed, control, urb->buffer, urb->length, urb, TD_DATA)<0)
    {
      urb->status = URB_ADDTDERR;
      return;
    }
  }

  if(urb->setup->bmRequestType & USB_DIR_IN)
    control = HcTD_CC | HcTD_DP_OUT | HcTD_T_DATA1;
  else
    control = HcTD_CC | HcTD_DP_IN  | HcTD_T_DATA1;
  if(hcd_add_td(ed, control, NULL, 0, urb, TD_STATUS) < 0)
  {
    urb->status = URB_ADDTDERR;
    return;
  }

  if(ed->flag == ED_IDLE || ed->flag == ED_UNLINK)
  {
    if(list_empty(&hcd_info.ed_control))
    {
      put_wvalue(HcControlHeadED, (uintptr_t)ed);
    }
    else
    {
      ed_list_tail = (PHCD_ED)get_wvalue(HcControlHeadED);
      while(ed_list_tail->HcED.NextED)
      {
        ed_list_tail = (PHCD_ED)ed_list_tail->HcED.NextED;
      }
      ed_list_tail->HcED.NextED = (uintptr_t)ed;
    }
    ed->HcED.NextED = 0;
    ed->flag = ED_LINK;

    list_add(&ed->ed_list, &hcd_info.ed_control);
  }

  if(ed->HcED.HeadP & HcED_HeadP_HALT)
  {
    ed->HcED.HeadP &= (uintptr_t)(~HcED_HeadP_HALT);
  }

  if(ed->HcED.Control & HcED_SKIP)
  {
    ed->HcED.Control &= (uintptr_t)(~HcED_SKIP);
  }

  if( (hcd_info.hc_control & OHCI_CTRL_CLE) == 0 )
  {
    hcd_info.hc_control |= OHCI_CTRL_CLE;
    put_wvalue(HcControl, hcd_info.hc_control);
  }

  enable_ohci_irq();

  put_wvalue(HcCommandStatus, OHCI_CLF);
}

void hcd_control_transfer(PURB urb)
{
  struct usb_setup *setup, *setup_saved;
  uintptr_t *buffer, *buffer_saved;

  if(hcd_malloc_check(urb->setup) != COMRAM)
  {
    disable_ohci_irq();
    setup = (struct usb_setup *)hcd_malloc(sizeof(struct usb_setup), COMRAM,6);
    enable_ohci_irq();
    if(!setup){
      urb->status = URB_NOMEM;
      return;
    }
    memcpy(setup,urb->setup, 8);
    setup_saved = urb->setup;
    urb->setup = setup;
  }
  else
  {
    setup = NULL;
    setup_saved = NULL;
  }

  if(urb->setup->wLength > 0 && (hcd_malloc_check(urb->buffer) != COMRAM))
  {
    disable_ohci_irq();
    buffer = hcd_malloc(urb->length, COMRAM,7);
    enable_ohci_irq();
    if (!buffer){
      urb->status = URB_NOMEM;
      return;
    }
    buffer_saved = urb->buffer;
    if((urb->setup->bmRequestType & USB_DIR_IN) == 0)
      memcpy(buffer, buffer_saved, urb->length);
    urb->buffer = buffer;
  }
  else
  {
    buffer = NULL;
    buffer_saved = NULL;
  }

  hcd_control_transfer_start(urb);

  if(urb->timeout || setup || buffer)
    hcd_transfer_wait(urb);

  if(setup)
  {
    urb->setup = setup_saved;
    disable_ohci_irq();
    hcd_free(setup);
    enable_ohci_irq();
  }

  if(buffer)
  {
    urb->buffer = buffer_saved;
    if(urb->setup->bmRequestType & USB_DIR_IN)
      memcpy(buffer_saved, buffer, urb->length);
    disable_ohci_irq();
    hcd_free(buffer);
    enable_ohci_irq();
  }
}

void hcd_bulk_transfer_start(PURB urb)
{
  PHCD_ED ed = (PHCD_ED)urb->ed;
  PHCD_ED ed_list_tail;
  uint8_t *buffer = (uint8_t *)urb->buffer;
  uint32_t length = urb->length;
  uint32_t cnt = 0;

  DBG_USB(" hcd_bulk_transfer_start:"EOL);
  urb->result = 0;
  urb->status = URB_PENDING;
  INIT_LIST_ENTRY(&urb->td_list);

  disable_ohci_irq();

  while(length > TD_BUFFER_MAX)
  {
    if(hcd_add_td(ed, HcTD_CC | HcTD_DI, buffer, TD_BUFFER_MAX, urb, cnt) < 0)
    {
      urb->status = URB_ADDTDERR;
      DBG_USB("x0");
      return;
    }
    buffer += TD_BUFFER_MAX;
    length -= TD_BUFFER_MAX;
    cnt++;
  }

  if(hcd_add_td(ed, HcTD_CC, buffer, length, urb, cnt) < 0)
  {
    urb->status = URB_ADDTDERR;
    DBG_USB("x1");
    return;
  }

  if(ed->flag == ED_IDLE || ed->flag == ED_UNLINK)
  {
    if(list_empty(&hcd_info.ed_bulk))
    {
      put_wvalue(HcBulkHeadED, (uintptr_t)ed);
    }
    else
    {
      ed_list_tail = (PHCD_ED)get_wvalue(HcBulkHeadED);
      while(ed_list_tail->HcED.NextED)
      {
        ed_list_tail = (PHCD_ED)ed_list_tail->HcED.NextED;
      }
      ed_list_tail->HcED.NextED = (uintptr_t)ed;
    }
    ed->HcED.NextED = 0;
    ed->flag = ED_LINK;

    list_add(&ed->ed_list, &hcd_info.ed_bulk);
  }

  if(ed->HcED.HeadP & HcED_HeadP_HALT)
  {
    ed->HcED.HeadP &= (uintptr_t)(~HcED_HeadP_HALT);
  }

  if(ed->HcED.Control & HcED_SKIP)
  {
    ed->HcED.Control &= (uintptr_t)(~HcED_SKIP);
  }

  if((hcd_info.hc_control & OHCI_CTRL_BLE) == 0)
  {
    hcd_info.hc_control |= OHCI_CTRL_BLE;
    put_wvalue(HcControl, hcd_info.hc_control);
  }

  enable_ohci_irq();

  put_wvalue(HcCommandStatus, OHCI_BLF);
}

void hcd_bulk_transfer(PURB urb)
{
  if(hcd_malloc_check(urb->buffer) != COMRAM)
  {
    urb->status = URB_ADDTDERR;
    DBG_USB("x2");
    return;
  }
  hcd_bulk_transfer_start(urb);
  if(urb->timeout)
    hcd_transfer_wait(urb);
}

int8_t hcd_transfer_request(PURB urb)
{
  DBG_USB(" hcd_transfer_request:");

  if (hcd_info.disabled)
  {
    DBG_USB("ABORT 2"EOL);
    urb->status = URB_ABORT;
    return urb->status;
  }

  switch(((PHCD_ED)urb->ed)->type)
  {
    case USB_CTRL:
//      DBG_USB("Ctrl"EOL);
      #ifdef DEBUG_RAMAC_ERR
      hcd_info.hc_control &= ~OHCI_CTRL_PLE;
      put_wvalue(HcControl, hcd_info.hc_control);
      #endif
      hcd_control_transfer(urb);
      break;

    case USB_BULK:
//      DBG_USB("Bulk"EOL);
      hcd_bulk_transfer(urb);
      break;

    default:
      DBG_USB("ABORT 3"EOL);
      urb->status = URB_ABORT;
  }
  return urb->status;
}

PHCD_TD hcd_get_done_list(void)
{
  PHCD_TD td_done, td_next, td_temp;

  td_done = NULL;
  td_next = (PHCD_TD)((hcd_info.hcca->donehead) & 0xfffffff0);
  hcd_info.hcca->donehead = 0;

  while (td_next)
  {

  #ifdef DEBUG_USB
    sprintf(dbg_buffer,"HCD: Done TD = %08lX"EOL, (uintptr_t)td_next);
    DBG_USB(dbg_buffer);
  #endif

    if( hcd_malloc_check((uintptr_t *)td_next) != COMRAM)
    {
      DBG_USB(EOL"HCD: DoneHead is bad address!!"EOL);
      break;
    }

    td_temp = td_done;
    td_done = td_next;
    td_next = (PHCD_TD)(td_next->HcTD.NextTD & 0xFFFFFFF0);
    td_done->HcTD.NextTD = (uintptr_t)td_temp;
  }
  return td_done;
}

uintptr_t hcd_transfer_count(PHCD_TD td, void *buffer)
{
  if (td->HcTD.CBP == 0 && td->HcTD.BE != 0)
  {
    return (uintptr_t)td->HcTD.BE - (uintptr_t)buffer + 1;
  }
  else
  {
    return (uintptr_t)td->HcTD.CBP - (uintptr_t)buffer;
  }
}

/* Must be called with OHCI IRQ masked */
PHCD_TD hcd_control_transfer_done(PHCD_TD td)
{
  PURB urb = td->urb;
  PHCD_ED ed = (PHCD_ED)urb->ed;
  PHCD_TD td_next;
  uint32_t cc;

  if(td->index == TD_DATA)
  {
    urb->result = hcd_transfer_count(td, urb->buffer);
  }
  else if(td->index == TD_STATUS)
  {
    urb->status = (int)urb->result;
    hcd_pause_ed(ed);
  }

  cc = (td->HcTD.Control >> 28);
  if(cc)
  {
    #ifdef DEBUG_USB
    sprintf(dbg_buffer,EOL"HCD: USB-error/status: %02lX(%s): %08lX"EOL,
            cc, hcd_cc_string(cc), (uint32_t)td);
    DBG_USB(dbg_buffer);
    #endif
    urb->result = cc;
    urb->status = URB_XFERERR;
    if( ((ed->HcED.HeadP & HcED_HeadP_HALT) != 0) &&
        ((ed->HcED.HeadP & 0xFFFFFFF0) != 0) )
    {
      hcd_delete_td(ed);
      hcd_pause_ed(ed);
    }
  }

  td_next = (PHCD_TD)td->HcTD.NextTD;
  list_del(&td->list);
  hcd_free(td);

  return td_next;
}

/* Must be called with OHCI IRQ masked */
PHCD_TD hcd_bulk_transfer_done(PHCD_TD td)
{
  PURB urb = td->urb;
  PHCD_ED ed = (PHCD_ED)urb->ed;
  PHCD_TD td_next;
  uint32_t cc;

  urb->result = hcd_transfer_count(td, urb->buffer);

  cc = (td->HcTD.Control >> 28);
  if((cc != 0) && (cc != CC_DATAOVERRUN) && (cc != CC_DATAUNDERRUN))
  {
    #ifdef DEBUG_USB
    sprintf(dbg_buffer,EOL"HCD: USB-error/status: %02lX: %08X"EOL,
            cc, HostCtl_addr);
    DBG_USB(dbg_buffer);
    #endif

    urb->result = cc;
    urb->status = URB_XFERERR;
    if( ((ed->HcED.HeadP & HcED_HeadP_HALT) != 0) &&
        ((ed->HcED.HeadP & 0xFFFFFFF0) != 0) )
    {
      hcd_delete_td((PHCD_ED)ed);
      hcd_pause_ed(ed);
    }
  }
  urb->status = (int8_t)urb->result;

  #ifdef BULK_UNLINK
  hcd_pause_ed(ed);
  #endif

  if(urb->callback)
    urb->callback(urb);

  td_next = (PHCD_TD)td->HcTD.NextTD;
  list_del(&td->list);
  hcd_free(td);

  return td_next;
}

void hcd_writeback_done(void)
{
  PURB  urb;
  PHCD_TD td_list, td_next;
  PHCD_ED ed;

  td_list = hcd_get_done_list();

  while(td_list)
  {
    urb = td_list->urb;
    ed = (PHCD_ED)urb->ed;
    if(ed->type == USB_CTRL)
    {
      td_next = hcd_control_transfer_done(td_list);
    }
    else if(ed->type == USB_BULK)
    {
      td_next = hcd_bulk_transfer_done(td_list);
    }
    else
    {
      #ifdef DEBUG_USB
      sprintf(dbg_buffer,EOL"Illegal Type! %x"EOL, ed->type);
      DBG_USB(dbg_buffer);
      #endif
      return;
    }
    td_list = td_next;
  }
}

void hcd_rh_irq(void)
{
  uint32_t hub_status;
  uint32_t port_status;

  hub_status = get_wvalue(HcRhStatus);

  if(hub_status & RH_HS_OCIC)
  {
    DBG_USB(" root hub: Overcurrent Indicator Changed."EOL);
     DBG_USB(" root hub: please debug this code."EOL);
    return;
  }
  else if(hub_status)
  {
     DBG_USB(" root hub: please debug this code."EOL);
    return;
  }

  port_status = get_wvalue(HcRhPortStatus);
  if(port_status & RH_PS_CSC)
  {
     DBG_USB(" root hub: Port Connect Status Changed."EOL);
    put_wvalue(HcRhPortStatus, RH_PS_CSC|RH_PS_PRS);
  }
  if(port_status & RH_PS_PRSC)
  {
     DBG_USB(" root hub: Port Reset Status Changed."EOL);
    put_wvalue(HcRhPortStatus, RH_PS_PRSC);
    if( ((port_status & RH_PS_PES) != 0) && ((port_status & RH_PS_CCS) != 0) )
    {
      DBG_USB(" root hub: Port Connect Status = 1"EOL);

      #ifdef DEBUG_USB
      sprintf(dbg_buffer," %s speed device connected."EOL,
              ((port_status & RH_PS_LSDA) ? "low" : "full"));
      DBG_USB(dbg_buffer);
      #endif

      hcd_info.rh_status |= HcdRH_CONNECT;
      if(port_status & RH_PS_LSDA)
        hcd_info.rh_status |= HcdRH_SPEED;
      else
        hcd_info.rh_status &= ~HcdRH_SPEED;
    }
  }

  if(port_status & RH_PS_PESC)
  {
     DBG_USB(" root hub: Port Enable Status Changed."EOL);
    put_wvalue(HcRhPortStatus, RH_PS_PESC);
    if( ((port_status & RH_PS_PES) == 0) || ((port_status & RH_PS_CCS) == 0) )
    {
      DBG_USB(" root hub: Port Connect Status = 0"EOL);
      hcd_info.rh_status |= HcdRH_DISCONNECT;
    }
  }
  else if(port_status & RH_PS_PSSC)
  {
     DBG_USB(" root hub: Port Suspend Status Changed."EOL);
     DBG_USB(" root hub: please debug this code."EOL);
    /*writel_reg(HcRhPortStatus0, RH_PS_PSSC);*/
  }
  else if(port_status & RH_PS_OCIC)
  {
     DBG_USB(" root hub: Port Overcurrent Indicator Status Changed."EOL);
     DBG_USB(" root hub: please debug this code."EOL);
    put_wvalue(HcRhPortStatus, RH_PS_PRS);
    /*writel_reg(HcRhPortStatus, RH_PS_OCIC);*/
  }
  else
  {
    DBG_USB("hcd_rh_irq: nope");
  }
}

void hcd_ed_delete_list(void)
{
  PHCD_ED ed;

  while(!list_empty(&hcd_info.ed_pause1)){
    ed = list_struct_entry(hcd_info.ed_pause1.Flink, HCD_ED, ed_list);
    list_del(&ed->ed_list);
    hcd_pause_ed(ed);
  }

  while(!list_empty(&hcd_info.ed_pause0)){
    ed = list_struct_entry(hcd_info.ed_pause0.Flink, HCD_ED, ed_list);
    list_del(&ed->ed_list);
    list_add(&ed->ed_list, &hcd_info.ed_pause1);
  }

  if(!list_empty(&hcd_info.ed_pause1)){
    put_wvalue(HcInterruptEnable, OHCI_INTR_SF);
  }
}

void usbhost_interrupt(void)
{
  uint32_t status;

  status = get_wvalue(SttTrnsCnt);
  if(status & B_DMAIRQ) {
    DBG_USB("USB DMA interrupt"EOL);
    /* DMA not used */
    put_wvalue(SttTrnsCnt, B_DMAIRQ);
  } else if(status & B_OHCIIRQ) {
    if ((hcd_info.hcca->donehead != 0) &&
        ((hcd_info.hcca->donehead & 0x01) == 0) )
    {
      status = OHCI_INTR_WDH;
    } else
    {
      if((status =
          (get_wvalue(HcInterruptEnable) & get_wvalue(HcInterruptStatus))) == 0)
        return;
    }

#ifdef DEBUG_USB
    sprintf(dbg_buffer, "HCD: Interrupt %lX frame: %X"EOL,
            status, hcd_info.hcca->framenumber);
    DBG_USB(dbg_buffer);
#endif

    if(status & OHCI_INTR_UE) {
      hcd_info.disabled++;
      put_wvalue(HcInterruptDisable, 0xFFFFFFFF);
      DBG_USB(" HCD: Unrecoverable Error, controller disabled"EOL);
    }

    if(status & OHCI_INTR_WDH) {
      put_wvalue(HcInterruptDisable, OHCI_INTR_WDH);
      put_wvalue(HcInterruptStatus, OHCI_INTR_WDH);
      DBG_USB(" OHCI_INTR_WDH irq"EOL);
      hcd_writeback_done();
      put_wvalue(HcInterruptEnable, OHCI_INTR_WDH);
    }

    if(status & OHCI_INTR_RHSC) {
      put_wvalue(HcInterruptDisable, OHCI_INTR_RHSC);
      put_wvalue(HcInterruptStatus, OHCI_INTR_RHSC);
      DBG_USB(" OHCI_INTR_RHSC irq"EOL);
      hcd_rh_irq();
      put_wvalue(HcInterruptEnable, OHCI_INTR_RHSC);
    }

    if(status & OHCI_INTR_SO) {
      put_wvalue(HcInterruptStatus, OHCI_INTR_SO);
      DBG_USB(" USB Schedule overrun"EOL);
    }

    if(status & OHCI_INTR_SF) {
      put_wvalue(HcInterruptDisable, OHCI_INTR_SF);
      put_wvalue(HcInterruptStatus, OHCI_INTR_SF);
      DBG_USB(" OHCI_INTR_SF"EOL);
      hcd_ed_delete_list();
    }
    put_wvalue(SttTrnsCnt, B_OHCIIRQ);
  }
}

int8_t hcd_transfer_cancel(PURB urb)
{
  DBG_USB(" hcd_transfer_cancel:"EOL);

  if (hcd_info.disabled) {
    urb->status = URB_ABORT;
    return urb->status;
  }

  if(((PHCD_ED)urb->ed)->flag == ED_LINK){
    hcd_pause_ed((PHCD_ED)urb->ed);
  }
  return urb->status;
}
