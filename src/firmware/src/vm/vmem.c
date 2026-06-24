/**
 * @file vmem.c
 * @author Sylvain Huet - 2006 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief VLISP Virtual Machine - Memory functions
 */
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#include "hal/audio.h"  // For play_check
#include "vm/vmem.h"
#include "vm/vloader.h"
#include "vm/vlog.h"

int32_t _vmem_heapindex;
int32_t *_vmem_top;
int32_t _vmem_stack;
int32_t _vmem_start;
int32_t _vmem_broken;

// initialisation de la mémoire
void vmemInit(int32_t start)
{
	_vmem_top=&_vmem_heap[VMEM_LENGTH];
	_vmem_stack=0;
	_vmem_start=_vmem_heapindex=start;
  _vmem_broken=0;
}

void vmemSetstart(int32_t start)
{
	int32_t size;

	if (start>=_vmem_start-HEADER_LENGTH) return;

	size=((_vmem_start-HEADER_LENGTH-start)<<2)-1;

	_vmem_heap[start]=(size<<8)+(TYPE_BINARY);
	_vmem_heap[start+HEADER_GC]=0;
	_vmem_heap[start+HEADER_LIST]=0;

	_vmem_start=start;
}


void vmemGCfirst()
{
	int32_t i,k,j,n;
	int32_t first;

	first=-1;
	for(i=_vmem_stack;i<0;i++)
	{
		k=_vmem_top[i];
		if ((ISVALPNT(k))&&(k!=NIL))
		{
			k=VALTOPNT(k);
//      if ((k<0)||(k>=VMEM_LENGTH)) consolestr("1.k out of space"EOL);
			if (!HEADER_USED(k))
			{
				HEADER_MARK(k);
				_vmem_heap[k+HEADER_LIST]=first;
				first=k;
			}
		}
	}
	while(first!=-1)
	{
		k=first;
//    if ((k<0)||(k>=VMEM_LENGTH)) consolestr("1.first out of space"EOL);
		first=_vmem_heap[k+HEADER_LIST];
		if (HEADER_TYPE(k))        // bloc table
		{
			n=VSIZE(k);
			j=k+HEADER_LENGTH;
			for(i=0;i<n;i++)
			{
				k=_vmem_heap[j+i];
				if ((ISVALPNT(k))&&(k!=NIL))
				{
					k=VALTOPNT(k);
//          if ((k<0)||(k>=VMEM_LENGTH)) consolestr("1.k2 out of space"EOL);
					if (!HEADER_USED(k))
					{
						HEADER_MARK(k);
						_vmem_heap[k+HEADER_LIST]=first;
						first=k;
					}
				}
			}
		}
	}
}

void dumpheap()
{
	int32_t pos,realsize;

	pos=_vmem_start;

	while(pos < _vmem_heapindex)
	{
		realsize=VSIZE(pos)+HEADER_LENGTH;
                consolehx(pos); consolestr(":pos ");consolehx(realsize);
    consolestr(":realsize"EOL);
  if ((realsize<0)||(realsize>=VMEM_LENGTH))
  {
    consolestr("2.realsize out of range"EOL);
    dump((uint8_t*)&_vmem_heap[pos-32],128);
    return;
  }
    dump((uint8_t*)&_vmem_heap[pos],32);
		pos+=realsize;
	}
}

void vmemGCsecond()
{
	int32_t pos,newpos,realsize;

	pos=newpos=_vmem_start;

	while(pos < _vmem_heapindex)
	{
		realsize=VSIZE(pos)+HEADER_LENGTH;
/*  if ((realsize<0)||(realsize>=VMEM_LENGTH))
  {
    dumpheap();
//    consolestr("2.realsize out of range"EOL);
//    dump((uint8_t*)&vmem_heap[pos-32],128);
  }
*/
    if (HEADER_USED(pos))
		{
			_vmem_heap[pos+HEADER_GC]=newpos<<1;
			newpos+=realsize;
		}
		pos+=realsize;
	}
}

void vmemGCthird()
{
	int32_t i,k,j,n;
	int32_t first;

	first=-1;
	for(i=_vmem_stack;i<0;i++)
	{
		k=_vmem_top[i];
		if ((ISVALPNT(k))&&(k!=NIL))
		{
			k=VALTOPNT(k);
			_vmem_top[i]=_vmem_heap[k+HEADER_GC]|1;	// attention, hack
			if (!HEADER_USED(k))
			{
				HEADER_MARK(k);
				_vmem_heap[k+HEADER_LIST]=first;
				first=k;
			}
		}
	}
	while(first!=-1)
	{
		k=first;
		first=_vmem_heap[k+HEADER_LIST];
		if (HEADER_TYPE(k))        // bloc table
		{
			n=VSIZE(k);
			j=k+HEADER_LENGTH;
			for(i=0;i<n;i++)
			{
				k=_vmem_heap[j+i];
				if ((ISVALPNT(k))&&(k!=NIL))
				{
					k=VALTOPNT(k);
					_vmem_heap[j+i]=_vmem_heap[k+HEADER_GC]|1;	// attention, hack
					if (!HEADER_USED(k))
					{
						HEADER_MARK(k);
						_vmem_heap[k+HEADER_LIST]=first;
						first=k;
					}
				}
			}
		}
	}
}
void vmemGCfourth()
{
	int32_t pos,newpos,realsize,i;

	pos=newpos=_vmem_start;

	while(pos < _vmem_heapindex)
	{
		realsize=VSIZE(pos)+HEADER_LENGTH;
		if (HEADER_USED(pos))
		{
			_vmem_heap[pos+HEADER_GC]=0;
      if (newpos!=pos)
      {
        if (newpos+realsize<=pos)
          memcpy(&_vmem_heap[newpos],&_vmem_heap[pos],realsize<<2);
        else
        {
          consolestr("########GC : BIG MOVE"EOL);
          for(i=0;i<realsize;i++)
            _vmem_heap[newpos+i]=_vmem_heap[pos+i];
        }

        newpos+=realsize;
        pos+=realsize;
//                        for(i=0;i<realsize;i++) vmem_heap[newpos++]=vmem_heap[pos++];
      }
      else
        newpos=pos=pos+realsize;
		}
		else
		{
			// ## ajouter ici la gestion des types externes : if (HEADER_EXT(pos)) ...
			pos+=realsize;
		}
	}
	_vmem_heapindex=newpos;
}

void vmemGC()
{
//  logGC();
	play_check(1);
	vmemGCfirst();
  play_check(1);
	vmemGCsecond();
  play_check(1);
	vmemGCthird();
  play_check(1);
	vmemGCfourth();
  play_check(1);
	logGC();
//        dump(bytecode,32);
}

int8_t vmem_check(int32_t wsize)
{
	if (VMEM_LENGTH+_vmem_stack-_vmem_heapindex-wsize<VMEM_GCTHRESHOLD)
	{
		vmemGC();
		if (VMEM_LENGTH+_vmem_stack-_vmem_heapindex-wsize<VMEM_GCTHRESHOLD)
		{
      consolestr("?OM Error"EOL);
      _vmem_broken=1;
      sysReboot();
			return -1;
		}
	}
  return 0;
}


int32_t vmemAllocBin(int32_t size,int32_t ext)
{
	int32_t wsize,p;
	wsize=HEADER_LENGTH+((size+4)>>2);

  if(vmem_check(wsize))
    return -1;

	p=_vmem_heapindex;
	_vmem_heapindex+=wsize;
	_vmem_heap[p]=(size<<8)+((ext&127)<<1)+(TYPE_BINARY);
	_vmem_heap[p+HEADER_GC]=0;
	_vmem_heap[p+HEADER_LIST]=0;
	_vmem_heap[p+wsize-1]=0;
	return p;
}

int32_t vmemAllocString(uint8_t *p,int32_t len)
{
	int32_t i;
	uint8_t *q;
	int32_t iq=vmemAllocBin(len,0);
	if (iq<0)
    return iq;
	q=VSTARTBIN(iq);
	for(i=0;i<len;i++)
    q[i]=p[i];
	return iq;
}

int32_t vmemAllocTab(int32_t size,int32_t ext)
{
	int32_t wsize,p;
	wsize=HEADER_LENGTH+size;

  if(vmem_check(wsize))
    return -1;

	p=_vmem_heapindex;
	_vmem_heapindex+=wsize;
	size=(size<<2)-1;
	_vmem_heap[p]=(size<<8)+((ext&127)<<1)+(TYPE_TAB);
	_vmem_heap[p+HEADER_GC]=0;
	_vmem_heap[p+HEADER_LIST]=0;
	_vmem_heap[p+wsize-1]=0;
	return p;
}

int32_t vmemAllocTabClear(int32_t size,int32_t ext)
{
	int32_t p=vmemAllocTab(size,ext);
	if (p>=0)
	{
		int32_t i;
		for(i=0;i<size;i++)
      _vmem_heap[p+HEADER_LENGTH+i]=NIL;
	}
	return p;
}

int32_t vmemPush(int32_t val)
{
	/* Check there is room for one more word before writing, like the
	   allocators do; the old code wrote first and checked afterwards. */
	int8_t r=vmem_check(1);
	if(!r)
		_vmem_top[--_vmem_stack]=val;
	return r;
}

void vmemStacktotab(int32_t n)
{
	int32_t *q;
	int32_t p=vmemAllocTab(n,0);
	q=VSTART(p);
	while(n>0)
    q[--n]=VPULL();
	VPUSH(PNTTOVAL(p));
}


void vmemDumpHeap()
{
  #ifdef DEBUG_VM
	consolestr("---HEAP"EOL);
  int32_t i,n,pos,realsize;
  char buff[50];
  pos=_vmem_start;
	n=0;
  while(pos < _vmem_heapindex)
	{
		realsize=VSIZE(pos)+HEADER_LENGTH;
		sprintf(buff,"0x%08X : %s %d\t",pos,HEADER_TYPE(pos)?"Tab":"Bin",VSIZE(pos));
    consolestr(buff);
		for(i=0;i<realsize;i++)
    {
      sprintf(buff,"0x%08x ",_vmem_heap[pos+i]);
      consolestr(buff);
    }
		consolestr(EOL);
		pos+=realsize;
		n++;
	}
  consolestr("---"EOL);
  #ifdef _NAB_SIM
  getchar();
  #endif
  #endif
	logGC();

}

void vmemDumpStack()
{
  #ifdef DEBUG_VM
	consolestr("---STACK"EOL);
  int32_t i,k;
  char buff[50];
	for(i=-1;i>=_vmem_stack;i--)
	{
		k=_vmem_top[i];

		sprintf(buff, "% 4d: 0x%08X -> 0x%08X (%d)"EOL,i,k,k>>1,k>>1);
    consolestr(buff);
	}
  consolestr("---"EOL);
  #ifdef _NAB_SIM
  getchar();
  #endif
  #endif
}

void vmemDump()
{
	vmemDumpHeap();
	vmemDumpStack();
}

void vmemDumpShort()
{
	logGC();
}
