/**
 * @file vinterp.c
 * @author Sylvain Huet - 2006 - Initial version
 * @author RedoX <dev@redox.ws> - 2015 - GCC Port, cleanup
 * @date 2015/09/07
 * @brief VLISP Virtual Machine -  Interpreter functions
 */
#include <string.h>

#include "ml674061.h"
#include "common.h"

#include "vm/vaudio.h"
#include "vm/vinterp.h"
#include "vm/vloader.h"
#include "vm/vlog.h"
#include "vm/vmem.h"
#include "vm/vnet.h"

#ifdef DEBUG_VM
#include "vm/vbc_str.h"
#ifdef _NAB_SIM
#include <stdio.h>
#endif
#endif

// test si dernier appel ?
int32_t TFCtest(int32_t p,int32_t pbase)
{
  int32_t i;
  while(_bytecode[p]!=OPret)
  {
    if (_bytecode[p]==OPgoto)
    {
      p++;
      i=(_bytecode[p]&255)+((_bytecode[p+1]&255)<<8);
      p=pbase+i;
    }
    else
      return 0;
  }
  return 1;
}


int32_t _currentop=-1;

// execute une fonction
// la pile doit contenir la liste d'argument, puis la fonction
// la fonction est soit un numéro, soit un tuple [numéro liste]
// on distingue le cas selon le fait que la valeur empilée est un pointeur ou un entier
void interpGo()
{
  int32_t pc=-1;
  int32_t pcbase=-1;
  int32_t callstack=1;
  int32_t callstackres=1;
  int32_t op=OPexec;
  int32_t cont;

  if (_vmem_broken)
    return;
  while(1)
  {

    do
    {
      cont=0;
      _currentop=op;
      #ifdef DEBUG_VM_FULL
//				displaybc(pc-1,pcbase);
      sprintf(dbg_buffer,"pc 0x%04X\t op: %d - ",pc,op);
      DBG_VM(dbg_buffer);
      if(op < MaxOpcode)
      {
        DBG_VM(strbytecod[op]);
      } else
        DBG_VM("unknown");
      if (op)
      {
        sprintf(dbg_buffer,"\t: %d",_bytecode[pc]);
        DBG_VM(dbg_buffer);
      }
      DBG_VM(EOL);
      #endif
      CLR_WDT;
      switch(op)
      {
      case OPexec:
        {
          int32_t p,npc,narg,nloc,i,fun;
          p=VSTACKGET(0);
          if (ISVALPNT(p))
          {
            p=VALTOPNT(p);
            fun=VALTOINT(VFETCH(p,0));
            VSTACKSET(0,VFETCH(p,1));
            while(VSTACKGET(0)!=NIL)
            {
              VPUSH(VFETCH(VALTOPNT(VSTACKGET(0)),1));
              VSTACKSET(1,VFETCH(VALTOPNT(VSTACKGET(1)),0));
            }
            // VPULL();
          }
          else fun=VALTOINT(p);
          (void)VPULL();
          if (fun<0)
          {
            op=-fun;
            cont=1;
            break;
          }
          npc=loaderFunstart(fun);
          narg=_bytecode[npc];
          nloc=loaderGetShort(_bytecode+npc+1);
          npc+=3;

          if (callstack<=0)
          {
            if (TFCtest(pc,pcbase))
            {
              int32_t ncall;
              pc=VALTOINT(VCALLSTACKGET(callstackres,-3));
              pcbase=VALTOINT(VCALLSTACKGET(callstackres,-2));
              ncall=VALTOINT(VCALLSTACKGET(callstackres,-1));
              callstackres=VALTOINT(VCALLSTACKGET(callstackres,0));
              for(i=0;i<narg;i++)	VCALLSTACKSET(callstack,i,VSTACKGET(narg-i-1));
              _vmem_stack=callstack-narg+1;
              callstack=ncall;
            }
          }

          for(i=0;i<nloc;i++)VPUSH(NIL);
          VPUSH(INTTOVAL(pc));
          VPUSH(INTTOVAL(pcbase));
          VPUSH(INTTOVAL(callstack));
          VPUSH(INTTOVAL(callstackres));
          callstack=_vmem_stack+3+nloc+narg;
          callstackres=_vmem_stack;
          pcbase=pc=npc;
        }
        break;
      case OPret:
        {
          int32_t ncall;
          pc=VALTOINT(VSTACKGET(4));
          pcbase=VALTOINT(VSTACKGET(3));
          ncall=VALTOINT(VSTACKGET(2));
          callstackres=VALTOINT(VSTACKGET(1));
          VCALLSTACKSET(callstack,0,VSTACKGET(0));	// recopie le résultat
          _vmem_stack=callstack;
          callstack=ncall;
        }
        break;
      case OPintb:
        VPUSH(INTTOVAL(255&_bytecode[pc++]));
        break;
      case OPint:
        VPUSH(INTTOVAL(loaderGetInt(&_bytecode[pc])));
        pc+=4;
        break;
      case OPnil:
        VPUSH(NIL);
        break;
      case OPdrop:
        VDROPN(1);
        break;
      case OPdup:
        VPUSH(VSTACKGET(0));
        break;
      case OPgetlocalb:
        VPUSH(VCALLSTACKGET(callstack,255&_bytecode[pc++]));
        break;
      case OPgetlocal:
        VSTACKSET(0,VCALLSTACKGET(callstack,VALTOINT(VSTACKGET(0))));
        break;
      case OPadd:
        VSTACKSET(1,INTTOVAL(VALTOINT(VSTACKGET(1))+VALTOINT(VSTACKGET(0))));
        VDROPN(1);
        break;
      case OPsub:
        VSTACKSET(1,INTTOVAL(VALTOINT(VSTACKGET(1))-VALTOINT(VSTACKGET(0))));
        VDROPN(1);
        break;
      case OPmul:
        VSTACKSET(1,INTTOVAL(VALTOINT(VSTACKGET(1))*VALTOINT(VSTACKGET(0))));
        VDROPN(1);
        break;
      case OPdiv:
        VSTACKSET(1,INTTOVAL(VALTOINT(VSTACKGET(1))/VALTOINT(VSTACKGET(0))));
        VDROPN(1);
        break;
      case OPmod:
        VSTACKSET(1,INTTOVAL(VALTOINT(VSTACKGET(1))%VALTOINT(VSTACKGET(0))));
        VDROPN(1);
        break;
      case OPand:
        VSTACKSET(1,INTTOVAL(VALTOINT(VSTACKGET(1))&VALTOINT(VSTACKGET(0))));
        VDROPN(1);
        break;
      case OPor:
        VSTACKSET(1,INTTOVAL(VALTOINT(VSTACKGET(1))|VALTOINT(VSTACKGET(0))));
        VDROPN(1);
        break;
      case OPeor:
        VSTACKSET(1,INTTOVAL(VALTOINT(VSTACKGET(1))^VALTOINT(VSTACKGET(0))));
        VDROPN(1);
        break;
      case OPshl:
        VSTACKSET(1,INTTOVAL(VALTOINT(VSTACKGET(1))<<VALTOINT(VSTACKGET(0))));
        VDROPN(1);
        break;
      case OPshr:
        VSTACKSET(1,INTTOVAL(VALTOINT(VSTACKGET(1))>>VALTOINT(VSTACKGET(0))));
        VDROPN(1);
        break;
      case OPneg:
        VSTACKSET(0,INTTOVAL(-VALTOINT(VSTACKGET(0))));
        break;
      case OPnot:
        VSTACKSET(0,INTTOVAL(~VALTOINT(VSTACKGET(0))));
        break;
      case OPnon:
        VSTACKSET(0,INTTOVAL(VALTOINT(VSTACKGET(0))?0:1));
        break;
      case OPeq:
        VSTACKSET(1,INTTOVAL((VSTACKGET(1)==VSTACKGET(0))?1:0));
        VDROPN(1);
        break;
      case OPne:
        VSTACKSET(1,INTTOVAL((VSTACKGET(1)!=VSTACKGET(0))?1:0));
        VDROPN(1);
        break;
      case OPlt:
        VSTACKSET(1,INTTOVAL((VALTOINT(VSTACKGET(1))<VALTOINT(VSTACKGET(0)))?1:0));
        VDROPN(1);
        break;
      case OPgt:
        VSTACKSET(1,INTTOVAL((VALTOINT(VSTACKGET(1))>VALTOINT(VSTACKGET(0)))?1:0));
        VDROPN(1);
        break;
      case OPle:
        VSTACKSET(1,INTTOVAL((VALTOINT(VSTACKGET(1))<=VALTOINT(VSTACKGET(0)))?1:0));
        VDROPN(1);
        break;
      case OPge:
        VSTACKSET(1,INTTOVAL((VALTOINT(VSTACKGET(1))>=VALTOINT(VSTACKGET(0)))?1:0));
        VDROPN(1);
        break;
      case OPgoto:
        pc=pcbase+loaderGetShort(&_bytecode[pc]);
        break;
      case OPelse:
        if (!VPULL()) pc=pcbase+loaderGetShort(&_bytecode[pc]);
        else pc+=2;
        break;
      case OPmktabb:
        VPUSH(PNTTOVAL(VMALLOCCLEAR(255&_bytecode[pc++])));
        break;
      case OPmktab:
        VPUSH(PNTTOVAL(VMALLOCCLEAR(VALTOINT(VPULL()))));
        break;
      case OPdeftabb:
        VMKTAB(255&_bytecode[pc++]);
        break;
      case OPdeftab:
        VMKTAB(VALTOINT(VPULL()));
        break;
      case OPfetchb:
        {
          int32_t i=255&_bytecode[pc++];
          int32_t p=VSTACKGET(0);
          if (p!=NIL) VSTACKSET(0,VFETCH(VALTOPNT(p),i));
        }
        break;
      case OPfetch:
        {
          int32_t i=VPULL();
          int32_t p=VSTACKGET(0);
          if ((p==NIL)||(i==NIL)) VSTACKSET(0,NIL);
          else
          {
            i=VALTOINT(i);
            p=VALTOPNT(p);
            if ((i<0)||(i>=VSIZE(p))) VSTACKSET(0,NIL);
            else VSTACKSET(0,VFETCH(p,i));
          }
        }
        break;
      case OPgetglobalb:
        VPUSH(VCALLSTACKGET(_global_start,255&_bytecode[pc++]));
        break;
      case OPgetglobal:
        VSTACKSET(0,VCALLSTACKGET(_global_start,VALTOINT(VSTACKGET(0))));
        break;
      case OPSecho:
        logSecho(VSTACKGET(0),0);
        break;
      case OPIecho:
        logIecho(VSTACKGET(0),0);
        break;
      case OPsetlocalb:
        VCALLSTACKSET(callstack,255&_bytecode[pc++],VPULL());
        break;
      case OPsetlocal:
        {
          int32_t v=VALTOINT(VPULL());
          VCALLSTACKSET(callstack,v,VPULL());
        }
        break;
      case OPsetglobal:
        VCALLSTACKSET(_global_start,VALTOINT(VSTACKGET(1)),VSTACKGET(0));
        VSTACKSET(1,VSTACKGET(0));
        (void)VPULL();
        break;
      case OPsetstructb:
        {
          int32_t i=255&_bytecode[pc++];
          int32_t v=VPULL();
          int32_t p=VSTACKGET(0);
          if ((p!=NIL)&&(i!=NIL))
          {
            p=VALTOPNT(p);
            if ((i>=0)||(i<VSIZE(p))) VSTORE(p,i,v);
          }
        }
        break;
      case OPsetstruct:
        {
          int32_t i=VPULL();
          int32_t v=VPULL();
          int32_t p=VSTACKGET(0);
          if ((p!=NIL)&&(i!=NIL))
          {
            i=VALTOINT(i);
            p=VALTOPNT(p);
            if ((i>=0)||(i<VSIZE(p))) VSTORE(p,i,v);
          }
        }
        break;
      case OPhd:
        {
          int32_t p=VSTACKGET(0);
          if (p!=NIL) VSTACKSET(0,VFETCH(VALTOPNT(p),0));
        }
        break;
      case OPtl:
        {
          int32_t p=VSTACKGET(0);
          if (p!=NIL) VSTACKSET(0,VFETCH(VALTOPNT(p),1));
        }
        break;
      case OPsetlocal2:
        VCALLSTACKSET(callstack,VALTOINT(VSTACKGET(1)),VSTACKGET(0));
        VSTACKSET(1,VSTACKGET(0));
        (void)VPULL();
        break;
      case OPstore:
        {
          int32_t v=VPULL();
          int32_t i=VPULL();
          int32_t p=VSTACKGET(0);
          if ((p!=NIL)&&(i!=NIL))
          {
            i=VALTOINT(i);
            p=VALTOPNT(p);
            if ((i>=0)||(i<VSIZE(p))) VSTORE(p,i,v);
          }
          VSTACKSET(0,v);
        }
        break;
      case OPcall:
        {
          int32_t p=VSTACKGET(0);
          if (p==NIL)
          {
            VSTACKSET(1,NIL);
            (void)VPULL();
          }
          else
          {
            int32_t n;
            n=VSIZE(VALTOPNT(p));
            if (n==0) (void)VPULL();
            else
            {
              int32_t i;
              for(i=1;i<n;i++) VPUSH(NIL);
              p=VALTOPNT(VSTACKGET(n-1));
              for(i=0;i<n;i++) VSTACKSET(n-1-i,VFETCH(p,i));
            }
            VPUSH(INTTOVAL(n));
            op=OPcallr;
            cont=1;
          }
        }
        break;
      case OPcallrb:
        {
          int32_t n=255&_bytecode[pc++];
          int32_t valn=VSTACKGET(n);
          if (valn==NIL) VDROPN(n);
          else
          {
            int32_t i;
            for(i=n;i>0;i--) VSTACKSET(i,VSTACKGET(i-1));
            VSTACKSET(0,valn);
            op=OPexec;
            cont=1;
          }
        }
        break;
      case OPcallr:
        {
          int32_t n=VALTOINT(VPULL());
          int32_t valn=VSTACKGET(n);
          if (valn==NIL) VDROPN(n);
          else
          {
            int32_t i;
            for(i=n;i>0;i--) VSTACKSET(i,VSTACKGET(i-1));
            VSTACKSET(0,valn);
            op=OPexec;
            cont=1;
          }
        }
        break;
      case OPfirst:
        {
          int32_t p=VSTACKGET(0);
          if (p!=NIL) VPUSH(VFETCH(VALTOPNT(p),0));
          else VPUSH(NIL);
        }
        break;
      case OPtime_ms:
        VPUSH(INTTOVAL(sysTimems()));
        break;
      case OPtabnew:
        {
          int32_t v=VPULL();
          if (v==NIL) VSTACKSET(0,NIL);
          else
          {
            int32_t p,i,v0;
            v=VALTOINT(v);
            p=VMALLOC(v);
            v0=VSTACKGET(0);
            for(i=0;i<v;i++) VSTORE(p,i,v0);
            VSTACKSET(0,PNTTOVAL(p));
          }
        }
        break;
      case OPfixarg:
        {
          int32_t v=VSTACKGET(1);
          if (v==NIL) (void)VPULL();
          else
          {
            if (ISVALPNT(v))
            {
              VPUSH(VFETCH(VALTOPNT(v),1));
              VSTACKSET(2,VFETCH(VALTOPNT(VSTACKGET(2)),0));
            }
            else VPUSH(NIL);
            VMKTAB(2);
            VMKTAB(2);
          }
        }
        break;
      case OPabs:
        {
          int32_t v=VALTOINT(VSTACKGET(0));
          if (v<0) v=-v;
          VSTACKSET(0,INTTOVAL(v));
        }
        break;
      case OPmax:
        {
          int32_t v=VALTOINT(VPULL());
          int32_t w=VALTOINT(VSTACKGET(0));
          VSTACKSET(0,INTTOVAL(v<w?w:v));
        }
        break;
      case OPmin:
        {
          int32_t v=VALTOINT(VPULL());
          int32_t w=VALTOINT(VSTACKGET(0));
          VSTACKSET(0,INTTOVAL(v>w?w:v));
        }
        break;
      case OPrand:
        VPUSH(INTTOVAL(sysRand()));
        break;
      case OPsrand:
        sysSrand(VALTOINT(VSTACKGET(0)));
        break;
      case OPtime:
        VPUSH(INTTOVAL(sysTime()));
        break;
      case OPstrnew:
        {
          int32_t n=VALTOINT(VSTACKGET(0));
          if (n<0) VSTACKSET(0,NIL);
          else VSTACKSET(0,PNTTOVAL(VMALLOCBIN(n)));
        }
        break;
      case OPstrset:
        {
          int32_t v=VALTOINT(VPULL());
          int32_t n=VALTOINT(VPULL());
          int32_t p=VALTOPNT(VSTACKGET(0));
          if ((p!=NIL)&&(n>=0)&&(n<VSIZEBIN(p)))
            (VSTARTBIN(p))[n]=v;
        }
        break;
      case OPstrcpy:
        {
          int32_t len=VPULL();
          int32_t start=VALTOINT(VPULL());
          int32_t q=VALTOPNT(VPULL());
          int32_t index=VALTOINT(VPULL());
          int32_t p=VALTOPNT(VSTACKGET(0));
          if ((p!=NIL)&&(q!=NIL))
          {
            if (len==NIL) len=VSIZEBIN(q)-start; else len=VALTOINT(len);
            sysCpy(VSTARTBIN(p),index,VSIZEBIN(p),VSTARTBIN(q),start,VSIZEBIN(q),len);
          }
        }
        break;
      case OPvstrcmp:
        {
          int32_t len=VPULL();
          int32_t start=VALTOINT(VPULL());
          int32_t q=VALTOPNT(VPULL());
          int32_t index=VALTOINT(VPULL());
          int32_t p=VALTOPNT(VSTACKGET(0));
          if ((p!=NIL)&&(q!=NIL))
          {
            if (len==NIL)
            {
              len=VSIZEBIN(q)-start;
              if (len<VSIZEBIN(p)-index) len=VSIZEBIN(p)-index;
            }
            else len=VALTOINT(len);
            VSTACKSET(0,INTTOVAL(sysCmp(VSTARTBIN(p),index,VSIZEBIN(p),VSTARTBIN(q),start,VSIZEBIN(q),len)));
          }
          else VSTACKSET(0,INTTOVAL((p!=NIL)?1:(q!=NIL)?-1:0));
        }
        break;
      case OPstrfind:
        {
          int32_t len=VPULL();
          int32_t start=VALTOINT(VPULL());
          int32_t q=VALTOPNT(VPULL());
          int32_t index=VALTOINT(VPULL());
          int32_t p=VALTOPNT(VSTACKGET(0));
          if ((p!=NIL)&&(q!=NIL))
          {
            if (len==NIL) len=VSIZEBIN(q)-start; else len=VALTOINT(len);
            VSTACKSET(0,sysFind(VSTARTBIN(p),index,VSIZEBIN(p),VSTARTBIN(q),start,VSIZEBIN(q),len));
          }
          else VSTACKSET(0,NIL);
        }
        break;
      case OPstrfindrev:
        {
          int32_t len=VPULL();
          int32_t start=VALTOINT(VPULL());
          int32_t q=VALTOPNT(VPULL());
          int32_t index=VPULL();
          int32_t p=VALTOPNT(VSTACKGET(0));
          if ((p!=NIL)&&(q!=NIL))
          {
            if (len==NIL) len=VSIZEBIN(q)-start; else len=VALTOINT(len);
            if (index==NIL) index=VSIZEBIN(p)-1; else index=VALTOINT(index);
            VSTACKSET(0,sysFindrev(VSTARTBIN(p),index,VSIZEBIN(p),VSTARTBIN(q),start,VSIZEBIN(q),len));
          }
          else VSTACKSET(0,NIL);
        }
        break;
      case OPstrlen:
        {
          int32_t p=VALTOPNT(VSTACKGET(0));
          if (p!=NIL) VSTACKSET(0,INTTOVAL(VSIZEBIN(p)));
        }
        break;
      case OPstrget:
        {
          int32_t n=VALTOINT(VPULL());
          int32_t p=VALTOPNT(VSTACKGET(0));
          if ((p==NIL)||(n<0)||(n>=VSIZEBIN(p))) VSTACKSET(0,NIL);
          else VSTACKSET(0,INTTOVAL(((VSTARTBIN(p))[n]&255)));
        }
        break;
      case OPstrsub:
        //				vmemDump();
        {
          int32_t len=VPULL();
          int32_t start=VALTOINT(VPULL());
          int32_t q=VALTOPNT(VSTACKGET(0));
          if (q!=NIL)
          {
            if (len==NIL) len=VSIZEBIN(q)-start; else len=VALTOINT(len);
            if (start+len>VSIZEBIN(q)) len=VSIZEBIN(q)-start;
            if ((start>=0)&&(len>=0))
            {
              int32_t p=VMALLOCBIN(len);
              mystrcpy(VSTARTBIN(p),VSTARTBIN(VALTOPNT(VSTACKGET(0)))+start,len);
              VSTACKSET(0,PNTTOVAL(p));
            }
            else VSTACKSET(0,NIL);
          }
          else VSTACKSET(0,NIL);
        }
        //				vmemDump();
        break;
      case OPstrcat:
        {
          int32_t r;
          int32_t len=0;
          int32_t q=VALTOPNT(VSTACKGET(0));
          int32_t p=VALTOPNT(VSTACKGET(1));
          if (p!=NIL) len+=VSIZEBIN(p);
          if (q!=NIL) len+=VSIZEBIN(q);
          r=VMALLOCBIN(len);
          len=0;
          q=VALTOPNT(VPULL());
          p=VALTOPNT(VSTACKGET(0));
          if (p!=NIL)
          {
            len=VSIZEBIN(p);
            mystrcpy(VSTARTBIN(r),VSTARTBIN(p),len);
          }
          if (q!=NIL)	mystrcpy(VSTARTBIN(r)+len,VSTARTBIN(q),VSIZEBIN(q));
          VSTACKSET(0,PNTTOVAL(r));
        }
        break;
      case OPtablen:
        {
          int32_t p=VALTOPNT(VSTACKGET(0));
          if (p!=NIL) VSTACKSET(0,INTTOVAL(VSIZE(p)));
        }
        break;
      case OPstrcatlist:
        {
          int32_t r,q;
          int32_t len=0;
          int32_t p=VALTOPNT(VSTACKGET(0));
          while(p!=NIL)
          {
            q=VALTOPNT(VFETCH(p,0));
            if (q!=NIL) len+=VSIZEBIN(q);
            p=VALTOPNT(VFETCH(p,1));
          }
          r=VMALLOCBIN(len);
          if (r<0) return;
          len=0;
          p=VALTOPNT(VSTACKGET(0));
          while(p!=NIL)
          {
            q=VALTOPNT(VFETCH(p,0));
            if (q!=NIL)
            {
              mystrcpy(VSTARTBIN(r)+len,VSTARTBIN(q),VSIZEBIN(q));
              len+=VSIZEBIN(q);
            }
            p=VALTOPNT(VFETCH(p,1));
          }
          VSTACKSET(0,PNTTOVAL(r));
        }
        break;
      case OPled:
        sysLed(VALTOINT(VSTACKGET(1)),VALTOINT(VSTACKGET(0)));
        (void)VPULL();
        break;
      case OPmotorset:
        sysMotorset(VALTOINT(VSTACKGET(1)),VALTOINT(VSTACKGET(0)));
        (void)VPULL();
        break;
      case OPmotorget:
        VSTACKSET(0,INTTOVAL(sysMotorget(VALTOINT(VSTACKGET(0)))));
        break;
      case OPbutton2:
        VPUSH(INTTOVAL(sysButton2()));
        break;
      case OPbutton3:
        VPUSH(INTTOVAL(sysButton3()));
        break;
      case OPplayStart:
        {
          int32_t k;
          VCALLSTACKSET(_sys_start,SYS_CBPLAY,VPULL());
          k=VALTOINT(VPULL());
          VPUSH(INTTOVAL(0));
          audioPlayStart(44100,16,2,k);
        }
        break;
      case OPplayFeed:
        {
          int32_t len=VPULL();
          int32_t start=VALTOINT(VPULL());
          int32_t q=VALTOPNT(VSTACKGET(0));
          if (q!=NIL)
          {
            if (len==NIL) len=VSIZEBIN(q)-start; else len=VALTOINT(len);
            if (start+len>VSIZEBIN(q)) len=VSIZEBIN(q)-start;
            if ((start>=0)&&(len>=0))
            {
              len=audioPlayFeed(VSTARTBIN(q)+start,len);
              VSTACKSET(0,INTTOVAL(len));
            }
            else VSTACKSET(0,NIL);
          }
          else
          {
            audioPlayFeed(NULL,0);
            VSTACKSET(0,NIL);
          }
        }
        break;
      case OPplayStop:
        audioPlayStop();
        VPUSH(INTTOVAL(0));
        break;
      case OPrecStart:
        {
          int32_t freq,gain;
          VCALLSTACKSET(_sys_start,SYS_CBREC,VPULL());
          gain=VALTOINT(VPULL());
          freq=VALTOINT(VPULL());
          VPUSH(INTTOVAL(0));
          audioRecStart(freq,gain);
        }
        break;
      case OPrecStop:
        audioRecStop();
        VPUSH(INTTOVAL(0));
        break;
      case OPrecVol:
        {
          int32_t start=VALTOINT(VPULL());
          int32_t q=VALTOPNT(VSTACKGET(0));
          if (q!=NIL)
          {
            VSTACKSET(0,INTTOVAL(audioRecVol((uint8_t *)VSTARTBIN(q),VSIZEBIN(q),start)));
          }
        }
        break;
      case OPload:
        {
          int32_t len=VPULL();
          int32_t start=VALTOINT(VPULL());
          int32_t q=VALTOPNT(VPULL());
          int32_t index=VALTOINT(VPULL());
          int32_t p=VALTOPNT(VSTACKGET(0));
          if ((p!=NIL)&&(q!=NIL))
          {
            if (len==NIL) len=VSIZEBIN(p)-index; else len=VALTOINT(len);
            len=sysLoad(VSTARTBIN(p),index,VSIZEBIN(p),VSTARTBIN(q),start,len);
            VSTACKSET(0,INTTOVAL(len));
          }
          else VSTACKSET(0,NIL);
        }
        break;
      case OPgc:
        vmemGC();
        VPUSH(INTTOVAL((_vmem_heapindex-_vmem_stack)*100/VMEM_LENGTH));
        break;
      case OPsave:
        {
          int32_t len=VPULL();
          int32_t start=VALTOINT(VPULL());
          int32_t q=VALTOPNT(VPULL());
          int32_t index=VALTOINT(VPULL());
          int32_t p=VALTOPNT(VSTACKGET(0));
          if ((p!=NIL)&&(q!=NIL))
          {
            if (len==NIL) len=VSIZEBIN(p)-index; else len=VALTOINT(len);
            len=sysSave(VSTARTBIN(p),index,VSIZEBIN(p),VSTARTBIN(q),start,len);
            VSTACKSET(0,INTTOVAL(len));
          }
          else VSTACKSET(0,NIL);
        }
        break;
      case OPbytecode:
        DBG_VM(EOL"#################### OPbytecode"EOL);
        if (VSTACKGET(0)!=NIL)
        {
          uint8_t* q;
          int32_t env=-1;
          int32_t p=VALTOPNT(VCALLSTACKGET(_sys_start,SYS_ENV));
          audioPlayStop();
          audioRecStop();
          if (p!=NIL)
          {
            env=VSIZEBIN(p);
            memcpy(audioFifoPlay,VSTARTBIN(p),env);
          }

          q=VSTARTBIN(VALTOPNT(VSTACKGET(0)));
          //                                        dump(q+0x2df0,128);

          loaderInit(q);
          //                          dump(&_bytecode[0x2440],32);

          if (env>=0)
          {
            //                                          dump(audioFifoPlay,env);
            VCALLSTACKSET(_sys_start,SYS_ENV,
              PNTTOVAL(VMALLOCSTR(audioFifoPlay,env)));
          }
          VPUSH(INTTOVAL(0));
          interpGo();
          (void)VPULL();
          DBG_VM(EOL"#################### OPbytecode done"EOL);
          return;
        }
        break;
      case OPloopcb:
        VCALLSTACKSET(_sys_start,SYS_CBLOOP,VSTACKGET(0));
        break;
      case OPSecholn:
        logSecho(VSTACKGET(0),1);
        break;
      case OPIecholn:
        logIecho(VSTACKGET(0),1);
        break;
      case OPenvget:
        VPUSH(VCALLSTACKGET(_sys_start,SYS_ENV));
        break;
      case OPenvset:
        VCALLSTACKSET(_sys_start,SYS_ENV,VSTACKGET(0));
        break;
      case OPsndVol:
        audioVol(VALTOINT(VSTACKGET(0)));
        break;
      case OPrfidGet:
        {
          uint8_t *p;
          p=sysRfidget();
          if (!p) VPUSH(NIL);
          else VPUSH(PNTTOVAL(VMALLOCSTR(p,8)));
        }
        break;
      case OPplayTime:
        VPUSH(INTTOVAL(audioPlayTime()));
        break;
      case OPnetCb:
        VCALLSTACKSET(_sys_start,SYS_CBTCP,VSTACKGET(0));
        break;
      case OPnetSend:
        {
          int32_t speed=VALTOINT(VPULL());
          int32_t indmac=VALTOINT(VPULL());
          int32_t q=VALTOPNT(VPULL());
          int32_t len=VPULL();
          int32_t index=VALTOINT(VPULL());
          int32_t p=VALTOPNT(VSTACKGET(0));
          if ((p!=NIL)&&(q!=NIL))
          {
            if (len==NIL) len=VSIZEBIN(p)-index; else len=VALTOINT(len);
            VSTACKSET(0,INTTOVAL(netSend(VSTARTBIN(p),index,len,VSIZEBIN(p),VSTARTBIN(q),indmac,VSIZEBIN(q),speed)));
          }
          else VSTACKSET(0,NIL);
        }
        break;
      case OPnetState:
        VPUSH(INTTOVAL(netState()));
        break;
      case OPnetMac:
        VPUSH(PNTTOVAL(VMALLOCSTR(netMac(),6)));
        break;
      case OPnetChk:
        {
          int32_t val=VALTOINT(VPULL());
          int32_t len=VPULL();
          int32_t index=VALTOINT(VPULL());
          int32_t p=VALTOPNT(VSTACKGET(0));
          if (p!=NIL)
          {
            if (len==NIL) len=VSIZEBIN(p)-index; else len=VALTOINT(len);
            VSTACKSET(0,INTTOVAL(netChk(VSTARTBIN(p),index,len,VSIZEBIN(p),val)));
          }
          else VSTACKSET(0,INTTOVAL(val));
        }
        break;
      case OPnetSetmode:
        {
          int32_t chn=VALTOINT(VPULL());
          int32_t p=VALTOPNT(VPULL());
          int32_t md=VALTOINT(VSTACKGET(0));
          uint8_t * ssid=(p==NIL)?NULL:VSTARTBIN(p);
          netSetmode(md,ssid,chn);
        }
        break;
      case OPnetScan:
        {
          int32_t p=VALTOPNT(VPULL());
          uint8_t * ssid=(p==NIL)?NULL:VSTARTBIN(p);
          netScan(ssid);
        }
        break;
      case OPnetAuth:
        {
          int32_t key=VALTOPNT(VPULL());
          int32_t encrypt=VALTOINT(VPULL());
          int32_t authmode=VALTOINT(VPULL());
          int32_t scan=VALTOPNT(VSTACKGET(0));
          if (scan!=NIL)
          {
            int32_t ssid=VALTOPNT(VFETCH(scan,0));
            int32_t mac=VALTOPNT(VFETCH(scan,1));
            int32_t bssid=VALTOPNT(VFETCH(scan,2));
            int32_t chn=VALTOINT(VFETCH(scan,4));
            int32_t rate=VALTOINT(VFETCH(scan,5));
            int32_t crypt=VALTOINT(VFETCH(scan,6));
            /* FIXME: 20170822: Really necessary ?
             * Check upper part of encrypt (config) and crypt (scan)
             * before using crypt (which has the supported cipher infos
            */
            if ((mac!=NIL)&&(bssid!=NIL)&&(crypt&0xF0)==encrypt)
            {
              netAuth((ssid==NIL)?NULL:VSTARTBIN(ssid),VSTARTBIN(mac),
                VSTARTBIN(bssid),chn,rate,authmode,
                crypt,(key==NIL)?NULL:VSTARTBIN(key));
            }
          }
        }
        break;
      case OPnetSeqAdd:
        {
          int32_t inc=VALTOINT(VPULL());
          int32_t seq=VALTOPNT(VPULL());
          if ((seq==NIL)||(VSIZEBIN(seq)<4)) VPUSH(NIL);
          else netSeqAdd((uint8_t *)VSTARTBIN(seq),inc);
        }
        break;
      case OPstrgetword:
        {
          int32_t ind=VALTOINT(VPULL());
          int32_t src=VALTOPNT(VSTACKGET(0));
          if (src!=NIL) VSTACKSET(0,INTTOVAL(sysStrgetword((uint8_t *)VSTARTBIN(src),VSIZEBIN(src),ind)));
        }
        break;
      case OPstrputword:
        {
          int32_t val=VALTOINT(VPULL());
          int32_t ind=VALTOINT(VPULL());
          int32_t src=VALTOPNT(VSTACKGET(0));
          if (src!=NIL) sysStrputword((uint8_t *)VSTARTBIN(src),VSIZEBIN(src),ind,val);
        }
        break;
      case OPatoi:
        {
          int32_t src=VALTOPNT(VSTACKGET(0));
          if (src!=NIL) VSTACKSET(0,INTTOVAL(sysAtoi(VSTARTBIN(src))));
        }
        break;
      case OPhtoi:
        {
          int32_t src=VALTOPNT(VSTACKGET(0));
          if (src!=NIL)
          {
            VSTACKSET(0,INTTOVAL(sysHtoi(VSTARTBIN(src))));
          }
        }
        break;
      case OPitoa:
        sysItoa(VALTOINT(VPULL()));
        break;
      case OPctoa:
        {
          int32_t val=VALTOINT(VPULL());
          sysCtoa(val);
        }
        break;
      case OPitoh:
        sysItoh(VALTOINT(VPULL()));
        break;
      case OPctoh:
        sysCtoh(VALTOINT(VPULL()));
        break;
      case OPitobin2:
        sysItobin2(VALTOINT(VPULL()));
        break;
      case OPlistswitch:
        {
          int32_t key=VPULL();
          int32_t p=VALTOPNT(VSTACKGET(0));
          VSTACKSET(0,sysListswitch(p,key));
        }
        break;
      case OPlistswitchstr:
        {
          int32_t key=VPULL();
          int32_t p=VALTOPNT(VSTACKGET(0));
          if (key==NIL) VSTACKSET(0,sysListswitch(p,key));
          else VSTACKSET(0,sysListswitchstr(p,VSTARTBIN(VALTOPNT(key))));
        }
        break;
      case OPsndRefresh:
        audioRefresh();
        VPUSH(NIL);
        break;
      case OPsndWrite:
        {
          int32_t val=VALTOINT(VPULL());
          audioWrite(VALTOINT(VSTACKGET(0)),val);
        }
        break;
      case OPsndRead:
        VSTACKSET(0,INTTOVAL(audioRead(VALTOINT(VSTACKGET(0)))));
        break;
      case OPsndFeed:
        {
          int32_t len=VPULL();
          int32_t start=VPULL();
          int32_t q=VALTOPNT(VSTACKGET(0));
          if (start==NIL) start=0;
          else start=VALTOINT(start);
          if (q!=NIL)
          {
            if (len==NIL) len=VSIZEBIN(q)-start; else len=VALTOINT(len);
            if (start+len>VSIZEBIN(q)) len=VSIZEBIN(q)-start;
            if ((start>=0)&&(len>=0))
            {
              int32_t k=audioFeed(VSTARTBIN(q)+start,len);
              VSTACKSET(0,INTTOVAL(k));
            }
            else VSTACKSET(0,NIL);
          }
          else VSTACKSET(0,NIL);
        }
        break;
      case OPsndAmpli:
        audioAmpli(VALTOINT(VSTACKGET(0)));
        break;
      case OPcorePP:
        VPUSH(INTTOVAL(_vmem_stack));
        break;
      case OPcorePush:
        VPUSH(VSTACKGET(0));
        break;
      case OPcorePull:
        VSTACKSET(1,VSTACKGET(0));
        (void)VPULL();
        break;
      case OPcoreBit0:
        {
          int32_t v=VALTOINT(VPULL());
          VSTACKSET(0,(VSTACKGET(0)&0xfffffffe)|(v&1));
        }
        break;
      case OPreboot:
        {
          int32_t w=VALTOINT(VPULL());
          int32_t v=VALTOINT(VSTACKGET(0));
          #ifdef DEBUG_VM
          sprintf(dbg_buffer,"Reboot. Keys 0x%04lX.0x%04lX"EOL,v,w);
          DBG_VM(dbg_buffer);
          #endif
          if ((v==0x0407FE58)&&(w==0x13fb6754))
          {
            DBG_VM("************REBOOT NOW");
            sysReboot();
          }
        }
        break;
      case OPstrcmp:
        {
          int32_t q=VALTOPNT(VPULL());
          int32_t p=VALTOPNT(VSTACKGET(0));
          if ((p!=NIL)&&(q!=NIL))
          {
            int32_t pl=VSIZEBIN(p);
            int32_t ql=VSIZEBIN(q);

            VSTACKSET(0,INTTOVAL(memcmp(VSTARTBIN(p),VSTARTBIN(q),(pl>ql)?pl:ql)));
          }
          else VSTACKSET(0,INTTOVAL((p!=NIL)?1:(q!=NIL)?-1:0));
        }
        break;
      case OPadp2wav:
        {
          int32_t len=VPULL();
          int32_t start=VALTOINT(VPULL());
          int32_t q=VALTOPNT(VPULL());
          int32_t index=VALTOINT(VPULL());
          int32_t p=VALTOPNT(VSTACKGET(0));
          if ((p!=NIL)&&(q!=NIL))
          {
            if (len==NIL) len=VSIZEBIN(q)-start; else len=VALTOINT(len);
            AudioAdp2wav(VSTARTBIN(p),index,VSIZEBIN(p),VSTARTBIN(q),start,VSIZEBIN(q),len);
          }
        }
        break;
      case OPwav2adp:
        {
          int32_t len=VPULL();
          int32_t start=VALTOINT(VPULL());
          int32_t q=VALTOPNT(VPULL());
          int32_t index=VALTOINT(VPULL());
          int32_t p=VALTOPNT(VSTACKGET(0));
          if ((p!=NIL)&&(q!=NIL))
          {
            if (len==NIL) len=VSIZEBIN(q)-start; else len=VALTOINT(len);
            AudioWav2adp(VSTARTBIN(p),index,VSIZEBIN(p),VSTARTBIN(q),start,VSIZEBIN(q),len);
          }
        }
        break;
      case OPalaw2wav:
        {
          int32_t mu=VALTOINT(VPULL());
          int32_t len=VPULL();
          int32_t start=VALTOINT(VPULL());
          int32_t q=VALTOPNT(VPULL());
          int32_t index=VALTOINT(VPULL());
          int32_t p=VALTOPNT(VSTACKGET(0));
          if ((p!=NIL)&&(q!=NIL))
          {
            if (len==NIL) len=VSIZEBIN(q)-start; else len=VALTOINT(len);
            AudioAlaw2wav(VSTARTBIN(p),index,VSIZEBIN(p),VSTARTBIN(q),start,VSIZEBIN(q),len,mu);
          }
        }
        break;
      case OPwav2alaw:
        {
          int32_t mu=VALTOINT(VPULL());
          int32_t len=VPULL();
          int32_t start=VALTOINT(VPULL());
          int32_t q=VALTOPNT(VPULL());
          int32_t index=VALTOINT(VPULL());
          int32_t p=VALTOPNT(VSTACKGET(0));
          if ((p!=NIL)&&(q!=NIL))
          {
            if (len==NIL) len=VSIZEBIN(q)-start; else len=VALTOINT(len);
            AudioWav2alaw(VSTARTBIN(p),index,VSIZEBIN(p),VSTARTBIN(q),start,VSIZEBIN(q),len,mu);
          }
        }
        break;
      case OPnetPmk:
        {
          int32_t key=VALTOPNT(VPULL());
          int32_t ssid=VALTOPNT(VPULL());
          if ((key==NIL)||(ssid==NIL)) VPUSH(NIL);
          else
          {
            uint8_t pmk[32];
            netPmk(VSTARTBIN(ssid),VSTARTBIN(key),pmk);
            VPUSH(PNTTOVAL(VMALLOCSTR(pmk,32)));
          }
        }
        break;
      case OPflashFirmware:
        {
          int32_t w=VALTOINT(VPULL());
          int32_t v=VALTOINT(VPULL());
          int32_t firmware=VALTOPNT(VSTACKGET(0));
          #ifdef DEBUG_VM
            sprintf(dbg_buffer, "Flash firmware ! Keys: 0x%04lX 0x%04lX"EOL,v,w);
            DBG_VM(dbg_buffer);
          #endif
          if ((firmware!=NIL)&&(v==0x13fb6754)&&(w==0x0407FE58))
          {
            DBG_VM("************FLASH AND REBOOT NOW");
            sysFlash(VSTARTBIN(firmware),VSIZEBIN(firmware));
          }
        }
        break;
      case OPcrypt:
        {
          int32_t alpha=VALTOINT(VPULL());
          int32_t key=VALTOINT(VPULL());
          int32_t len=VPULL();
          int32_t index=VALTOINT(VPULL());
          int32_t p=VALTOPNT(VSTACKGET(0));
          if (p!=NIL)
          {
            if (len==NIL) len=VSIZEBIN(p)-index; else len=VALTOINT(len);
            VSTACKSET(0,INTTOVAL(sysCrypt(VSTARTBIN(p),index,len,VSIZEBIN(p),key,alpha)));
          }
          else VSTACKSET(0,NIL);
        }
        break;
      case OPuncrypt:
        {
          int32_t alpha=VALTOINT(VPULL());
          int32_t key=VALTOINT(VPULL());
          int32_t len=VPULL();
          int32_t index=VALTOINT(VPULL());
          int32_t p=VALTOPNT(VSTACKGET(0));
          if (p!=NIL)
          {
            if (len==NIL) len=VSIZEBIN(p)-index; else len=VALTOINT(len);
            VSTACKSET(0,INTTOVAL(sysUncrypt(VSTARTBIN(p),index,len,VSIZEBIN(p),key,alpha)));
          }
          else VSTACKSET(0,NIL);
        }
        break;
      case OPnetRssi:
        VPUSH(INTTOVAL(netRssi()));
        break;
      case OPrfidGetList:
                                sysRfidgetList();
        break;
      case OPrfidRead:
        {
        int32_t bloc=VALTOINT(VPULL());
        int32_t p=VALTOPNT(VPULL());
        if (p!=NIL)
        {
          sysRfidread(VSTARTBIN(p),bloc);
        }
        else VPUSH(NIL);
        }
        break;
      case OPrfidWrite:
        {
        int32_t q=VALTOPNT(VPULL());
        int32_t bloc=VALTOINT(VPULL());
        int32_t p=VALTOPNT(VPULL());
        if ((p!=NIL)&&(q!=NIL))
        {
          VPUSH(INTTOVAL(sysRfidwrite(VSTARTBIN(p),bloc,VSTARTBIN(q))));
        }
        else VPUSH(NIL);
        }
        break;
      case OPi2cRead:
      { // Lecture à partir d'un périphérique I2C
        int32_t val2 = VALTOINT(VPULL()); // Taille de lecture
        uint8_t val1 = VALTOINT(VPULL()); // Adresse du lapin
        sysI2cRead(val1, val2);
      }
      break;
      case OPi2cWrite:
      { // Ecriture sur un périphérique I2C
        int32_t val3=VALTOINT(VPULL());  // Taille du buffer
        int32_t val2=VALTOPNT(VPULL());  // Buffer à écrire
        uint8_t val1=VALTOINT(VPULL()); // Adresse du périphérique
        sysI2cWrite(val1, (uint8_t *)VSTARTBIN(val2), val3);
      }
      break;
      default:
      #ifdef DEBUG_VM
        sprintf(dbg_buffer,"Unknown opcode %ld at 0x%04lX"EOL,op, pc-1);
        DBG_VM(dbg_buffer);
        dump(_bytecode,256);
      #endif
        cont=0;
      }
    } while(cont);

    _currentop=-1;
    if (callstack>0)
      return;
    #ifdef DEBUG_VM_FULL
    if(pc>=0)
      vmemDump();
    #endif
    op=255&_bytecode[pc++];
  }
}
