/**
 * @file list.c
 * @author Oki Electric Industry Co.,Ltd. - 2003 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief Linked list util
 */
#include "common.h"

#include "usb/list.h"
#include "utils/debug.h"
void __list_add(pLIST_ENTRY entry, pLIST_ENTRY Blink, pLIST_ENTRY Flink)
{
  entry->Blink = Blink;
  entry->Flink = Flink;
  Blink->Flink = entry;
  Flink->Blink = entry;
}

void __list_del(pLIST_ENTRY Blink, pLIST_ENTRY Flink)
{
  Flink->Blink = Blink;
  Blink->Flink = Flink;
}

void list_add_top(pLIST_ENTRY entry, pLIST_ENTRY head)
{
  __list_add(entry, head, head->Flink);
}

void list_add(pLIST_ENTRY entry, pLIST_ENTRY head)
{
//  sprintf(dbg_buffer,"+%X(%X)",head,entry); DBG(dbg_buffer);
  __list_add(entry, head->Blink, head);
}

void list_del(pLIST_ENTRY entry)
{
//  sprintf(dbg_buffer,"-%X"EOL,entry); DBG(dbg_buffer);
  __list_del(entry->Blink, entry->Flink);
}

uint8_t list_empty(pLIST_ENTRY head)
{
  return (head->Flink == head);
}
