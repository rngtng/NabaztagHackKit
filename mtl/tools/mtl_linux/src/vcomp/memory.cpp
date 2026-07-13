//-------------------
// MV
// version WIN32 et POCKETPC
// Sylvain Huet
// Premiere version : 07/01/2003
// Derniere mise a jour : 07/01/2003
//

#include <stdio.h>
#include <string.h>
#include <stdint.h>

#include "param.h"
#include "terminal.h"
#include "memory.h"
#include "util.h"
#include "interpreter.h"

#define GCincPeriod 20


// Gestion de la pile
Stack::Stack()
{
    base=NULL;
}
Stack::~Stack()
{
    if (base)
    {
        delete base;
    }
}

void Stack::dump(FILE *f)
{
    const char* buf="stck";
    fwrite((void*)buf,1,4,f);
    fwrite((void*)&size,1,4,f);
    fwrite((void*)&pp,1,4,f);
    fwrite((void*)&base,1,4,f);
    fwrite((void*)base,1,4*size,f);
}

void Stack::initialize(int s)
{
    size=s;
    base=new intptr_t[size];
    pp=base;
}

int Stack::bigger(Memory* m)
{
    int currentpp=pp-base;

    size*=2;

    intptr_t* newbase=new intptr_t[size];
    if (!newbase)
    {
        PRINTF(m)(LOG_RUNTIME,"Stack : out of Stack Memory\n");
        AbortMetal(m,1);
        return MTLERR_OM;	// impossible d'augmenter la taille de la pile
    }
    int i;
    for(i=0; i<=currentpp; i++)
    {
        newbase[i]=base[i];
    }
    delete base;
    base=newbase;
    pp=&base[currentpp];
    return 0;
}




// ajout d'une racine
intptr_t Memory::addroot(intptr_t *p)
{
    intptr_t k;

    if (k=push(PNTTOVAL(p)))
    {
        return k;
    }
    intptr_t* r=malloc(LIST_LENGTH,TYPE_TAB);
    TABSET(this,r,LIST_VAL,PNTTOVAL(p));
    TABSET(this,r,LIST_NEXT,root);
    root=PNTTOVAL(r);
    STACKDROP(this);
    return 0;
}

// suppression d'une racine
void Memory::removeroot(intptr_t *p)
{
    intptr_t* last=NULL;	// pointeur vers le précédent maillon de la liste
    intptr_t vq=root;
    while(vq!=NIL)
    {
        intptr_t* q=VALTOPNT(vq);
        if (TABGET(q,LIST_VAL)==PNTTOVAL(p))
        {
            if (last)
            {
                TABSET(this,last,LIST_NEXT,TABGET(q,LIST_NEXT));
            }
            else
            {
                root=TABGET(q,LIST_NEXT);
            }
            return;
        }
        last=q;
        vq=TABGET(q,LIST_NEXT);
    }
}

Memory::Memory(int size,Terminal *t,FileSystem *fs)
{
    term=t;
    filesystem=fs;
    size0=size;	// on retient la taille initiale, elle sert d'ordre de grandeur
    gcincperiod=GCincPeriod;
    abort=0;

    util=new Util(this);
    winutil=NULL;
//	listing();
}

Memory::~Memory()
{
//	if (winutil) delete winutil;
    delete util;
}

void Memory::stop()
{
    abort=1;
    util->stop();
    root=NIL;
    stack.pp=0;
}

int Memory::start()
{
    root=NIL;

    stack.initialize(STACK_FIRST_SIZE);

    PRINTF(this)(LOG_RUNTIME,"Metal Virtual Machine\n");
    PRINTF(this)(LOG_RUNTIME,"V0.2 - Sylvain Huet - 2005\n");
    PRINTF(this)(LOG_RUNTIME,"--------------------------\n");
    return util->start();
}

void Memory::dump()
{
}


intptr_t* Memory::malloc(int size,int type)
{
    intptr_t *p=NULL;

    int blocsize=size+HEADER_LENGTH;
    p=new intptr_t[blocsize];
    if (!p)
    {
        return p;
    }

    HEADERSETSIZETYPE(p,blocsize,type);

    return p;
}



intptr_t* Memory::mallocClear(int size)
{
    intptr_t* p=malloc(size,TYPE_TAB);
    if (!p)
    {
        return p;
    }
    int i;
    for(i=0; i<size; i++)
    {
        TABSET(this,p,i,NIL);
    }
    return p;
}

// allocation de type TYPE_EXT (la fonction fun(pnt) sera appelée lors de l'oubli du bloc)
intptr_t* Memory::mallocExternal(void* pnt,FORGET fun)
{
    intptr_t* p=malloc(2,TYPE_EXT);
    if (!p)
    {
        return p;
    }
    TABSET(this,p,0,*((intptr_t*)pnt));
    TABSET(this,p,1,*((intptr_t*)fun));
    return p;
}

// allocation de type TYPE_EXT (la fonction fun(pnt) sera appelée lors de l'oubli du bloc)
intptr_t Memory::pushExternal(void* pnt,FORGET fun)
{
    intptr_t* p=mallocExternal(pnt,fun);
    if (!p)
    {
        return MTLERR_OM;
    }
    return push(PNTTOVAL(p));
}

intptr_t* Memory::storenosrc(int size)
{
    // calcul de la taille d'un bloc pouvant contenir une certain nombre de caractères
    // il faut 1 mot pour la taille et un octet nul final
    int l=2+(size>>2);

    intptr_t* p=malloc(l,TYPE_BINARY);
    if (!p)
    {
        return p;
    }
    STRSETLEN(p,size);
    STRSTART(p)[size]=0;
    return p;
}

intptr_t* Memory::storebinary(const char *src,int size)
{
    // calcul de la taille d'un bloc pouvant contenir une certain nombre de caractères
    // il faut 1 mot pour la taille et un octet nul final
    int l=2+(size>>2);

    intptr_t* p=malloc(l,TYPE_BINARY);
    if (!p)
    {
        return p;
    }
    STRSETLEN(p,size);
    memcpy(STRSTART(p),src,size);
    STRSTART(p)[size]=0;
    return p;
}

intptr_t* Memory::storestring(const char *src)
{
    return storebinary(src,strlen(src));
}

int Memory::deftab(int size)
{
    intptr_t* p=malloc(size,TYPE_TAB);
    if (!p)
    {
        return MTLERR_OM;
    }
    int i;
    for(i=size-1; i>=0; i--)
    {
        TABSET(this,p,i,STACKPULL(this));
    }
    return push(PNTTOVAL(p));
}

const char* Memory::errorname(int err)
{
    if (err==MTLERR_OM)
    {
        return "Out of memory";
    }
    else if (err==MTLERR_OP)
    {
        return "Unknown Operand";
    }
    else if (err==MTLERR_DIV)
    {
        return "Division by zero";
    }
    else if (err==MTLERR_RET)
    {
        return "Bad implementation on 'return'";
    }
    else if (err==MTLERR_NOFUN)
    {
        return "No function defined";
    }
    else if (err==MTLERR_SN)
    {
        return "Syntax error";
    }
    else if (err==MTLERR_TYPE)
    {
        return "Typechecking error";
    }
    else if (err==MTLERR_ABORT)
    {
        return "Application aborted";
    }
    else
    {
        return "Unknown error";
    }
}
