/**
 * @file hcdmem.c
 * @author Oki Electric Industry Co.,Ltd. - 2003 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief HCD dynamic memory management
 */
#include <stdio.h>

#include "ml674061.h"
#include "common.h"

#include "utils/debug.h"
#include "utils/delay.h"

#include "usb/hcdmem.h"

/**
 * @brief Get the max value
 *
 * @param a
 * @param b
 *
 * @return max(a,b)
 */
#define max(a,b)  (((a)>(b))? (a):(b))

/*--------------------------------------------------------------------------*/
typedef struct _memory
{
  struct _memory *Next;
  uint32_t Address;
  uint32_t Area;
  uint32_t Size;
  uint32_t tag;
  uint32_t time;
} MMDL, *PMMDL;

typedef struct _buffer
{
  PMMDL MMDLs;
  uint32_t Address;
  uint32_t PhyAddress;
  uint32_t Size;
  uint32_t Boundary;
} MBDL, *PMBDL;

/*--------------------------------------------------------------------------*/
#define BufferBanks    3

static MBDL BufferTable[BufferBanks] = {
  { NULL, 0, 0, 0 ,0},
  { NULL, 0, 0, 0 ,0},
  { NULL, 0, 0, 0 ,0}
};

/*--------------------------------------------------------------------------*/
#define SetBoundary(Addr, Boundary) (((Addr) % (Boundary) == 0 ) ? (Addr) : \
                  (((Addr) / (Boundary) + 1) * (Boundary)))

int8_t hcd_malloc_init(int32_t Address, uint32_t Size,
                    uint32_t Boundary, uint8_t Bank)
{
  #ifdef DEBUG_USB
  sprintf(dbg_buffer, "Set up memory bank %d, addr 0x%08lx, size %ld"EOL,
          Bank, Address, Size);
  DBG_USB(dbg_buffer);
  #endif

  if(Bank<BufferBanks)
  {
    BufferTable[Bank].MMDLs = NULL;
    BufferTable[Bank].Address = Address;
    BufferTable[Bank].Size = Size;
    BufferTable[Bank].Boundary = Boundary;
    BufferTable[Bank].PhyAddress = Address;
  }
  else
  {
    DBG_USB(" hcd_malloc: Bad number of Bank."EOL);
    return -1;
  }
  return 0;
}

int8_t hcd_malloc_check(void *Address)
{
  int8_t Bank;

  for(Bank = 0; Bank < BufferBanks; Bank++){
    if(((uint32_t)Address  < (uint32_t)BufferTable[Bank].Address
                    + BufferTable[Bank].Size)
    && ((uint32_t)Address  >= (uint32_t)BufferTable[Bank].Address)){
      return Bank;
    }
  }
  return -1;
}

void *hcd_malloc(uint32_t Size, int8_t Bank,int32_t tag)
{
  PMMDL  pLastMMDL;
  PMMDL  pNextMMDL;
  PMMDL  pNewMMDL;
  uint32_t  NextBufferAddress;
  PMBDL  Buffer;
  uint32_t  Area;

  if(Bank>=0 && Bank<BufferBanks)
  {
    Buffer = &BufferTable[Bank];
  }
  else
  {
    DBG_USB(" hcd_malloc: Bad number of Bank."EOL);
    return NULL;
  }

  if(Buffer->Address == 0)
  {
    DBG_USB("hcd_malloc: No memory for buffer."EOL);
    return NULL;
  }

  if(Size==0)
    return NULL;

  if( Size > (Buffer->Size - sizeof(MMDL)) )
  {
    DBG_USB("hcd_malloc: Size > (Buffer->Size - sizeof(MMDL)"EOL);
    return NULL;
  }

  Area = Size + sizeof(MMDL);
  switch(Area % 4)
  {
    case 1:
      Area += 3;
      break;
    case 2:
      Area += 2;
      break;
    case 3:
      Area++;
      break;
    default:
      break;
  }

  pLastMMDL = NULL;
  if(Buffer->MMDLs == NULL)
  {
    NextBufferAddress = Buffer->Address;
    pNextMMDL = NULL;
  }
  else
  {
    NextBufferAddress = SetBoundary(Buffer->Address, Buffer->Boundary);
    pNextMMDL = Buffer->MMDLs;

    while(pNextMMDL != NULL)
    {
      if(NextBufferAddress + Area <= pNextMMDL->Address)
      {
        break;
      }

      NextBufferAddress = SetBoundary(pNextMMDL->Address + pNextMMDL->Area,
                                      Buffer->Boundary);
      pLastMMDL = pNextMMDL;
      pNextMMDL = pLastMMDL->Next;
    }
  }

  if(NextBufferAddress + Area > Buffer->Address + Buffer->Size)
  {
    DBG_USB("hcd_malloc: NextBufferAddress + Area > Buffer->Address + Buffer->Size"EOL);
    pNextMMDL = Buffer->MMDLs;
    while(pNextMMDL != NULL)
    {
      sprintf(dbg_buffer,"addr=0x%lX area=0x%lX size=0x%lX tag=0x%lX time=%ld"EOL,
              pNextMMDL->Address,pNextMMDL->Area,pNextMMDL->Size,
              pNextMMDL->tag,pNextMMDL->time);
      DBG_USB(dbg_buffer);
      pNextMMDL = pNextMMDL->Next;
    }
    sprintf(dbg_buffer,"time=%ld"EOL,counter_timer);
    DBG_USB(dbg_buffer);
    return NULL;
  }

  pNewMMDL = (PMMDL)(NextBufferAddress + Area - sizeof(MMDL));

  pNewMMDL->Address = NextBufferAddress;
  pNewMMDL->Area = Area;
  pNewMMDL->Size = Size;
  pNewMMDL->tag=tag;
  pNewMMDL->time=counter_timer;

  if(pLastMMDL == NULL)
  {
    Buffer->MMDLs = pNewMMDL;
  }
  else
  {
    pLastMMDL->Next = pNewMMDL;
  }

  pNewMMDL->Next = pNextMMDL;

//  sprintf(dbg_buffer, "hcd_malloc: %08lX (%d,%d)"EOL, NextBufferAddress, Size, Area);
//  DBG_USB(dbg_buffer);

  return (uint8_t *)NextBufferAddress;
}

int8_t hcd_free(void * pAddress)
{
  PMMDL  pLastMMDL;
  PMMDL  pNowMMDL;
  PMBDL  Buffer;
  int8_t  Bank;


  Bank = hcd_malloc_check(pAddress);
  if(Bank<0)
    return 0;

  Buffer = &BufferTable[Bank];

  if(pAddress == NULL || Buffer->MMDLs == NULL)
    return 0;

  pNowMMDL = Buffer->MMDLs;
  if((uint8_t *)pNowMMDL->Address == (uint8_t *)pAddress)
  {
//    if (pNowMMDL->tag==19)
//    {
//      sprintf(dbg_buffer,"hcd_free: %x"EOL, pAddress);
//      DBG_USB(dbg_buffer);
//    }

    Buffer->MMDLs = pNowMMDL->Next;
    return 1;
  }

  pLastMMDL = Buffer->MMDLs;
  while(pLastMMDL->Next != NULL)
  {
    pNowMMDL = pLastMMDL->Next;

    if((uint8_t *)pNowMMDL->Address == (uint8_t *)pAddress)
    {
//      if (pNowMMDL->tag==19)
//      {
//        sprintf(dbg_buffer,"hcd_free: %x"EOL, pAddress);
//        DBG_USB(dbg_buffer);
//      }
      pLastMMDL->Next = pNowMMDL->Next;
      return 1;
    }
    pLastMMDL = pNowMMDL;
  }
  return 0;
}

int32_t hcd_malloc_rest(uint8_t Bank)
{
  PMMDL  pLastMMDL;
  uint32_t  LastBufferAddress;
  PMBDL  Buffer;
  uint32_t Gap2MaxSize = 0;
  uint32_t FreeMaxSize;
  uint32_t rest;

  if(Bank<BufferBanks)
  {
    Buffer = &BufferTable[Bank];
  }
  else
  {
    DBG_USB(" hcd_malloc_rest: Bad number of Bank."EOL);
    return -1;
  }

  pLastMMDL = Buffer->MMDLs;
  LastBufferAddress = Buffer->Address;

  while(pLastMMDL != NULL)
  {
    if(LastBufferAddress != pLastMMDL->Address)
    {
      Gap2MaxSize = max(Gap2MaxSize, (pLastMMDL->Address - LastBufferAddress));
    }
    LastBufferAddress = pLastMMDL->Address + pLastMMDL->Area;
    pLastMMDL = pLastMMDL->Next;
  }
  FreeMaxSize = Buffer->Size - (int32_t)(LastBufferAddress - Buffer->Address);

  rest = max(Gap2MaxSize, FreeMaxSize)-sizeof(MMDL);
  return (int32_t)rest;
}
