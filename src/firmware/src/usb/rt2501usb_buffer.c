/**
 * @file rt2501usb_buffer.c
 * @author Sebastien Bourdeauducq - 2006 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief RT2501 USB buffer management
 */
#include <stdio.h>
#include <string.h>

#include "common.h"
#include "utils/mem.h"
#include "utils/debug.h"
#include "utils/delay.h"

#include "usb/hcdmem.h"
#include "usb/hcd.h"
#include "usb/rt2501usb.h"            // rt2501_* stuff
#include "usb/rt2501usb_buffer.h"

#include "net/eapol.h"                // llc_* stuff
#include "net/ieee80211.h"            // ieee80211* stuff

static struct rt2501buffer *head, *queue;

/**
 * @brief Initialize the Buffer list for RT2501 tranfers
 */
void rt2501buffer_init(void)
{
  disable_ohci_irq();
  head = queue = NULL;
  enable_ohci_irq();
}

/**
 * @brief Flush the Buffer list, by reading it
 */
void rt2501buffer_free(void)
{
  disable_ohci_irq();
  while(rt2501_receive() != NULL);
  enable_ohci_irq();
}

/**
 * @brief Allocate and prepare a new buffer
 *
 * @param [in]  data        Input data pointer
 * @param [in]  length      Input data Length
 * @param [in]  source_mac  Source MAC address
 * @param [in]  dest_mac    Destination MAC address
 *
 * @retval  0 on error
 * @retval  1 on success
 */
uint8_t rt2501buffer_new(const uint8_t *data, uint32_t length,
                         const uint8_t *source_mac,
                         const uint8_t *dest_mac)
{
  struct rt2501buffer *newbuffer;

  disable_ohci_irq();
  newbuffer = (struct rt2501buffer *)
                      hcd_malloc(sizeof(struct rt2501buffer)+length, EXTRAM,19);
  if(newbuffer == NULL)
  {
    enable_ohci_irq();
    return 0;
  }
  newbuffer->next = NULL;
  newbuffer->length = length;
  memcpy(newbuffer->source_mac, source_mac, IEEE80211_ADDR_LEN);
  memcpy(newbuffer->dest_mac, dest_mac, IEEE80211_ADDR_LEN);
  memcpy(newbuffer->data, data, length);

  if(queue == NULL)
  {
    /* This is the first buffer */
    head = queue = newbuffer;
  }
  else
  {
    queue->next = newbuffer;
    queue = newbuffer;
  }
  enable_ohci_irq();
  return 1;
}

/**
 * @brief Receive (pulls from buffer list) a buffer item
 *
 * @return  Pointer to the buffer item
 */
struct rt2501buffer *rt2501_receive(void)
{
  struct rt2501buffer *r;

  while(1)
  {
    disable_ohci_irq();
    if(head == NULL)
    {
      enable_ohci_irq();
      return NULL;
    }
    r = head;
    head = head->next;
    if(head == NULL)
      queue = NULL;
    enable_ohci_irq();
/*
    sprintf(dbg_buffer,"rt2501_receive: %x"EOL, r);
    DBG_WIFI(dbg_buffer);
*/
    if((r->length < LLC_LENGTH)
       || (memcmp(r->data, eapol_llc, LLC_LENGTH) != 0))
      /* Not an EAPOL frame. Return it to the application. */
      return r;

    /* EAPOL frame, process it. */
    eapol_input(r->data, r->length);
    disable_ohci_irq();
    hcd_free(r);
    enable_ohci_irq();
  }
  return NULL;
}
