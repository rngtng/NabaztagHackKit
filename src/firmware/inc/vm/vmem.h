/**
 * @file vmem.h
 * @author Sylvain Huet - 2006 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief VLISP Virtual Machine - Memory functions
 */
#ifndef _MEMORY_
#define _MEMORY_

// taille en mots 32 bits (128kwords -> 512ko)
#define VMEM_LENGTH	            (1024*200)
#define VMEM_GCTHRESHOLD        16

#ifndef _NAB_SIM
#define _vmem_heap               ((int32_t*)(0xD0010000))
#else
  extern int32_t _vmem_heap[VMEM_LENGTH];
#endif

extern int32_t _vmem_heapindex;
extern int32_t *_vmem_top;
extern int32_t _vmem_stack;
extern int32_t _vmem_start;
extern int32_t _vmem_broken;

int32_t vmemAllocBin(int32_t size,int32_t ext);
int32_t vmemAllocTab(int32_t size,int32_t ext);
int32_t vmemAllocTabClear(int32_t size,int32_t ext);
int32_t vmemAllocString(uint8_t *p,int32_t len);
int32_t vmemPush(int32_t val);
void    vmemStacktotab(int32_t n);

void    vmemInit(int32_t start);
void    vmemSetstart(int32_t start);

void    vmemGC();

void    vmemDumpHeap();
void    vmemDumpStack();
void    vmemDump();
void    vmemDumpShort();

#define TYPE_BINARY             0	// type binaire pour allocation
#define TYPE_TAB                1 // type table pour allocation

#define PNTTOVAL(p)             (1+((p)<<1))	// conversion pointeur vers mot mv
#define INTTOVAL(i)             ((i)<<1)	// conversion entier vers mot mv

#define VALTOPNT(v)             ((v)>>1)	// conversion mot mv vers pointeur
#define VALTOINT(v)             ((v)>>1)	// conversion mot mv vers entier

#define NIL                     (-1)	// nil
#define ISVALPNT(v)             ((v)&1)	// teste si un mot mv est un pointeur

#define HEADER_LENGTH           3
#define HEADER_SIZETYPE         0	// la taille contient le header
#define HEADER_GC               1	// bit 0 : marquage, autres bits : adresse après GC
#define HEADER_LIST             2	// pile GC

#define HEADER_TYPE(p)          (_vmem_heap[p]&1)
#define HEADER_EXT(p)           ((_vmem_heap[p]>>1)&127)
#define HEADER_SIZE(p)          (_vmem_heap[p]>>8)
#define HEADER_USED(p)          (_vmem_heap[(p)+HEADER_GC]&1)
#define HEADER_MARK(p)          _vmem_heap[(p)+HEADER_GC]|=1

// Stack management
#define VPUSH(v)                vmemPush(v)
#define VPULL()                 (_vmem_top[_vmem_stack++])
#define VSTACKGET(n)            (_vmem_top[_vmem_stack+(n)])
#define VSTACKSET(n,v)          _vmem_top[_vmem_stack+(n)]=(v)
#define VDROPN(n)               _vmem_stack+=(n)
#define VCALLSTACKGET(off,n)    (_vmem_top[(off)-(n)])
#define VCALLSTACKSET(off,n,v)  _vmem_top[(off)-(n)]=(v)

// Heap management
#define VMALLOC(n)              vmemAllocTab(n,0)
#define VMALLOCCLEAR(n)         vmemAllocTabClear(n,0)
#define VMALLOCBIN(n)           vmemAllocBin(n,0)
#define VMALLOCSTR(p,n)         vmemAllocString(p,n)
#define VMKTAB(n)               vmemStacktotab(n)
#define VSIZE(p)                (((_vmem_heap[p]>>8)+4)>>2)
#define VSIZEBIN(p)             (_vmem_heap[p]>>8)
#define VSTART(p)               (&_vmem_heap[(p)+HEADER_LENGTH])
#define VSTARTBIN(p)            ((uint8_t*)&_vmem_heap[(p)+HEADER_LENGTH])
#define VFETCH(p,i)             (_vmem_heap[(p)+HEADER_LENGTH+(i)])
#define VSTORE(p,i,v)           _vmem_heap[(p)+HEADER_LENGTH+(i)]=(v)

#endif

