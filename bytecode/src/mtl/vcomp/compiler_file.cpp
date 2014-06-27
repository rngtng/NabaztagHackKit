//-------------------
// MV
// version WIN32 et POCKETPC
// Sylvain Huet
// Derniere mise a jour : 07/01/2003
//

#include <stdio.h>
#include <string.h>

#include "param.h"
#include "terminal.h"
#include "memory.h"
#include "parser.h"
#include "prodbuffer.h"
#include "compiler.h"
#include "interpreter.h"


int Compiler::recglobal(int val,Prodbuffer *b)
{
    if (val==NIL)
    {
        b->addint(-1);
//		printf("nil\n");
    }
    else if (ISVALPNT(val))
    {
        int *p=VALTOPNT(val);
        if (HEADER_TYPE(p)==TYPE_TAB)
        {
            b->addint((TABLEN(p)<<2)+3);
//			printf("tuple %d @ %x : %x\n",TABLEN(p),val,*p);
            for(int i=0; i<TABLEN(p); i++)
            {
                recglobal(TABGET(p,i),b);
            }
        }
        else
        {
            b->addint((STRLEN(p)<<2)+1);
            b->addstr(STRSTART(p),STRLEN(p));
//			printf("'%s'\n",STRSTART(p));
        }
    }
    else
    {
        b->addint(val);
//		printf("%d\n",VALTOINT(val));
    }
    return 0;
}


/**
	 Génère le bytecode correspondant à la déclaration de toutes les
	 variables globales
*/
int Compiler::recglobals(int vlab,Prodbuffer *b)
{
    if (vlab==NIL)
    {
        return -1;
    }

    int n=1+recglobals(TABGET(VALTOPNT(vlab),LABELLIST_NEXT),b);
    int val=TABGET(VALTOPNT(vlab),LABELLIST_REF);

//	printf("%d:%s ",n,STRSTART(VALTOPNT(TABGET(VALTOPNT(vlab),LABELLIST_NAME))));
    // avant de l'enregistrer, on verifie si tout est correct
    int* p;
    p = searchref_nosetused(PNTTOVAL(newpackage),STRSTART(VALTOPNT(TABGET(VALTOPNT(vlab),LABELLIST_NAME))));
    if (NULL != p)
    {
        if (VALTOINT(TABGET(p,REF_USED)) == 0)
        {
            PRINTF(m)(LOG_WARNING,"%s is declared but never used\n",
                      STRSTART(VALTOPNT(TABGET(VALTOPNT(vlab),LABELLIST_NAME))));
        }
        else if (VALTOINT(TABGET(p,REF_CODE)) == CODE_VAR)
        {
            if (VALTOINT(TABGET(p,REF_SET)) == 0)
            {
                if (VALTOINT(TABGET(p,REF_USED)) > VALTOINT(TABGET(p,REF_USED_IN_IFDEF)))
                    PRINTF(m)(LOG_WARNING,"%s never gets a value\n",
                              STRSTART(VALTOPNT(TABGET(VALTOPNT(vlab),LABELLIST_NAME))));
            }
            else if (VALTOINT(TABGET(p,REF_SET)) == 1)
                PRINTF(m)(LOG_WARNING,"%s is only set once, at declaration. It should be a const.\n",
                          STRSTART(VALTOPNT(TABGET(VALTOPNT(vlab),LABELLIST_NAME))));
        }
    }

    recglobal(val,b);
    return n;
}

int Compiler::recbc(int vref,Prodbuffer *b,Prodbuffer *btab,int offset)
{
    if (vref==NIL)
    {
        return -1;
    }
    int f=recbc(TABGET(VALTOPNT(vref),REF_NEXTALL),b,btab,offset);
    if (f<-1)
    {
        return f;
    }
    if (VALTOINT(TABGET(VALTOPNT(vref),REF_CODE))<0)
    {
        return f;
    }
    int nf=VALTOINT(TABGET(VALTOPNT(vref),REF_PACKAGE));
    if (nf<=f)
    {
        return f;
    }
    if (TABGET(VALTOPNT(vref),REF_VAL)==NIL)
    {
        printf("%s is EMPTY !!!\n",STRSTART(VALTOPNT(TABGET(VALTOPNT(vref),REF_NAME))));
        return -2;
    }
    int *fun=VALTOPNT(TABGET(VALTOPNT(vref),REF_VAL));
    int nloc=VALTOINT(TABGET(fun,FUN_NBLOCALS))-VALTOINT(TABGET(fun,FUN_NBARGS));
    int nargs=VALTOINT(TABGET(fun,FUN_NBARGS));
    int ipc=b->getsize()-offset;
    btab->addint(ipc);
    b->addchar(nargs);
    b->addshort(nloc);
    b->addstr(STRSTART(VALTOPNT(TABGET(fun,FUN_BC))),STRLEN(VALTOPNT(TABGET(fun,FUN_BC))));
    printf("%d@%d:%s nargs=%d nlocals=%d / %d bytes\n",nf,ipc,STRSTART(VALTOPNT(TABGET(VALTOPNT(vref),REF_NAME))),nargs,nloc,STRLEN(VALTOPNT(TABGET(fun,FUN_BC))));
    return nf;
}


// compilation
// [filename/src packages] -> [packages]
int Compiler::gocompile(int type)
{
    int k;

    if (STACKGET(m,0)==NIL)
    {
        STACKDROP(m);
        return 0;
    }
    const char* name=STRSTART(VALTOPNT(STACKGET(m,0)));
    if (type==COMPILE_FROMFILE)
    {
        parser=new Parser(m->term,m->filesystem,name);
        PRINTF(m)(LOG_COMPILER,"Compiler : compiling file '%s'\n",name);
    }
    else
    {
        parser=new Parser(m->term,name);
        name="...";
        PRINTF(m)(LOG_COMPILER,"Compiler : compiling string buffer\n");
    }

    k=createpackage(name,4);
    if (k)
    {
        return k;    // [package nom_fichier env]
    }
    TABSET(m,VALTOPNT(STACKGET(m,0)),PACK_NEXT,STACKGET(m,2));
    STACKSET(m,2,STACKGET(m,0));	// [newenv nom_fichier newenv]
    STACKDROPN(m,2);	// [newenv]
    newpackage=VALTOPNT(STACKGET(m,0));
    // parsing

//### ici on doit pouvoir ajouter la liste des globales et la liste des fonctions
//### c'est ici également qu'on va réinitialiser la structure bc
    k=STACKPUSH(m,NIL); // GLOBALS
    if (k)
    {
        return k;
    }
    globals=STACKREF(m);
    ifuns=0;

    k=parsefile(0);

//### c'est fait, on a le bytecode, la liste des fonctions, et la liste des globales
    if (!k)
    {
        Prodbuffer* btab=new Prodbuffer();

        int n=nblabels(globals);
        brelease->addint(0);
        recglobals(STACKGETFROMREF(m,globals,0),brelease);
        brelease->setint(0,brelease->getsize());

        int sizebc=brelease->getsize();
        brelease->addint(0);	// on prépare le champ pour la taille du bytecode
        int* p=VALTOPNT(TABGET(newpackage,PACK_HACH));
        int vref=TABGET(p,TABLEN(p)-1);
        int nfun=recbc(vref,brelease,btab,sizebc+4);
        if (nfun<0)
        {
            return nfun;
        }

        brelease->setint(sizebc,brelease->getsize()-sizebc-4);
        brelease->addshort(nfun+1);
        brelease->addstr(btab->getstart(),btab->getsize());
        delete btab;
    }
    if (k)
    {
        parser->echoposition();
    }
    PRINTF(m)(LOG_COMPILER,"\n");
//	dumppackage(STACKGET(m,0));
    delete parser;
    return k;
}


int Compiler::parsefile(int ifdef)
{
    int k;

    while(parser->next(0))
    {
        if (!strcmp(parser->token,"fun"))
        {
            if ((!parser->next(0))||(!islabel(parser->token)))
            {
                if (parser->token)
                {
                    PRINTF(m)(LOG_COMPILER,"Compiler : name of function expected (found '%s')\n",parser->token);
                }
                else
                {
                    PRINTF(m)(LOG_COMPILER,"Compiler : name of function expected (found EOF)\n");
                }
                return MTLERR_SN;
            }
            k=STRPUSH(m,parser->token);
            if (k)
            {
                return k;
            }
            int* s=VALTOPNT(STACKGET(m,0));
            k=parsefun();
            if (k)
            {
                PRINTF(m)(LOG_COMPILER,"Compiler : error compiling function '%s'\n",STRSTART(s));
                return k;
            }
        }
        else if (!strcmp(parser->token,"type"))
        {
            if ((!parser->next(0))||(!islabel(parser->token)))
            {
                if (parser->token)
                {
                    PRINTF(m)(LOG_COMPILER,"Compiler : name of type expected (found '%s')\n",parser->token);
                }
                else
                {
                    PRINTF(m)(LOG_COMPILER,"Compiler : name of type expected (found EOF)\n");
                }
                return MTLERR_SN;
            }
            k=STRPUSH(m,parser->token);
            if (k)
            {
                return k;
            }
            int* s=VALTOPNT(STACKGET(m,0));

            k=parsetype();
            if (k)
            {
                PRINTF(m)(LOG_COMPILER,"Compiler : error compiling type '%s'\n",STRSTART(s));
                return k;
            }
        }
        else if (!strcmp(parser->token,"var"))
        {
            if ((!parser->next(0))||(!islabel(parser->token)))
            {
                if (parser->token)
                {
                    PRINTF(m)(LOG_COMPILER,"Compiler : name of reference expected (found '%s')\n",parser->token);
                }
                else
                {
                    PRINTF(m)(LOG_COMPILER,"Compiler : name of reference expected (found EOF)\n");
                }
                return MTLERR_SN;
            }
            k=STRPUSH(m,parser->token);
            if (k)
            {
                return k;
            }
            int* s=VALTOPNT(STACKGET(m,0));

            k=parsevar();
            if (k)
            {
                PRINTF(m)(LOG_COMPILER,"Compiler : error compiling var '%s'\n",STRSTART(s));
                return k;
            }
        }
        else if (!strcmp(parser->token,"const"))
        {
            if ((!parser->next(0))||(!islabel(parser->token)))
            {
                if (parser->token)
                {
                    PRINTF(m)(LOG_COMPILER,"Compiler : name of reference expected (found '%s')\n",parser->token);
                }
                else
                {
                    PRINTF(m)(LOG_COMPILER,"Compiler : name of reference expected (found EOF)");
                }
                return MTLERR_SN;
            }
            k=STRPUSH(m,parser->token);
            if (k)
            {
                return k;
            }
            int *s=VALTOPNT(STACKGET(m,0));
            k=parseconst();
            if (k)
            {
                PRINTF(m)(LOG_COMPILER,"Compiler : error compiling const '%s'\n",STRSTART(s));
                return k;
            }
        }
        else if (!strcmp(parser->token,"proto"))
        {
            if ((!parser->next(0))||(!islabel(parser->token)))
            {
                if (parser->token)
                {
                    PRINTF(m)(LOG_COMPILER,"Compiler : name of function expected (found '%s')\n",parser->token);
                }
                else
                {
                    PRINTF(m)(LOG_COMPILER,"Compiler : name of function expected (found EOF)\n");
                }
                return MTLERR_SN;
            }
            k=STRPUSH(m,parser->token);
            if (k)
            {
                return k;
            }
            int* s=VALTOPNT(STACKGET(m,0));

            k=parseproto();
            if (k)
            {
                PRINTF(m)(LOG_COMPILER,"Compiler : error compiling proto '%s'\n",STRSTART(s));
                return k;
            }
        }
        else if (!strcmp(parser->token,"ifdef"))
        {
            k=parseifdef(0);
            if (k)
            {
                return k;
            }
        }
        else if (!strcmp(parser->token,"ifndef"))
        {
            k=parseifdef(1);
            if (k)
            {
                return k;
            }
        }
        else if ((ifdef)&&(!strcmp(parser->token,"}")))
        {
            parser->giveback();
            return 0;
        }
        else
        {
            PRINTF(m)(LOG_COMPILER,"Compiler : unknown token %s\n",parser->token);
            return MTLERR_SN;
        }
    }
    return 0;
}

int Compiler::skipifdef()
{
    int n=0;
    while(parser->next(0))
    {
        if (!strcmp(parser->token,"{"))
        {
            n++;
        }
        else if (!strcmp(parser->token,"}"))
        {
            n--;
            if (n<0)
            {
                return 0;
            }
        }
    }
    PRINTF(m)(LOG_COMPILER,"Compiler : '}' expected (found EOF)\n");
    return MTLERR_SN;
}

int Compiler::parseifdef(int ifndef)
{
    int k;
    if (!parser->next(0))
    {
        PRINTF(m)(LOG_COMPILER,"Compiler : label expected (found EOF)\n");
        return MTLERR_SN;
    }
    if (!islabel(parser->token))
    {
        PRINTF(m)(LOG_COMPILER,"Compiler : label expected (found '%s')\n",parser->token);
        return MTLERR_SN;
    }
    int first=1;

    int *ref, *type;
    if ((!(ref = searchref(PNTTOVAL(newpackage),parser->token)))&&(!(type = searchtype(PNTTOVAL(newpackage),parser->token))))
    {
        first=0;
    }

    // si c'est une variable on note qu'elle est utlilisé dans un ifdef
    // ça nous permet de savoir, plus tard, si elle est seulement
    // utilisée dans un/des ifdef - dans ce cas elle n'a pas besoin de
    // valeur - ou aussi autre part - dans ce cas elle a besoin d'être
    // settée au moins une fois.
    if (ref)
    {
        int curval = VALTOINT(TABGET(ref,REF_USED_IN_IFDEF));
        TABSET(m,ref,REF_USED_IN_IFDEF,INTTOVAL(curval+1));
    }

    if (ifndef)
    {
        first=!first;
    }
    k=parser->parsekeyword("{");
    if (k)
    {
        return k;
    }
    if (first)
    {
        parsefile(1);
        k=parser->parsekeyword("}");
        if (k)
        {
            return k;
        }
    }
    else
    {
        k=skipifdef();
        if (k)
        {
            return k;
        }
    }
    if (!parser->next(0))
    {
        return 0;
    }
    if (strcmp(parser->token,"else"))
    {
        parser->giveback();
        return 0;
    }
    else
    {
        k=parser->parsekeyword("{");
        if (k)
        {
            return k;
        }
        if (!first)
        {
            parsefile(1);
            k=parser->parsekeyword("}");
            if (k)
            {
                return k;
            }
        }
        else
        {
            k=skipifdef();
        }
        if (k)
        {
            return k;
        }
    }
    return 0;
}

void displaybc(Memory* m,char* src);


// compilation d'une fonction
// [name] -> 0
int Compiler::parsefun()
{
    int k;
    int* type_result;
//	PRINTF(m)(LOG_DEVCORE,"fonction %s\n",STRSTART(VALTOPNT(STACKGET(m,0))));

    char* name=STRSTART(VALTOPNT(STACKGET(m,0)));
    // création des variables de travail
    k=STACKPUSH(m,NIL);
    if (k)
    {
        return k;    // LOCALS
    }
    locals=STACKREF(m);

    k=createnodetype(TYPENAME_FUN);
    if (k)
    {
        return k;
    }

    // recherche des arguments
    int narg=0;
    do
    {
        if (!parser->next(0))
        {
            PRINTF(m)(LOG_COMPILER,"Compiler : argument or '=' expected (found EOF)\n");
            return MTLERR_SN;
        }
        if (islabel(parser->token))
        {
            k=createnodetype(TYPENAME_UNDEF);
            if (k)
            {
                return k;
            }
            k=addlabel(locals,parser->token,INTTOVAL(narg++),STACKGET(m,0));
            if (k)
            {
                return k;
            }
        }
        else if (strcmp(parser->token,"="))
        {
            PRINTF(m)(LOG_COMPILER,"Compiler : argument or '=' expected (found '%s')\n",parser->token);
            return MTLERR_SN;
        }
    }
    while(strcmp(parser->token,"="));
    // construction du type initial de la fonction
    k=createnodetuple(narg);
    if (k)
    {
        return k;
    }
    TABSET(m,VALTOPNT(STACKGET(m,1)),TYPEHEADER_LENGTH,STACKGET(m,0));	// attachement du noeud tuple au noeud fun
    STACKDROP(m);

    k=createnodetype(TYPENAME_UNDEF);
    if (k)
    {
        return k;    // noeud résultat
    }
    TABSET(m,VALTOPNT(STACKGET(m,1)),TYPEHEADER_LENGTH+1,STACKGET(m,0));	// attachement du noeud resultat au noeud fun
    type_result=VALTOPNT(STACKPULL(m));	// on garde en mémoire le type du résultat
    // ici : [type local global name]

    // on crée le bloc fonction
    newref=MALLOCCLEAR(m,REF_LENGTH);
    if (!newref)
    {
        return MTLERR_OM;
    }
    TABSET(m,newref,REF_TYPE,STACKPULL(m));
    TABSET(m,newref,REF_NAME,STACKGET(m,1));
    TABSET(m,newref,REF_CODE,INTTOVAL(narg));

    // vient d'être déclarée, pas encore utilisée
    TABSET(m,newref,REF_USED,INTTOVAL(0));

    k=findproto(PNTTOVAL(newpackage),newref);

    TABSET(m,newref,REF_PACKAGE,(k!=NIL)?k:INTTOVAL(ifuns++));

    k=STACKPUSH(m,PNTTOVAL(newref));	// [newref local global name]
    if (k)
    {
        return MTLERR_OM;
    }
    addreftopackage(newref,newpackage);
    STACKDROP(m);
    // [local global name]

    // on poursuit la compilation vers le corps de la fonction
    nblocals=narg;

    bc->reinit();	// initialisation de la production de bytecode

    // [locals globals]

    // parsing
    k=parseprogram();
    if (k)
    {
        return k;
    }

    // [type locals globals]
//	la pile contient le type du résultat de la fonction
    k=parser->parsekeyword(";;");
    if (k)
    {
        return k;
    }

    // unifier le type résultat
    k=unif(type_result,VALTOPNT(STACKGET(m,0)));
    if (k)
    {
        return k;
    }
    STACKDROP(m);
    // [locals globals name]
    // créer le bloc programme
    int* fun=MALLOCCLEAR(m,FUN_LENGTH);
    if (!fun)
    {
        return MTLERR_OM;
    }
    TABSET(m,newref,REF_VAL,PNTTOVAL(fun));

    TABSET(m,fun,FUN_NBARGS,INTTOVAL(narg));
    TABSET(m,fun,FUN_NBLOCALS,INTTOVAL(nblocals));

    // stocker le bytecode
    bc->addchar(OPret);

    k=STRPUSHBINARY(m,bc->getstart(),bc->getsize());
    if (k)
    {
        return k;
    }
    TABSET(m,fun,FUN_BC,STACKPULL(m));

    if (!strcmp(name,"awcConnect"))
    {
        displaybc(m,STRSTART(VALTOPNT(TABGET(fun,FUN_BC))));
    }

    // construire le tuple des références globales
//	int* globalstuple=tuplefromlabels(globals);
//	if (!globalstuple) return MTLERR_OM;
//	TABSET(m,fun,FUN_REF,PNTTOVAL(globalstuple));
    TABSET(m,fun,FUN_REFERENCE,PNTTOVAL(newref));

    STACKDROPN(m,2);

    // []

    // chercher d'éventuels prototypes
    k=fillproto(PNTTOVAL(newpackage),newref);
    if (k)
    {
        return k;
    }

    outputbuf->reinit();
    outputbuf->printf("Compiler : %s : ",STRSTART(VALTOPNT(TABGET(newref,REF_NAME))));
    echograph(outputbuf,VALTOPNT(TABGET(newref,REF_TYPE)));
    PRINTF(m)(LOG_COMPILER,"%s\n",outputbuf->getstart());

    return 0;
}

// compilation d'une variable
// [name] -> 0
int Compiler::parsevar()
{
    int k;
    int hasvalue = 0;

    if (!parser->next(0))
    {
        PRINTF(m)(LOG_COMPILER,"Compiler : ';;' expected (found EOF)\n");
        return MTLERR_SN;
    }
    if (!strcmp(parser->token,"="))
    {
        k=parseval();
        if (k)
        {
            return k;
        }
        hasvalue = 1;
    }
    else
    {
        parser->giveback();
        k=STACKPUSH(m,NIL);
        if (k)
        {
            return k;
        }
        k=createnodetype(TYPENAME_WEAK);
        if (k)
        {
            return k;
        }
    }
    // [val type name]
    k=parser->parsekeyword(";;");
    if (k)
    {
        return k;
    }

    int val=INTTOVAL(nblabels(globals));
    k=addlabel(globals,STRSTART(VALTOPNT(STACKGET(m,2))),val,STACKGET(m,1));
    if (k)
    {
        return k;    // enregistrement d'une nouvelle globale
    }

    // on crée le bloc fonction
    newref=MALLOCCLEAR(m,REF_LENGTH);
    if (!newref)
    {
        return MTLERR_OM;
    }
    TABSET(m,newref,REF_TYPE,STACKPULL(m));
    TABSET(m,newref,REF_VAL,STACKPULL(m));
    TABSET(m,newref,REF_NAME,STACKPULL(m));
    TABSET(m,newref,REF_CODE,INTTOVAL(CODE_VAR));

    // la variable vient d'être créé, elle n'est donc ni utilisée ni modifiée encore
    TABSET(m,newref,REF_USED,INTTOVAL(0));
    TABSET(m,newref,REF_SET,INTTOVAL(hasvalue));
    TABSET(m,newref,REF_USED_IN_IFDEF,INTTOVAL(0));

    TABSET(m,newref,REF_PACKAGE,val);

    k=STACKPUSH(m,PNTTOVAL(newref));	// [newref]
    if (k)
    {
        return MTLERR_OM;
    }
    addreftopackage(newref,newpackage);
    STACKDROP(m);

    outputbuf->reinit();
    outputbuf->printf("Compiler : var %s : ",STRSTART(VALTOPNT(TABGET(newref,REF_NAME))));
    echograph(outputbuf,VALTOPNT(TABGET(newref,REF_TYPE)));
    PRINTF(m)(LOG_COMPILER,"%s\n",outputbuf->getstart());
    return 0;
}


// compilation d'une constante (const var = val;;)
// [name] -> 0
int Compiler::parseconst()
{
    int k;

    k=parser->parsekeyword("=");
    if (k)
    {
        return k;
    }
    k=parseval();
    if (k)
    {
        return k;
    }
    k=parser->parsekeyword(";;");
    if (k)
    {
        return k;
    }

    int val=INTTOVAL(nblabels(globals));
    k=addlabel(globals,STRSTART(VALTOPNT(STACKGET(m,2))),val,STACKGET(m,1));
    if (k)
    {
        return k;    // enregistrement d'une nouvelle globale
    }

    // on crée le bloc fonction
    newref=MALLOCCLEAR(m,REF_LENGTH);
    if (!newref)
    {
        return MTLERR_OM;
    }
    TABSET(m,newref,REF_TYPE,STACKPULL(m));
    TABSET(m,newref,REF_VAL,STACKPULL(m));
    TABSET(m,newref,REF_NAME,STACKPULL(m));
    TABSET(m,newref,REF_CODE,INTTOVAL(CODE_CONST));

    // la constante vient d'être créée, elle n'est donc pas utilisée, mais elle a une valeur
    TABSET(m,newref,REF_USED,INTTOVAL(0));
    TABSET(m,newref,REF_SET,INTTOVAL(1));
    TABSET(m,newref,REF_USED_IN_IFDEF,INTTOVAL(0));

    TABSET(m,newref,REF_PACKAGE,val);

    k=STACKPUSH(m,PNTTOVAL(newref));	// [newref]
    if (k)
    {
        return MTLERR_OM;
    }
    addreftopackage(newref,newpackage);
    STACKDROP(m);

    outputbuf->reinit();
    outputbuf->printf("Compiler : const %s : ",STRSTART(VALTOPNT(TABGET(newref,REF_NAME))));
    echograph(outputbuf,VALTOPNT(TABGET(newref,REF_TYPE)));
    PRINTF(m)(LOG_COMPILER,"%s\n",outputbuf->getstart());
    return 0;
}


// compilation d'un prototype
// [name] -> 0
int Compiler::parseproto()
{
    int k;

    if (!parser->next(0))
    {
        PRINTF(m)(LOG_COMPILER,"Compiler : integer or '=' expected (found EOF)\n");
        return MTLERR_SN;
    }
    int nbarg=-1;
    if (!strcmp(parser->token,"="))
    {
        k=creategraph(parser,PNTTOVAL(newpackage),0);
        if (k)
        {
            return k;
        }
        int vp=STACKGET(m,0);
        if (vp!=NIL)
        {
            int* p=VALTOPNT(vp);
            if (TABGET(p,TYPEHEADER_CODE)==INTTOVAL(TYPENAME_FUN))
            {
                vp=TABGET(p,TYPEHEADER_LENGTH);
                if (vp!=NIL)
                {
                    int* p=VALTOPNT(vp);
                    if (TABGET(p,TYPEHEADER_CODE)==INTTOVAL(TYPENAME_TUPLE))
                    {
                        nbarg=TABLEN(p)-TYPEHEADER_LENGTH;
                    }
                }
            }
        }
        if (nbarg<0)
        {
            PRINTF(m)(LOG_COMPILER,"Compiler : function type expected\n");
            return MTLERR_SN;
        }
    }
    else if (isdecimal(parser->token))
    {
        nbarg=mtl_atoi(parser->token);

        k=createnodetype(TYPENAME_FUN);
        if (k)
        {
            return k;
        }

        int i;
        for(i=0; i<nbarg; i++)
        {
            k=createnodetype(TYPENAME_WEAK);
        }
        if (k)
        {
            return k;
        }
        k=createnodetuple(nbarg);
        if (k)
        {
            return k;
        }

        TABSET(m,VALTOPNT(STACKGET(m,1)),TYPEHEADER_LENGTH,STACKGET(m,0));	// attachement du noeud tuple au noeud fun
        STACKDROP(m);

        k=createnodetype(TYPENAME_WEAK);
        if (k)
        {
            return k;    // noeud résultat
        }
        TABSET(m,VALTOPNT(STACKGET(m,1)),TYPEHEADER_LENGTH+1,STACKGET(m,0));	// attachement du noeud resultat au noeud fun
        STACKDROP(m);
    }
    if (nbarg<0)
    {
        PRINTF(m)(LOG_COMPILER,"Compiler : integer or '=' expected (found '%s')\n",parser->token);
        return MTLERR_SN;
    }
    k=parser->parsekeyword(";;");
    if (k)
    {
        return k;
    }
    // on crée le bloc fonction
    newref=MALLOCCLEAR(m,REF_LENGTH);
    if (!newref)
    {
        return MTLERR_OM;
    }
    TABSET(m,newref,REF_TYPE,STACKPULL(m));
    TABSET(m,newref,REF_NAME,STACKPULL(m));
    TABSET(m,newref,REF_CODE,INTTOVAL(nbarg));
    TABSET(m,newref,REF_PACKAGE,INTTOVAL(ifuns++));
    k=STACKPUSH(m,PNTTOVAL(newref));	// [newref]
    if (k)
    {
        return MTLERR_OM;
    }
    addreftopackage(newref,newpackage);
    STACKDROP(m);

    outputbuf->reinit();
    outputbuf->printf("Compiler : proto %s : ",STRSTART(VALTOPNT(TABGET(newref,REF_NAME))));
    echograph(outputbuf,VALTOPNT(TABGET(newref,REF_TYPE)));
    PRINTF(m)(LOG_COMPILER,"%s\n",outputbuf->getstart());
    return 0;
}
// compilation d'un type
// [name] -> 0
int Compiler::parsetype()
{
    int k;
//	PRINTF(m)(LOG_DEVCORE,"type %s\n",STRSTART(VALTOPNT(STACKGET(m,0))));

    char* name=STRSTART(VALTOPNT(STACKGET(m,0)));
    // création des variables de travail
    k=STACKPUSH(m,NIL);
    if (k)
    {
        return k;    // LOCALS
    }
    locals=STACKREF(m);

    newref=searchemptytype(PNTTOVAL(newpackage),name);
    int mergetype=1;
    if (newref)
    {
        k=createnodetypecore(TABGET(VALTOPNT(TABGET(newref,REF_TYPE)),TYPEHEADER_LENGTH+1));
        if (k)
        {
            return k;
        }
    }
    else
    {
        mergetype=0;
        k=createnodetypecore(STACKGET(m,1));
        if (k)
        {
            return k;
        }
        newref=MALLOCCLEAR(m,REF_LENGTH);
        if (!newref)
        {
            return MTLERR_OM;
        }
        TABSET(m,newref,REF_CODE,INTTOVAL(CODE_EMPTYTYPE));
        TABSET(m,newref,REF_TYPE,STACKGET(m,0));
        k=STACKPUSH(m,PNTTOVAL(newref));	// [newtyp local name]
        if (k)
        {
            return MTLERR_OM;
        }
        addreftopackage(newref,newpackage);
        STACKDROP(m);
    }

    int narg=0;
    if (parser->next(0))
    {
        if (strcmp(parser->token,"("))
        {
            parser->giveback();
        }
        else
        {
            do
            {
                if (!parser->next(0))
                {
                    PRINTF(m)(LOG_COMPILER,"Compiler : parameter or ')' expected (found EOF)\n");
                    return MTLERR_SN;
                }
                if (islabel(parser->token))
                {
                    k=createnodetype(TYPENAME_UNDEF);
                    if (k)
                    {
                        return k;
                    }
                    k=addlabel(locals,parser->token,STACKGET(m,0),INTTOVAL(narg++));
                    if (k)
                    {
                        return k;
                    }
                }
                else if (strcmp(parser->token,")"))
                {
                    PRINTF(m)(LOG_COMPILER,"Compiler : parameter or ')' expected (found '%s')\n",parser->token);
                    return MTLERR_SN;
                }
            }
            while(strcmp(parser->token,")"));
            k=DEFTAB(m,narg);
            if (k)
            {
                return k;
            }
            TABSET(m,VALTOPNT(STACKGET(m,1)),TYPEHEADER_LENGTH,STACKGET(m,0));
            STACKDROP(m);
        }
    }
    if (!mergetype)
    {
        STACKDROP(m);
    }
    else
    {
        k=unif(VALTOPNT(STACKPULL(m)),VALTOPNT(TABGET(newref,REF_TYPE)));
    }
    if (k)
    {
        return k;
    }

    if (!parser->next(0))
    {
        PRINTF(m)(LOG_COMPILER,"Compiler : '=' or ';;' expected (found EOF)\n");
        return MTLERR_SN;
    }
    if (!strcmp(parser->token,"="))
    {
        if (!parser->next(0))
        {
            PRINTF(m)(LOG_COMPILER,"Compiler : uncomplete type definition (found EOF)\n");
            return MTLERR_SN;
        }
        if (!strcmp(parser->token,"["))
        {
            return parsestruct();
        }
        parser->giveback();
        return parsesum();
    }
    else if (!strcmp(parser->token,";;"))
    {
        STACKDROPN(m,2);
        outputbuf->reinit();
        outputbuf->printf("Compiler : uncompleted type : ");
        echograph(outputbuf,VALTOPNT(TABGET(newref,REF_TYPE)));
        PRINTF(m)(LOG_COMPILER,"%s\n",outputbuf->getstart());
        return 0;
    }
    PRINTF(m)(LOG_COMPILER,"Compiler : '=' or ';;' expected (found '%s')\n",parser->token);
    return MTLERR_SN;
}


// compilation d'une structure (le premier '[' a été lu)
// [locals nom]
int Compiler::parsestruct()
{
    int k;

    // on crée le bloc type
    TABSET(m,newref,REF_VAL,INTTOVAL(0));
    TABSET(m,newref,REF_CODE,INTTOVAL(CODE_STRUCT));
    // [local name]

    int loop=1;
    do
    {
        if (!parser->next(0))
        {
            PRINTF(m)(LOG_COMPILER,"Compiler : field name or ']' expected (found EOF)\n");
            return MTLERR_SN;
        }
        if (islabel(parser->token))
        {
            k=STRPUSH(m,parser->token);
            if (k)
            {
                return k;
            }

            // on crée le bloc champ
            int* newfield=MALLOCCLEAR(m,REF_LENGTH);
            if (!newfield)
            {
                return MTLERR_OM;
            }
            TABSET(m,newfield,REF_NAME,STACKPULL(m));
            TABSET(m,newfield,REF_CODE,INTTOVAL(CODE_FIELD));
            k=STACKPUSH(m,PNTTOVAL(newfield));	// [newfield local name]
            if (k)
            {
                return MTLERR_OM;
            }
            addreftopackage(newfield,newpackage);
            STACKDROP(m);
            // [local name]
            k=STACKPUSH(m,TABGET(newref,REF_VAL));
            if (k)
            {
                return k;
            }
            k=STACKPUSH(m,PNTTOVAL(newref));
            if (k)
            {
                return k;
            }
            k=DEFTAB(m,FIELD_LENGTH);
            if (k)
            {
                return k;
            }
            TABSET(m,newfield,REF_VAL,STACKPULL(m));

            TABSET(m,newref,REF_VAL,INTTOVAL(1+VALTOINT(TABGET(newref,REF_VAL))));	// incrémentation

            k=createnodetype(TYPENAME_FUN);
            if (k)
            {
                return k;
            }
            k=STACKPUSH(m,TABGET(newref,REF_TYPE));
            if (k)
            {
                return k;
            }
            k=createnodetuple(1);
            if (k)
            {
                return k;
            }
            TABSET(m,VALTOPNT(STACKGET(m,1)),TYPEHEADER_LENGTH,STACKGET(m,0));	// attachement du noeud tuple au noeud fun
            STACKDROP(m);

            if ((!parser->next(0))||(strcmp(parser->token,":")))
            {
                parser->giveback();
                k=createnodetype(TYPENAME_WEAK);
            }
            else
            {
                k=creategraph(parser,PNTTOVAL(newpackage),1,locals);
                if (k)
                {
                    return k;
                }
            }
            TABSET(m,VALTOPNT(STACKGET(m,1)),TYPEHEADER_LENGTH+1,STACKGET(m,0));	// attachement du noeud resultat au noeud fun
            STACKDROP(m);
            TABSET(m,newfield,REF_TYPE,STACKPULL(m));

            outputbuf->reinit();
            outputbuf->printf("Compiler : %s : ",STRSTART(VALTOPNT(TABGET(newfield,REF_NAME))));
            echograph(outputbuf,VALTOPNT(TABGET(newfield,REF_TYPE)));
            PRINTF(m)(LOG_COMPILER,"%s\n",outputbuf->getstart());
        }
        else if (!strcmp(parser->token,"]"))
        {
            loop=0;
        }
        else
        {
            PRINTF(m)(LOG_COMPILER,"Compiler : field name or ']' expected (found '%s')\n",parser->token);
            return MTLERR_SN;
        }
    }
    while(loop);
    k=parser->parsekeyword(";;");
    if (k)
    {
        return k;
    }

    STACKDROPN(m,2);

    outputbuf->reinit();
    outputbuf->printf("Compiler : ");
    echograph(outputbuf,VALTOPNT(TABGET(newref,REF_TYPE)));
    outputbuf->printf(" : struct (%d)\n",VALTOINT(TABGET(newref,REF_VAL)));
    PRINTF(m)(LOG_COMPILER,"%s\n",outputbuf->getstart());

    return 0;
}

// compilation d'une somme
// [locals nom]
int Compiler::parsesum()
{
    int k;

    // on crée le bloc type
    TABSET(m,newref,REF_VAL,INTTOVAL(0));
    TABSET(m,newref,REF_CODE,INTTOVAL(CODE_SUM));
    // [local name]

    int loop=1;
    do
    {
        if (!parser->next(0))
        {
            PRINTF(m)(LOG_COMPILER,"Compiler : constructor expected (found EOF)\n");
            return MTLERR_SN;
        }
        if (islabel(parser->token))
        {
            k=STRPUSH(m,parser->token);
            if (k)
            {
                return k;
            }

            // on crée le bloc champ
            int* newcons=MALLOCCLEAR(m,REF_LENGTH);
            if (!newcons)
            {
                return MTLERR_OM;
            }
            TABSET(m,newcons,REF_NAME,STACKPULL(m));
            k=STACKPUSH(m,PNTTOVAL(newcons));	// [newcons local name]
            if (k)
            {
                return MTLERR_OM;
            }
            addreftopackage(newcons,newpackage);
            STACKDROP(m);
            // [local name]
            TABSET(m,newcons,REF_VAL,TABGET(newref,REF_VAL));
            TABSET(m,newref,REF_VAL,INTTOVAL(1+VALTOINT(TABGET(newref,REF_VAL))));	// incrémentation

            k=createnodetype(TYPENAME_FUN);
            if (k)
            {
                return k;
            }

            if ((parser->next(0))&&((!strcmp(parser->token,"|"))||(!strcmp(parser->token,";;"))))
            {
                parser->giveback();
                k=createnodetuple(0);
                if (k)
                {
                    return k;
                }
                TABSET(m,newcons,REF_CODE,INTTOVAL(CODE_CONS0));
            }
            else
            {
                parser->giveback();
                k=creategraph(parser,PNTTOVAL(newpackage),1,locals);
                if (k)
                {
                    return k;
                }
                k=createnodetuple(1);
                if (k)
                {
                    return k;
                }
                TABSET(m,newcons,REF_CODE,INTTOVAL(CODE_CONS));
            }
            TABSET(m,VALTOPNT(STACKGET(m,1)),TYPEHEADER_LENGTH,STACKGET(m,0));	// attachement du noeud tuple au noeud fun
            STACKDROP(m);
            TABSET(m,VALTOPNT(STACKGET(m,0)),TYPEHEADER_LENGTH+1,TABGET(newref,REF_TYPE));	// attachement du type resultat au noeud fun
            TABSET(m,newcons,REF_TYPE,STACKPULL(m));

            outputbuf->reinit();
            outputbuf->printf("Compiler : %s : ",STRSTART(VALTOPNT(TABGET(newcons,REF_NAME))));
            echograph(outputbuf,VALTOPNT(TABGET(newcons,REF_TYPE)));
            PRINTF(m)(LOG_COMPILER,"%s\n",outputbuf->getstart());
        }
        else
        {
            PRINTF(m)(LOG_COMPILER,"Compiler : constructor expected (found '%s')\n",parser->token);
            return MTLERR_SN;
        }

        if (!parser->next(0))
        {
            PRINTF(m)(LOG_COMPILER,"Compiler : '|' or ';;' expected (found EOF)\n");
            return MTLERR_SN;
        }
        if (!strcmp(parser->token,";;"))
        {
            loop=0;
        }
        else if (strcmp(parser->token,"|"))
        {
            PRINTF(m)(LOG_COMPILER,"Compiler : '|' or ';;' expected (found '%s')\n",parser->token);
            return MTLERR_SN;
        }
    }
    while(loop);
    STACKDROPN(m,2);

    outputbuf->reinit();
    outputbuf->printf("Compiler : ");
    echograph(outputbuf,VALTOPNT(TABGET(newref,REF_TYPE)));
    outputbuf->printf(" : constructor\n");
    PRINTF(m)(LOG_COMPILER,"%s\n",outputbuf->getstart());

    return 0;
}
