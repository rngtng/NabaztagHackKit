//-------------------
// MV
// version WIN32 et POCKETPC
// Sylvain Huet
// Premiere version : 07/01/2003
// Derniere mise a jour : 07/01/2003
//

#ifndef _COMPILER_
#define _COMPILER_

#include"interpreter.h"

class Memory;
class Parser;
class Prodbuffer;

class Compiler
{
private:
    Memory* m;
    int systempackage;
    intptr_t* stdtypes;

    Prodbuffer* bc;	// structure de production de bytecode

    Prodbuffer* outputbuf;	// structure de production des messages de type


    Parser* parser;
    intptr_t* newpackage;
    intptr_t* newref;
    int globals;
    int locals;
    int nblocals;

    int ifuns;

    // typage
    intptr_t createnodetypecore(const char* name);
    intptr_t createnodetypecore(int name);
    intptr_t createnodetype(int type);
    intptr_t createnodetuple(int size);
    intptr_t createnodetupleval(int size);

    intptr_t* actualtype(intptr_t* p);

    int parsegraph(Parser* p,int env,int mono,int rec,int labels,int newvars,intptr_t* rnode);
    intptr_t parse_rnode(intptr_t* p);

    intptr_t creategraph(Parser* p,int env,int mono);
    intptr_t creategraph(Parser* p,int env,int mono,int labels);
    intptr_t creategraph(const char* src,int env,int mono);

    int recechograph(Prodbuffer *output,intptr_t* p,int rec,int labels);

    intptr_t reccopytype(intptr_t* p);
    intptr_t recresetcopy(intptr_t* p);

    intptr_t recgoweak(intptr_t* p);

    int restoreactual(intptr_t* t,intptr_t* s,int vt,int vs,int k);
    intptr_t recunif(intptr_t* x,intptr_t* y);
    intptr_t unif(intptr_t* x,intptr_t* y);
    intptr_t unif_argfun();

    intptr_t unifbigger(intptr_t* x,intptr_t* y);

    intptr_t* argsfromfun(intptr_t *f);

    void echonode(int code,intptr_t* p);
    // packages
    int hash(const char* name);
    intptr_t createpackage(const char* name,int loghach);
    void addreftopackage(intptr_t* ref,intptr_t* package);
    intptr_t* searchtype(int env,const char* name);
    intptr_t* searchemptytype(int env,const char* name);

    void dumppackage(int env);

    intptr_t searchbytype(int env,int type);

    // liste de labels
    intptr_t addlabel(intptr_t base,const char* name,int val,intptr_t ref);
    int nblabels(intptr_t base);
    void removenlabels(intptr_t base,int n);
    int searchlabel_byname(intptr_t base,const char* name,int* val,intptr_t* ref);
    int searchlabel_byval(intptr_t base,int val, char** name);
    intptr_t* tuplefromlabels(intptr_t base);

    // compilation
    int parsefile(int ifdef);
    int parsevar();
    int parseconst();
    int parseproto();
    int parseifdef(int ifndef);
    int skipifdef();
    intptr_t fillproto(int env,intptr_t* fun);
    intptr_t findproto(int env,intptr_t* fun);
    int parsetype();
    int parsestruct();
    int parsesum();

    int parsefun();
    int parseprogram();
    int parseexpression();
    int parsearithm();
    int parsea1();
    int parsea2();
    int parsea3();
    int parsea4();
    int parsea5();
    int parsea6();
    int parseterm();
    int parseref();
    int parseif();
    intptr_t parselet();
    int parseset();
    int parsewhile();
    int parsefor();
    int parsepntfun();
    int parselink();
    int parsecall();
    int parselocals();
    int parsestring();
    int parsefields(intptr_t* p);
    int parsematch();
    int parsematchcons(intptr_t* end);

    int parsegetpoint();
    int parsesetpoint(int local,int ind,int* opstore);

    int parseupdate();
    int parseupdatevals();

    // parsing variables
    int parseval();
    int parseval3();
    int parseval4();
    int parseval5();
    int parseval6();
    int parseval7();

    // production bytecode
    void bc_byte_or_int(int val,int opbyte,int opint);
    void bcint_byte_or_int(int val);
    // autres
    intptr_t addstdlibcore();
    int addstdlibstr();
    int addstdlibbuf();
    int addstdlibfiles();

    int recglobal(int val,Prodbuffer *b);
    int recglobals(int vlab,Prodbuffer *b);
    int recbc(int vref,Prodbuffer *b,Prodbuffer *btab,int offset);

public:
    Prodbuffer* brelease;
    Compiler(Memory* mem);
    ~Compiler();
    int start();
    void stop();
    intptr_t addnative(int nref, const char** nameref, int* valref
                       , int* coderef, const char** typeref,void* arg);

    int gocompile(int type); // [filename/src packages] -> [packages]
    int getsystempackage();

    intptr_t* searchref(int env,char* name);
    intptr_t* searchref_nosetused(int env,char* name);


    intptr_t echograph(Prodbuffer *output,intptr_t* p);
    intptr_t copytype(intptr_t* p);
    intptr_t recunifbigger(intptr_t* x,intptr_t* y);

};

#define LABELLIST_LENGTH 4
#define LABELLIST_NAME 0
#define LABELLIST_VAL 1
#define LABELLIST_REF 2
#define LABELLIST_NEXT 3

#define STDTYPE_LENGTH 8
#define STDTYPE_I 0
#define STDTYPE_F 1
#define STDTYPE_S 2
#define STDTYPE_Env 3
#define STDTYPE_Xml 4
#define STDTYPE_fun__u0_list_u0__list_u0 5
#define STDTYPE_fun__tab_u0_I__u0 6
#define STDTYPE_fun__fun_u0_u1_u0__u1 7


#define COMPILE_FROMFILE 0
#define COMPILE_FROMSTRING 1


#endif
