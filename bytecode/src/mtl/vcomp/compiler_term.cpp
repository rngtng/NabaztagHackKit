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

// switch 8 / 32 bits
void Compiler::bc_byte_or_int(int val,int opbyte,int opint)
{
		if ((val>=0)&&(val<=255))
		{
			bc->addchar(opbyte);
			bc->addchar(val);
		}
		else
		{
			bc->addchar(OPint);
			bc->addint(val);
			bc->addchar(opint);
		}
}

void Compiler::bcint_byte_or_int(int val)
{
		if ((val>=0)&&(val<=255))
		{
			bc->addchar(OPintb);
			bc->addchar(val);
		}
		else
		{
			bc->addchar(OPint);
			bc->addint(val);
		}
}


int Compiler::parseterm()
{
	int k;

	if (!parser->next(0))
	{
		PRINTF(m)(LOG_COMPILER,"Compiler : term expected (found EOF)\n");
		return MTLERR_SN;
	}
	if (!strcmp(parser->token,"("))	// gestion des parenthèses
	{
		k=parseprogram();
		if (k) return k;
		return parser->parsekeyword(")");
	}
	else if (!strcmp(parser->token,"["))	// gestion des tuples
	{
		if (parser->next(0))
		{
			if (islabel(parser->token))
			{
				int *p;
				if ((p=searchref(PNTTOVAL(newpackage),parser->token))	// recherche dans les autres globales
					&&(VALTOINT(TABGET(p,REF_CODE))==CODE_FIELD))
				return parsefields(p);
			}
			parser->giveback();
		}
		int nval=0;
		while(1)
		{
			if (!parser->next(0))
			{
				PRINTF(m)(LOG_COMPILER,"Compiler : ']' expected (found EOF)\n");
				return MTLERR_SN;
			}
			if (!strcmp(parser->token,"]"))
			{
				bc_byte_or_int(nval,OPdeftabb,OPdeftab);
				return createnodetuple(nval);
			}
			parser->giveback();
			k=parseexpression();
			if (k) return k;
			nval++;
		}
	}
	else if (!strcmp(parser->token,"{"))	// gestion des tableaux
	{
		int nval=0;
		k=createnodetype(TYPENAME_TAB);
		if (k) return k;
		k=createnodetype(TYPENAME_UNDEF);
		if (k) return k;
		TABSET(m,VALTOPNT(STACKGET(m,1)),TYPEHEADER_LENGTH,STACKGET(m,0));
		int* p=VALTOPNT(STACKPULL(m));

		while(1)
		{
			if (!parser->next(0))
			{
				PRINTF(m)(LOG_COMPILER,"Compiler : '}' expected (found EOF)\n");
				return MTLERR_SN;
			}
			if (!strcmp(parser->token,"}"))
			{
				bc_byte_or_int(nval,OPdeftabb,OPdeftab);
				return 0;
			}
			parser->giveback();
			k=parseexpression();
			if (k) return k;
			k=unif(VALTOPNT(STACKGET(m,0)),p);
			if (k) return k;
			STACKDROP(m);
			nval++;
		}
	}
	else if (!strcmp(parser->token,"if"))	// gestion du if
		return parseif();
	else if (!strcmp(parser->token,"let"))	// gestion du let
		return parselet();
	else if (!strcmp(parser->token,"set"))	// gestion du set
		return parseset();
	else if (!strcmp(parser->token,"while"))	// gestion du while
		return parsewhile();
	else if (!strcmp(parser->token,"for"))	// gestion du for
		return parsefor();
	else if (!strcmp(parser->token,"match"))	// gestion du match
		return parsematch();
	else if (!strcmp(parser->token,"call"))	// gestion du while
		return parsecall();
	else if (!strcmp(parser->token,"update"))	// gestion du update
		return parseupdate();
	else if (!strcmp(parser->token,"nil"))	// gestion du nil
	{
		bc->addchar(OPnil);
		return createnodetype(TYPENAME_UNDEF);
	}
	else if (!strcmp(parser->token,"'"))	// gestion des 'char
	{
		if (!parser->next(0))
		{
			PRINTF(m)(LOG_COMPILER,"Compiler : 'char expected (found EOF)\n");
			return MTLERR_SN;
		}
		bcint_byte_or_int(parser->token[0]&255);
		k=parser->parsekeyword("'");
		if (k) return k;
		return STACKPUSH(m,TABGET(stdtypes,STDTYPE_I));
	}
	else if (islabel(parser->token))	// gestion des appels de fonctions ou références
		return parseref();
	else if (isdecimal(parser->token))	// gestion des entiers
	{
		int i=mtl_atoi(parser->token);
		bcint_byte_or_int(i);
		return STACKPUSH(m,TABGET(stdtypes,STDTYPE_I));
	}
	else if ((parser->token[0]=='0')&&(parser->token[1]=='x')
		&&(ishexadecimal(parser->token+2)))	// gestion des entiers
	{
		int i=mtl_htoi(parser->token+2);
		bcint_byte_or_int(i);
		return STACKPUSH(m,TABGET(stdtypes,STDTYPE_I));
	}
	else if (parser->token[0]=='"')	// gestion des chaines
	{
		k=parser->getstring(m,'"');
		if (k) return k;
		int val=INTTOVAL(nblabels(globals));
		k=addlabel(globals,"",val,STACKGET(m,0));
		if (k) return k;	// enregistrement d'une nouvelle globale
		STACKDROP(m);
		bc_byte_or_int(VALTOINT(val),OPgetglobalb,OPgetglobal);
		return STACKPUSH(m,TABGET(stdtypes,STDTYPE_S));
	}
	else if (!strcmp(parser->token,"#"))	// gestion des pointeurs sur fonction statique ou dynamique
		return parsepntfun();
	else
	{
		PRINTF(m)(LOG_COMPILER,"Compiler : unexpected term '%s'\n",parser->token);
		return MTLERR_SN;
	}
}

// parsing de la création d'une structure
// p contient la référence du premier champ
int Compiler::parsefields(int* p)
{
	int k;
	int* type=VALTOPNT(TABGET(VALTOPNT(TABGET(p,REF_VAL)),FIELD_TYPE));
	int n=VALTOINT(TABGET(type,REF_VAL));
	bc_byte_or_int(n,OPmktabb,OPmktab);	// création de la structure
	k=copytype(VALTOPNT(TABGET(type,REF_TYPE)));	// création du type
	if (k) return k;

	int loop=1;
	while(loop)
	{
		k=copytype(VALTOPNT(TABGET(p,REF_TYPE)));	// création du type du champ
		if (k) return k;
		k=unif(VALTOPNT(STACKGET(m,1)),
			VALTOPNT(TABGET(argsfromfun(VALTOPNT(STACKGET(m,0))),TYPEHEADER_LENGTH)) );
		if (k) return k;
		STACKSET(m,0,TABGET(VALTOPNT(STACKGET(m,0)),TYPEHEADER_LENGTH+1));
		
		k=parser->parsekeyword(":");
		if (k) return k;
		k=parseexpression();
		if (k) return k;
		k=unif(VALTOPNT(STACKGET(m,0)),VALTOPNT(STACKGET(m,1)));
		if (k) return k;
		STACKDROPN(m,2);
		bc_byte_or_int( VALTOINT(TABGET(VALTOPNT(TABGET(p,REF_VAL)),FIELD_NUM)), OPsetstructb,OPsetstruct);

		
		if (!parser->next(0))
		{
			PRINTF(m)(LOG_COMPILER,"Compiler : ']' expected (found EOF)\n");
			return MTLERR_SN;
		}
		if (!strcmp(parser->token,"]"))
		{
			return 0;
		}
		loop=0;
		if (islabel(parser->token))
		{
			if ((p=searchref(PNTTOVAL(newpackage),parser->token))	// recherche dans les autres globales
				&&(VALTOINT(TABGET(p,REF_CODE))==CODE_FIELD))
				loop=1;
		}
	}
	PRINTF(m)(LOG_COMPILER,"Compiler : ']' expected (found '%s')\n",parser->token);
	return MTLERR_SN;
}


// parsing d'une référence quelconque (déjà présente dans parser->token)
int Compiler::parseref()
{
	int k;

	int val;
	int ref;
	if (!searchlabel_byname(locals,parser->token,&val,&ref))	// recherche dans les variables locales
	{
		bc_byte_or_int(VALTOINT(val),OPgetlocalb,OPgetlocal);
		k=STACKPUSH(m,ref);
		if (k) return k;
		return parsegetpoint();
	}
	val=-1;

	int *p;
	p=searchref(PNTTOVAL(newpackage),parser->token);	// recherche dans les autres globales
	if (p)	// recherche dans les autres globales
	{
		ref=PNTTOVAL(p);
		int code=VALTOINT(TABGET(p,REF_CODE));
		if (code==CODE_CONS0)
		{
			bcint_byte_or_int(VALTOINT(TABGET(p,REF_VAL)));
			bc->addchar(OPdeftabb);
			bc->addchar(1);
			return copytype(VALTOPNT(TABGET(VALTOPNT(TABGET(p,REF_TYPE)),TYPEHEADER_LENGTH+1)));
		}
		else if (code==CODE_CONS)
		{
			bcint_byte_or_int(VALTOINT(TABGET(p,REF_VAL)));
			k=parseexpression();
			if (k) return k;
			bc->addchar(OPdeftabb);
			bc->addchar(2);
			k=createnodetuple(1);
			if (k) return k;
			k=copytype(VALTOPNT(TABGET(p,REF_TYPE)));
			if (k) return k;
			return unif_argfun();
		}
		val=0;
	}

	if (val!=-1)
	{
		int* p=VALTOPNT(ref);
		int code=VALTOINT(TABGET(p,REF_CODE));

		if (code>=0)	// appel d'une fonction
		{
			int i;
			for(i=0;i<code;i++)
			{
				k=parseexpression();
				if (k)
				{
					char * name=STRSTART(VALTOPNT(TABGET(p,REF_NAME)));
					PRINTF(m)(LOG_COMPILER,"Compiler : function %s requires %d arguments\n",name, code);
					return k;
				}
			}
			if ((TABGET(p,REF_VAL)!=NIL)&&(TABGET(VALTOPNT(TABGET(p,REF_VAL)),FUN_NBLOCALS)==NIL))
			{	// appel natif
				bc->addchar((char)VALTOINT(TABGET(VALTOPNT(TABGET(p,REF_VAL)),FUN_BC)));
			}
			else
			{	// appel normal
				bcint_byte_or_int(VALTOINT(TABGET(p,REF_PACKAGE)));
				bc->addchar(OPexec);
			}
			k=createnodetuple(code);
			if (k) return k;
			if (p!=newref)
			{
				k=copytype(VALTOPNT(TABGET(p,REF_TYPE)));
				if (k) return k;
			} else
			{
				k=STACKPUSH(m,TABGET(p,REF_TYPE));
				if (k) return k;
			}
			return unif_argfun();
		}
		else if (code==CODE_VAR || code==CODE_CONST)	// lecture d'une référence
		{
			bc_byte_or_int(VALTOINT(TABGET(p,REF_PACKAGE)),OPgetglobalb,OPgetglobal);
			k=STACKPUSH(m,TABGET(p,REF_TYPE));
			if (k) return k;
			return parsegetpoint();
		}
	}

	PRINTF(m)(LOG_COMPILER,"Compiler : unknown label '%s'\n",parser->token);
	return MTLERR_SN;
}

// parsing (.Term/champ)* en lecture
int Compiler::parsegetpoint()
{
	int k;
	while(1)
	{
		if (!parser->next(0)) return 0;
		if (strcmp(parser->token,"."))
		{
			parser->giveback();
			return 0;
		}
		if (!parser->next(0))
		{
			PRINTF(m)(LOG_COMPILER,"Compiler : expression or field name expected (found EOF)\n");
			return MTLERR_SN;
		}

		int* p;
		if ((islabel(parser->token))
			&&(p=searchref(PNTTOVAL(newpackage),parser->token))	// recherche dans les autres globales
			&&(VALTOINT(TABGET(p,REF_CODE))==CODE_FIELD))
		{
			bc_byte_or_int(VALTOINT(TABGET(VALTOPNT(TABGET(p,REF_VAL)),FIELD_NUM)),OPfetchb,OPfetch);
			k=createnodetuple(1);
			if (k) return k;
			k=copytype(VALTOPNT(TABGET(p,REF_TYPE)));
			if (k) return k;
			k=unif_argfun();
			if (k) return k;
		}
		else
		{
			parser->giveback();
			k=parseterm();
			if (k) return k;
			bc->addchar(OPfetch);
			k=createnodetuple(2);
			if (k) return k;
			k=copytype(VALTOPNT(TABGET(stdtypes,STDTYPE_fun__tab_u0_I__u0)));
			if (k) return k;
			k=unif_argfun();
			if (k) return k;
		}
	}
}


// parsing du if ... then ... else (le 'if' a déjà été lu)
int Compiler::parseif()
{
	int k;
	
	k=parseexpression();	// lire la condition
	if (k) return k;

	k=unif(VALTOPNT(STACKGET(m,0)),VALTOPNT(TABGET(stdtypes,STDTYPE_I)));
	if (k) return k;
	STACKDROP(m);
	k=parser->parsekeyword("then");
	if (k) return k;

	bc->addchar(OPelse);
	int bc_else=bc->getsize();
	bc->addshort(0);	// on prépare le champ pour le saut 'else'

	k=parseexpression();	// lire l'expression 'then'
	if (k) return k;

	bc->addchar(OPgoto);
	int bc_goto=bc->getsize();
	bc->addshort(0);	// on prépare le champ pour le saut du 'then'

	bc->setshort(bc_else,bc->getsize());	// on règle le saut du else

	if ((parser->next(0))&&(!strcmp(parser->token,"else")))
	{
		k=parseexpression();	// lire l'expression 'else'
		if (k) return k;
	}
	else	// pas de else, on remplace par nil
	{
		parser->giveback();
		bc->addchar(OPnil);
		k=createnodetype(TYPENAME_UNDEF);
		if (k) return k;
	}
	bc->setshort(bc_goto,bc->getsize());	// on règle le saut du 'goto'

	k=unif(VALTOPNT(STACKGET(m,0)),VALTOPNT(STACKGET(m,1)));
	if (k) return k;
	STACKDROP(m);
	return 0;

}

// parsing du while ... do ... (le 'while' a déjà été lu)
int Compiler::parsewhile()
{
	int k;
	
	bc->addchar(OPnil);	// on empile le premier résultat

	int bc_while=bc->getsize();		// on retient la position pour le saut 'while'
	k=parseexpression();	// lire la condition
	if (k) return k;

	k=unif(VALTOPNT(STACKGET(m,0)),VALTOPNT(TABGET(stdtypes,STDTYPE_I)));
	if (k) return k;
	STACKDROP(m);
	k=parser->parsekeyword("do");
	if (k) return k;
	bc->addchar(OPelse);
	int bc_end=bc->getsize();
	bc->addshort(0);	// on prépare le champ pour le saut 'end'
	bc->addchar(OPdrop);	// on ignore le résultat précédent

	k=parseexpression();	// lire l'expression 'do'
	if (k) return k;

	bc->addchar(OPgoto);
	bc->addshort(bc_while);	// on retourne à la condition
	
	bc->setshort(bc_end,bc->getsize());	// on règle le saut 'end'

	return 0;
}


// parsing du for ...=... ; cond ; nextvalue do ... (le 'for' a déjà été lu)
int Compiler::parsefor()
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
	k=STRPUSH(m,parser->token);
	if (k) return k;
	// [name_it]
	k=parser->parsekeyword("=");
	if (k) return k;
	k=parseexpression();	// lire la valeur d'initialisation
	if (k) return k;
	// [type_init name_it]
	int i=nblabels(locals);
	k=addlabel(locals,STRSTART(VALTOPNT(STACKGET(m,1))),INTTOVAL(i),STACKGET(m,0));
	if (k) return k;
	if (i+1>nblocals) nblocals=i+1;	// nombre maximum de variables locales

	// [type_init name_it]
	bc_byte_or_int(i,OPsetlocalb,OPsetlocal);

	k=parser->parsekeyword(";");
	if (k) return k;

	bc->addchar(OPnil);	// on empile le premier résultat

	int bc_cond=bc->getsize();		// on retient la position pour le saut 'while'
	k=parseexpression();	// lire la condition
	if (k) return k;
	k=unif(VALTOPNT(STACKGET(m,0)),VALTOPNT(TABGET(stdtypes,STDTYPE_I)));
	if (k) return k;
	STACKDROP(m);

	bc->addchar(OPelse);
	int bc_end=bc->getsize();
	bc->addshort(0);	// on prépare le champ pour le saut 'end'

	if (!parser->next(0))
	{
		PRINTF(m)(LOG_COMPILER,"Compiler : 'do' expected (found EOF)\n");
		return MTLERR_SN;
	}
	if (!strcmp(parser->token,";"))
	{
		bc->addchar(OPgoto);
		int bc_expr=bc->getsize();
		bc->addshort(0);	// on prépare le champ pour le saut 'expr'

		int bc_next=bc->getsize();
		k=parseexpression();	// lire la valeur next
		if (k) return k;
		k=unif(VALTOPNT(STACKGET(m,0)),VALTOPNT(STACKGET(m,1)));
		if (k) return k;
		STACKDROP(m);
		bc_byte_or_int(i,OPsetlocalb,OPsetlocal);	// mise à jour de l'itérateur
		bc->addchar(OPgoto);
		bc->addshort(bc_cond);

		k=parser->parsekeyword("do");
		if (k) return k;

		bc->setshort(bc_expr,bc->getsize());
		bc->addchar(OPdrop);	// on ignore le résultat précédent
		k=parseexpression();	// lire la valeur itérée
		if (k) return k;
		bc->addchar(OPgoto);
		bc->addshort(bc_next);	// on retourne à l'itérateur
	}
	else
	{
		parser->giveback();
		k=unif(VALTOPNT(STACKGET(m,0)),VALTOPNT(TABGET(stdtypes,STDTYPE_I)));
		if (k) return k;
		k=parser->parsekeyword("do");
		if (k) return k;
		bc->addchar(OPdrop);	// on ignore le résultat précédent
		k=parseexpression();	// lire la valeur itérée
		if (k) return k;
		bc_byte_or_int(i,OPgetlocalb,OPgetlocal);	// i+1
		bc->addchar(OPintb);
		bc->addchar(1);
		bc->addchar(OPadd);
		bc_byte_or_int(i,OPsetlocalb,OPsetlocal);	// mise à jour de l'itérateur
		bc->addchar(OPgoto);
		bc->addshort(bc_cond);	// on retourne à la condition
	}
	bc->setshort(bc_end,bc->getsize());

	removenlabels(locals,1);

	STACKSET(m,2,STACKGET(m,0));
	STACKDROPN(m,2);

	return 0;
}

// parsing du match ... with ... (le 'match' a déjà été lu)
int Compiler::parsematch()
{
	int k;

	k=parseexpression();	// lire l'objet
	if (k) return k;
	k=createnodetype(TYPENAME_UNDEF);	// préparer le type du résultat
	if (k) return k;
	// [result src]

	k=parser->parsekeyword("with");
	if (k) return k;

	int end;
	k=parsematchcons(&end);
	if (k) return k;
	STACKSET(m,1,STACKGET(m,0));
	STACKDROP(m);
	return 0;
}

int Compiler::parsematchcons(int* end)
{
	int k;
	k=parser->parsekeyword("(");
	if (k) return k;
	if (!parser->next(0))
	{
		PRINTF(m)(LOG_COMPILER,"Compiler : constructor expected (found EOF)\n");
		return MTLERR_SN;
	}
	int* p=NULL;
	if (!strcmp(parser->token,"_"))	// cas par défaut
	{
		bc->addchar(OPdrop);
		k=parser->parsekeyword("->");
		if (k) return k;
		k=parseprogram();	// lire le résultat
		if (k) return k;
		// [type_result result src]
		k=unif(VALTOPNT(STACKGET(m,0)),VALTOPNT(STACKGET(m,1)));
		if (k) return k;
		STACKDROP(m);
		// [result src]
		k=parser->parsekeyword(")");
		if (k) return k;
		*end=bc->getsize();
		return 0;
	}
	if ((islabel(parser->token))	// constructeur
		&&(p=searchref(PNTTOVAL(newpackage),parser->token)))	// recherche dans les autres globales
	{
		int code=VALTOINT(TABGET(p,REF_CODE));
		if ((code==CODE_CONS)||(code==CODE_CONS0))
		{
			// [result src]
			bc->addchar(OPfirst);
			bcint_byte_or_int(VALTOINT(TABGET(p,REF_VAL)));
			bc->addchar(OPeq);
			bc->addchar(OPelse);
			int bc_else=bc->getsize();		// on retient la position pour le saut 'else'
			bc->addshort(0);

			int nloc=nblabels(locals);	// on sauvegarde le nombre de locales
			if (code==CODE_CONS)
			{
				bc->addchar(OPfetchb);
				bc->addchar(1);
				k=parselocals();
				if (k) return k;
				k=createnodetuple(1);
				if (k) return k;
			}
			else
			{
				bc->addchar(OPdrop);
				k=createnodetuple(0);
				if (k) return k;
			}
			// [[locals] result src]
			int newnloc=nblabels(locals);	// on sauvegarde le nombre de locales
			if (newnloc>nblocals) nblocals=newnloc;	// nombre maximum de variables locales
			int nloctodelete=newnloc-nloc;	// combien de variables locales ont été crées ?

			k=copytype(VALTOPNT(TABGET(p,REF_TYPE)));
			if (k) return k;
			k=unif_argfun();
			if (k) return k;
			// [type_src result src]
			k=unif(VALTOPNT(STACKGET(m,0)),VALTOPNT(STACKGET(m,2)));
			if (k) return k;
			STACKDROP(m);
			// [result src]
			k=parser->parsekeyword("->");
			if (k) return k;
			k=parseprogram();	// lire le résultat
			if (k) return k;
			// [type_result result src]
			k=unif(VALTOPNT(STACKGET(m,0)),VALTOPNT(STACKGET(m,1)));
			if (k) return k;
			STACKDROP(m);
			// [result src]
			removenlabels(locals,nloctodelete);
			k=parser->parsekeyword(")");
			if (k) return k;
			bc->addchar(OPgoto);
			int bc_goto=bc->getsize();		// on retient la position pour le saut 'else'
			bc->addshort(0);
			bc->setshort(bc_else,bc->getsize());
			if ((parser->next(0))&&(!strcmp(parser->token,"|")))
			{
				k=parsematchcons(end);
				if (k) return k;
				bc->setshort(bc_goto,*end);
				return 0;
			}
			parser->giveback();
			bc->addchar(OPdrop);
			bc->addchar(OPnil);
			*end=bc->getsize();
			bc->setshort(bc_goto,*end);
			return 0;
		}
	}
	PRINTF(m)(LOG_COMPILER,"Compiler : constructor expected (found '%s')\n",parser->token);
	return MTLERR_SN;
}


// parsing du let ... -> ... in (le 'let' a déjà été lu)
int Compiler::parselet()
{
	int k;
	
	k=parseexpression();	// lire la source
	if (k) return k;
	k=parser->parsekeyword("->");
	if (k) return k;

	int nloc=nblabels(locals);	// on sauvegarde le nombre de locales

	k=parselocals();
	if (k) return k;

	int newnloc=nblabels(locals);	// on sauvegarde le nombre de locales
	if (newnloc>nblocals) nblocals=newnloc;	// nombre maximum de variables locales
	int nloctodelete=newnloc-nloc;	// combien de variables locales ont été crées ?

	k=unif(VALTOPNT(STACKGET(m,0)),VALTOPNT(STACKGET(m,1)));
	if (k) return k;
	STACKDROPN(m,2);
	k=parser->parsekeyword("in");
	if (k) return k;

	k=parseexpression();	// lire l'expression du let
	if (k) return k;
	removenlabels(locals,nloctodelete);
	return 0;
}


// parsing de variables locales structurées
int Compiler::parselocals()
{
	int k;
	
	if (!parser->next(0))
	{
		PRINTF(m)(LOG_COMPILER,"Compiler : term expected (found EOF)\n");
		return MTLERR_SN;
	}
	else if (!strcmp(parser->token,"["))
    {
		int n=0;
		while(1)
        {
			if (!parser->next(0))
			{
				PRINTF(m)(LOG_COMPILER,"Compiler : ']' expected (found EOF)\n");
				return MTLERR_SN;
			}
			if (!strcmp(parser->token,"]"))
            {
				bc->addchar(OPdrop);
				return createnodetuple(n);
			}
			else if (!strcmp(parser->token,"_"))
            {
				k=createnodetype(TYPENAME_UNDEF);
				if (k) return k;
				n++;
			}
			else
			{
				parser->giveback();
				bc->addchar(OPdup);
				bc_byte_or_int(n,OPfetchb,OPfetch);
				k=parselocals();
				if (k) return k;
				n++;
			}
		}
	}
	else if (!strcmp(parser->token,"("))
    {
		k=createnodetype(TYPENAME_LIST);
		if (k) return k;
		int* plist=VALTOPNT(STACKGET(m,0));
		k=createnodetype(TYPENAME_UNDEF);
		if (k) return k;
		int* pval=VALTOPNT(STACKGET(m,0));
		TABSET(m,plist,TYPEHEADER_LENGTH,STACKGET(m,0));

		while(1)	// la liste est dans la pile
        {
			if (!parser->next(0)) return parselocals();
			if (!strcmp(parser->token,"_"))
            {
				if ((parser->next(0))&&(!strcmp(parser->token,")")))
				{
					bc->addchar(OPdrop);
					STACKDROP(m);
					return 0;
				}
				parser->giveback();
			}
			else if (islabel(parser->token))
			{
				int i=nblabels(locals);
				k=createnodetype(TYPENAME_UNDEF);
				if (k) return k;
				k=addlabel(locals,parser->token,INTTOVAL(i),STACKGET(m,0));
				if (k) return k;
				if ((parser->next(0))&&(!strcmp(parser->token,")")))
				{
					bc_byte_or_int(i,OPsetlocalb,OPsetlocal);
					k=unif(VALTOPNT(STACKGET(m,0)),plist);
					if (k) return k;
					STACKDROPN(m,2);
					return 0;
				}
				parser->giveback();
				bc->addchar(OPfirst);
				bc_byte_or_int(i,OPsetlocalb,OPsetlocal);
				k=unif(VALTOPNT(STACKGET(m,0)),pval);
				if (k) return k;
				STACKDROP(m);
			}
			else
			{
				parser->giveback();
				bc->addchar(OPfirst);
				k=parselocals();
				if (k) return k;
				k=unif(VALTOPNT(STACKGET(m,0)),pval);
				if (k) return k;
				STACKDROP(m);
			}
			if (!parser->next(0))
			{
				PRINTF(m)(LOG_COMPILER,"Compiler : '::' expected (found EOF)\n");
				return MTLERR_SN;
			}
			if (!strcmp(parser->token,"::"))
            {
				bc->addchar(OPfetchb);
				bc->addchar(1);
			}
			else
			{
				PRINTF(m)(LOG_COMPILER,"Compiler : '::' expected (found '%s')\n",parser->token);
				return MTLERR_SN;
			}
		}
	}
	else if (!strcmp(parser->token,"_"))
	{
		k=createnodetype(TYPENAME_UNDEF);
		if (k) return k;
		bc->addchar(OPdrop);
		return 0;
	}
	else if (islabel(parser->token))
	{
		k=createnodetype(TYPENAME_UNDEF);
		if (k) return k;
		int i=nblabels(locals);
		bc_byte_or_int(i,OPsetlocalb,OPsetlocal);

		k=addlabel(locals,parser->token,INTTOVAL(i),STACKGET(m,0));
		if (k) return k;
		return 0;
	}
	PRINTF(m)(LOG_COMPILER,"Compiler : unexpected term '%s'\n",parser->token);
	return MTLERR_SN;
}

// parsing du update ... with ... (le 'update' a déjà été lu)
int Compiler::parseupdate()
{
	int k;
	
	k=parseexpression();	// lire la source
	if (k) return k;
	k=parser->parsekeyword("with");
	if (k) return k;
	k=parser->parsekeyword("[");
	if (k) return k;

	k=parseupdatevals();
	if (k) return k;

	k=unif(VALTOPNT(STACKGET(m,0)),VALTOPNT(STACKGET(m,1)));
	if (k) return k;
	STACKDROP(m);
	return 0;
}

// parsing des valeurs d'un update (le premier '[' a déjà été lu)
int Compiler::parseupdatevals()
{
	int k;

	int n=0;
	while(1)
    {
		if (!parser->next(0))
		{
			PRINTF(m)(LOG_COMPILER,"Compiler : ']' expected (found EOF)\n");
			return MTLERR_SN;
		}
		if (!strcmp(parser->token,"["))
		{
			bc->addchar(OPdup);
			bc_byte_or_int(n,OPfetchb,OPfetch);
			parseupdatevals();
			bc->addchar(OPdrop);
			n++;
		}
		else if (!strcmp(parser->token,"]"))
		{
			return createnodetuple(n);
		}
		else if (!strcmp(parser->token,"_"))
        {
			k=createnodetype(TYPENAME_UNDEF);
			if (k) return k;
			n++;
		}
		else
		{
			parser->giveback();
			k=parseexpression();
			if (k) return k;
			bc_byte_or_int(n,OPsetstructb,OPsetstruct);
			n++;
		}
	}
}


// parsing d'un set ... = ... (le 'set' a déjà été lu)
int Compiler::parseset()
{
	int k;
	int val;
	int ref;
	int opstore=-1;

	if (!parser->next(0))
	{
		PRINTF(m)(LOG_COMPILER,"Compiler : reference expected (found EOF)\n");
		return MTLERR_SN;
	}
	if (!islabel(parser->token))
	{
		PRINTF(m)(LOG_COMPILER,"Compiler : reference expected (found '%s')\n",parser->token);
		return MTLERR_SN;
	}

	if (!searchlabel_byname(locals,parser->token,&val,&ref))	// recherche dans les variables locales
	{
		k=STACKPUSH(m,ref);
		if (k) return k;
		k=parsesetpoint(1,VALTOINT(val),&opstore);
		if (k) return k;
	}
	else
	{
		val=-1;
		int *p;
		p=searchref(PNTTOVAL(newpackage),parser->token);	// recherche dans les autres globales
		if (p)	// recherche dans les autres globales
		{
			ref=PNTTOVAL(p);
			val=0;
		}
		if (val!=-1)
		{
			int* p=VALTOPNT(ref);
			int code=VALTOINT(TABGET(p,REF_CODE));

			if (code==CODE_VAR)	// variable
			{
				k=STACKPUSH(m,TABGET(p,REF_TYPE));
				if (k) return k;
				k=parsesetpoint(0,VALTOINT(TABGET(p,REF_PACKAGE)),&opstore);
				if (k) return k;

				// la variable a été settée au moins une fois maintenant
				TABSET(m,p,REF_SET,INTTOVAL(2));
			}
			else if (code == CODE_CONST) // constante
				{
					PRINTF(m)(LOG_COMPILER,"Compiler : '%s' is a const, it cannot be set\n",parser->token);
					return MTLERR_SN;
				}
		}
	}
	if (opstore==-1)
	{
		PRINTF(m)(LOG_COMPILER,"Compiler : reference expected (found '%s')\n",parser->token);
		return MTLERR_SN;
	}
	k=parser->parsekeyword("=");
	if (k) return k;
	k=parseexpression();	// lire la source
	if (k) return k;
	bc->addchar(opstore);

	k=unif(VALTOPNT(STACKGET(m,0)),VALTOPNT(STACKGET(m,1)));
	if (k) return k;
	STACKDROP(m);
	return 0;
}

// parsing (.Term/champ)* en écriture
int Compiler::parsesetpoint(int local,int ind,int* opstore)
{
	int k;

	if (!parser->next(0)) return 0;
	if (strcmp(parser->token,"."))
	{
		bcint_byte_or_int(ind);
		if (local) *opstore=OPsetlocal2;
		else *opstore=OPsetglobal;
		parser->giveback();
		return 0;
	}
	if (local) bc_byte_or_int(ind,OPgetlocalb,OPgetlocal);
	else bc_byte_or_int(ind,OPgetglobalb,OPgetglobal);
	*opstore=OPstore;
	while(1)
	{
		if (!parser->next(0))
		{
			PRINTF(m)(LOG_COMPILER,"Compiler : expression or field name expected (found EOF)\n");
			return MTLERR_SN;
		}

		ind=-1;
		int* p;
		if ((islabel(parser->token))
			&&(p=searchref(PNTTOVAL(newpackage),parser->token))	// recherche dans les autres globales
			&&(VALTOINT(TABGET(p,REF_CODE))==CODE_FIELD))
		{
			ind=VALTOINT(TABGET(VALTOPNT(TABGET(p,REF_VAL)),FIELD_NUM));
			k=createnodetuple(1);
			if (k) return k;
			k=copytype(VALTOPNT(TABGET(p,REF_TYPE)));
			if (k) return k;
			k=unif_argfun();
			if (k) return k;
		}
		else
		{
			parser->giveback();

			k=parseterm();
			if (k) return k;
			k=createnodetuple(2);
			if (k) return k;
			k=copytype(VALTOPNT(TABGET(stdtypes,STDTYPE_fun__tab_u0_I__u0)));
			if (k) return k;
			k=unif_argfun();
			if (k) return k;
		}
		if (!parser->next(0)) return 0;
		if (strcmp(parser->token,"."))
		{
			parser->giveback();
			if (ind>=0)
			{
				bcint_byte_or_int(ind);
			}
			return 0;
		}
		if (ind>=0) bc_byte_or_int(ind,OPfetchb,OPfetch);
		else bc->addchar(OPfetch);
	}
}

// parsing d'un pointeur de fonction
int Compiler::parsepntfun()
{
	int k;

	int val;
	int ref;

	if (!parser->next(0))
	{
		PRINTF(m)(LOG_COMPILER,"Compiler : function name expected (found EOF)\n");
		return MTLERR_SN;
	}
	val=-1;
		int *p;
		p=searchref(PNTTOVAL(newpackage),parser->token);	// recherche dans les autres globales
		if (p)	// recherche dans les autres globales
		{
			ref=PNTTOVAL(p);
			val=0;
		}
	if (val!=-1)
	{
		int* p=VALTOPNT(ref);
		int code=VALTOINT(TABGET(p,REF_CODE));

		if (code>=0)
		{
			int v=0;
			if (TABGET(p,REF_PACKAGE)!=NIL) v=VALTOINT(TABGET(p,REF_PACKAGE));
			else
			{
				char *ppp=parser->token;
				v=-VALTOINT(TABGET(VALTOPNT(TABGET(p,REF_VAL)),FUN_BC));
			}
			// pointeur d'une fonction virtuelle
			bcint_byte_or_int(v);
			if (p!=newref)
			{
				k=copytype(VALTOPNT(TABGET(p,REF_TYPE)));
				if (k) return k;
			}
			else k=STACKPUSH(m,TABGET(p,REF_TYPE));
			if (k) return k;
			return 0;
		}
	}

	PRINTF(m)(LOG_COMPILER,"Compiler : function name expected (found '%s')\n",parser->token);
	return MTLERR_SN;
}


// parsing du call ... ... (le 'call' a déjà été lu)
int Compiler::parsecall()
{
	int k;
	
	k=parseexpression();	// lire la fonction
	if (k) return k;

	if ((parser->next(0))&&(!strcmp(parser->token,"[")))
	{
		int nval=0;
		while(1)
		{
			if (!parser->next(0))
			{
				PRINTF(m)(LOG_COMPILER,"Compiler : ']' expected (found EOF)\n");
				return MTLERR_SN;
			}
			if (!strcmp(parser->token,"]"))
			{
				bc_byte_or_int(nval,OPcallrb,OPcallr);
				k=createnodetuple(nval);
				if (k) return k;
				k=createnodetuple(2);
				if (k) return k;
				k=copytype(VALTOPNT(TABGET(stdtypes,STDTYPE_fun__fun_u0_u1_u0__u1)));
				if (k) return k;
				return unif_argfun();
			}
			parser->giveback();
			k=parseexpression();
			if (k) return k;
			nval++;
		}		
	}
	else
	{
		parser->giveback();
		k=parseexpression();	// lire les arguments
		if (k) return k;

		bc->addchar(OPcall);
		k=createnodetuple(2);
		if (k) return k;
		k=copytype(VALTOPNT(TABGET(stdtypes,STDTYPE_fun__fun_u0_u1_u0__u1)));
		if (k) return k;
		return unif_argfun();
	}
}

