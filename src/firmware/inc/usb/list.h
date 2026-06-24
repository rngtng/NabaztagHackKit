/**
 * @file list.h
 * @author Oki Electric Industry Co.,Ltd. - 2003 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief Linked list utils
 */
#ifndef _LIST_H_
#define _LIST_H_

typedef struct _LIST_ENTRY {
	struct _LIST_ENTRY *Blink;
	struct _LIST_ENTRY *Flink;
} LIST_ENTRY, *pLIST_ENTRY;

#define DEFINE_LIST_ENTRY(entry) LIST_ENTRY entry = {&(entry), &(entry)}

#define INIT_LIST_ENTRY(ptr) do { \
	(ptr)->Flink = (ptr); (ptr)->Blink = (ptr); \
} while (0)

#define list_struct_entry(entry, type, member) \
	((type *)((char *)(entry)-(unsigned long)(&((type *)0)->member)))

void __list_add(pLIST_ENTRY entry, pLIST_ENTRY Blink, pLIST_ENTRY Flink);
void __list_del(pLIST_ENTRY Blink, pLIST_ENTRY Flink);
void list_add_top(pLIST_ENTRY entry, pLIST_ENTRY head);
void list_add(pLIST_ENTRY entry, pLIST_ENTRY head);
void list_del(pLIST_ENTRY entry);
uint8_t list_empty(pLIST_ENTRY head);

#endif
