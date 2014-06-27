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


int Compiler::parseprogram()
{
	int k;

	while(1)
	{
		k=parseexpression();
		if (k) return k;
		if (!parser->next(0)) return 0;
		if (strcmp(parser->token,";"))
		{
			parser->giveback();
			return 0;
		}
		bc->addchar(OPdrop);
		STACKDROP(m);	// on supprime le type de l'expression
	}
}

int Compiler::parseexpression()
{
	int k;
	
	k=parsearithm();
	if (k) return k;
	
	if (!parser->next(0)) return 0;
	if (strcmp(parser->token,"::"))
    {
		parser->giveback();
		return 0;
    }
	k=parseexpression();
	if (k) return k;	// récursion

	k=createnodetuple(2);
	if (k) return k;
	k=copytype(VALTOPNT(TABGET(stdtypes,STDTYPE_fun__u0_list_u0__list_u0)));
	if (k) return k;
	k=unif_argfun();
	if (k) return k;

	bc->addchar(OPdeftabb);
	bc->addchar(2);
	
	return 0;
}


int Compiler::parsearithm()
{
	int k;
	
	k=parsea1();
	if (k) return k;
	while(1)
    {
		if (!parser->next(0)) return 0;
		if ((strcmp(parser->token,"&&"))&&(strcmp(parser->token,"||")))
        {
			parser->giveback();
			return 0;
        }
		bc->addchar(OPdup);
		if (strcmp(parser->token,"&&")) bc->addchar(OPnon);
		bc->addchar(OPelse);
		int bc_i=bc->getsize();
		bc->addshort(0);	// on prépare le champ pour le saut
		bc->addchar(OPdrop);
		k=parsea1();
		if (k) return k;
		bc->setshort(bc_i,bc->getsize());	// on règle le saut

		k=unif(VALTOPNT(STACKGET(m,0)),VALTOPNT(TABGET(stdtypes,STDTYPE_I)));
		if (k) return k;
		STACKDROP(m);
		k=unif(VALTOPNT(STACKGET(m,0)),VALTOPNT(TABGET(stdtypes,STDTYPE_I)));
		if (k) return k;
    }
}


int Compiler::parsea1()
{
	int k;
	
	if ((parser->next(0))&&(!strcmp(parser->token,"!")))
    {
		k=parsea1();
		if (k) return k;
		bc->addchar(OPnon);
		k=unif(VALTOPNT(STACKGET(m,0)),VALTOPNT(TABGET(stdtypes,STDTYPE_I)));
		if (k) return k;
		return 0;
	}
	parser->giveback();
	return parsea2();
}

int Compiler::parsea2()
{
	int k,op,typ;
	
	k=parsea3();
	if (k) return k;
	while(1)
    {
		if (!parser->next(0)) return 0;
		if (!strcmp(parser->token,"==")) { op=OPeq; typ=0;}
		else if (!strcmp(parser->token,"!=")) { op=OPne; typ=0;}
		else if (!strcmp(parser->token,"<")) { op=OPlt; typ=1;}
		else if (!strcmp(parser->token,">")) { op=OPgt; typ=1;}
		else if (!strcmp(parser->token,"<=")) { op=OPle; typ=1;}
		else if (!strcmp(parser->token,">=")) { op=OPge; typ=1;}
		else
		{
			parser->giveback();
			return 0;
		}
		k=parsea3();
		if (k) return k;
		bc->addchar(op);

		k=unif(VALTOPNT(STACKGET(m,0)),VALTOPNT(STACKGET(m,1)));
		if (k) return k;
		STACKDROP(m);
		if (typ==1)
		{
			k=unif(VALTOPNT(STACKGET(m,0)),VALTOPNT(TABGET(stdtypes,STDTYPE_I)));
			if (k) return k;
		}
		STACKSET(m,0,TABGET(stdtypes,STDTYPE_I));
    }
}

int Compiler::parsea3()
{
	int k,op,typ;
	
	k=parsea4();
	if (k) return k;
	while(1)
    {
		if (!parser->next(0)) return 0;
		if (!strcmp(parser->token,"+")) { op=OPadd; typ=1;}
		else if (!strcmp(parser->token,"-")) { op=OPsub; typ=1;}
		else
		{
			parser->giveback();
			return 0;
		}
		k=parsea4();
		if (k) return k;
		bc->addchar(op);

		int* ptyp;
		if (typ==1) ptyp=VALTOPNT(TABGET(stdtypes,STDTYPE_I));
		k=unif(VALTOPNT(STACKGET(m,0)),ptyp);
		if (k) return k;
		STACKDROP(m);
		k=unif(VALTOPNT(STACKGET(m,0)),ptyp);
		if (k) return k;
    }
}

int Compiler::parsea4()
{
	int k,op,typ;
	
	k=parsea5();
	if (k) return k;
	while(1)
    {
		if (!parser->next(0)) return 0;
		if (!strcmp(parser->token,"*")) { op=OPmul; typ=1;}
		else if (!strcmp(parser->token,"/")) { op=OPdiv; typ=1;}
		else if (!strcmp(parser->token,"%")) { op=OPmod; typ=1;}
		else
		{
			parser->giveback();
			return 0;
		}
		k=parsea5();
		if (k) return k;
		bc->addchar(op);

		int* ptyp;
		if (typ==1) ptyp=VALTOPNT(TABGET(stdtypes,STDTYPE_I));
		k=unif(VALTOPNT(STACKGET(m,0)),ptyp);
		if (k) return k;
		STACKDROP(m);
		k=unif(VALTOPNT(STACKGET(m,0)),ptyp);
		if (k) return k;
    }
}

int Compiler::parsea5()
{
	int k,op;
	
	k=parsea6();
	if (k) return k;
	while(1)
    {
		if (!parser->next(0)) return 0;
		if (!strcmp(parser->token,"&")) op=OPand;
		else if (!strcmp(parser->token,"|")) op=OPor;
		else if (!strcmp(parser->token,"^")) op=OPeor;
		else if (!strcmp(parser->token,"<<")) op=OPshl;
		else if (!strcmp(parser->token,">>")) op=OPshr;
		else
		{
			parser->giveback();
			return 0;
		}
		k=parsea6();
		if (k) return k;
		bc->addchar(op);

		k=unif(VALTOPNT(STACKGET(m,0)),VALTOPNT(TABGET(stdtypes,STDTYPE_I)));
		if (k) return k;
		STACKDROP(m);
		k=unif(VALTOPNT(STACKGET(m,0)),VALTOPNT(TABGET(stdtypes,STDTYPE_I)));
		if (k) return k;
    }
}

int Compiler::parsea6()
{
	int k;

	if (!parser->next(0)) return parseterm();
	
	if (!strcmp(parser->token,"-"))
	{
		if (!parser->next(0)) return parsea6();
		if (isdecimal(parser->token))	// gestion des entiers
		{
			int i=-mtl_atoi(parser->token);
			bc->addchar(OPint);
			bc->addint(i);
			return STACKPUSH(m,TABGET(stdtypes,STDTYPE_I));
		}
		parser->giveback();
		k=parsea6();
		if (k) return k;
		bc->addchar(OPneg);
		return unif(VALTOPNT(STACKGET(m,0)),VALTOPNT(TABGET(stdtypes,STDTYPE_I)));
	}
	else if (!strcmp(parser->token,"~"))
	{
		k=parsea6();
		if (k) return k;
		bc->addchar(OPnot);
		return unif(VALTOPNT(STACKGET(m,0)),VALTOPNT(TABGET(stdtypes,STDTYPE_I)));
	}
	parser->giveback();
	return parseterm();
}


