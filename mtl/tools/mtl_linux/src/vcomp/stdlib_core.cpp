//-------------------
// MV
// version WIN32 et POCKETPC
// Sylvain Huet
// Premiere version : 07/01/2003
// Derniere mise a jour : 07/01/2003
//

#include "param.h"
#include <stdio.h>
#include <stdint.h>

#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <math.h>


#include "terminal.h"
#include "prodbuffer.h"
#include "memory.h"
#include "util.h"
#include "interpreter.h"
#include "parser.h"
#include "compiler.h"


// types de base
#define NBcore 119 /******** A ACTUALISER! ************/
// mnemonic
const char* corename[]=
    /* 1*/ {NULL,NULL,NULL,NULL,NULL,"hd","tl","Secholn","Secho","Iecholn",					                             //10
            /* 2*/"Iecho","time_ms","tabnew","abs","min","max","rand","srand",					                                  //10+8
            /* 3*/"time","strnew","strset","strcpy","vstrcmp","strfind","strfindrev","strlen",		                        //10+8+8
            /* 4*/"strget","strsub","strcat","tablen","strcatlist","led","motorset","motorget",	                          //10+8+8+8
            /* 5*/"fixarg1","fixarg2","fixarg3","fixarg4","fixarg5","fixarg6","fixarg7","fixarg8",	                      //10+8+8+8+8
            /* 6*/"button2","button3","playStart","playFeed","playStop","recStart","recStop","recVol",	                  //10+8+8+8+8+8
            /* 7*/"load","gc","save","bytecode","loopcb",	                                                                //10+8+8+8+8+8+5
            /* 8*/"udpStart","udpCb","udpStop","udpSend","tcpOpen","tcpClose","tcpSend","tcpCb","tcpListen","tcpEnable",	//10+8+8+8+8+8+5+10
            /* 9*/"envget","envset","sndVol","rfidGet","playTime","sndRefresh","sndWrite","sndRead","sndFeed","sndAmpli",	//10+8+8+8+8+8+5+10+10
            /*10*/"netCb","netSend","netState","netMac","netChk","netSetmode","netScan","netAuth","netPmk","netRssi",	    //10+8+8+8+8+8+5+10+10+10
            /*11*/"netSeqAdd","strgetword","strputword","atoi","htoi","itoa","ctoa","itoh",	                              //10+8+8+8+8+8+5+10+10+10+8
            /*12*/"ctoh","itobin2","listswitch","listswitchstr","corePP","corePush","corePull","coreBit0",                //10+8+8+8+8+8+5+10+10+10+8+8
            /*13*/"reboot","flashFirmware","strcmp","adp2wav","wav2adp","alaw2wav","wav2alaw",	                          //10+8+8+8+8+8+5+10+10+10+8+8+7
            /*14*/"crypt","uncrypt","rfidGetList","rfidRead","rfidWrite",                                                 //10+8+8+8+8+8+5+10+10+10+8+8+7+5
            /*15*/"i2cRead", "i2cWrite",                                                                                  //10+8+8+8+8+8+5+10+10+10+8+8+7+5+2 => 115
            /*16*/"strright","strcrypt8","loadf","savef"
           };

// Opcode
int coreval[]=
    /* 1*/ {0,0,0,0,0,OPhd,OPtl,OPSecholn,OPSecho,OPIecholn,
            /* 2*/OPIecho,OPtime_ms,OPtabnew,OPabs,OPmin,OPmax,OPrand,OPsrand,
            /* 3*/OPtime,OPstrnew,OPstrset,OPstrcpy,OPvstrcmp,OPstrfind,OPstrfindrev,OPstrlen,
            /* 4*/OPstrget,OPstrsub,OPstrcat,OPtablen,OPstrcatlist,OPled,OPmotorset,OPmotorget,
            /* 5*/OPfixarg,OPfixarg,OPfixarg,OPfixarg,OPfixarg,OPfixarg,OPfixarg,OPfixarg,
            /* 6*/OPbutton2,OPbutton3,OPplayStart,OPplayFeed,OPplayStop,OPrecStart,OPrecStop,OPrecVol,
            /* 7*/OPload,OPgc,OPsave,OPbytecode,OPloopcb,
            /* 8*/OPudpStart,OPudpCb,OPudpStop,OPudpSend,OPtcpOpen,OPtcpClose,OPtcpSend,OPtcpCb,OPtcpListen,OPtcpEnable,
            /* 9*/OPenvget,OPenvset,OPsndVol,OPrfidGet,OPplayTime,OPsndRefresh,OPsndWrite,OPsndRead,OPsndFeed,OPsndAmpli,
            /*10*/OPnetCb,OPnetSend,OPnetState,OPnetMac,OPnetChk,OPnetSetmode,OPnetScan,OPnetAuth,OPnetPmk,OPnetRssi,
            /*11*/OPnetSeqAdd,OPstrgetword,OPstrputword,OPatoi,OPhtoi,OPitoa,OPctoa,OPitoh,
            /*12*/OPctoh,OPitobin2,OPlistswitch,OPlistswitchstr,OPcorePP,OPcorePush,OPcorePull,OPcoreBit0,
            /*13*/OPreboot,OPflashFirmware,OPstrcmp,OPadp2wav,OPwav2adp,OPalaw2wav,OPwav2alaw,
            /*14*/OPcrypt,OPuncrypt,OPrfidGetList,OPrfidRead,OPrfidWrite,
            /*15*/OPi2cRead, OPi2cWrite,
            /*16*/OPstrright,OPstrright,OPstrright,OPstrright,
           };
// N args
int corecode[]=
    /* 1*/ {CODE_TYPE,CODE_TYPE,CODE_TYPE,CODE_TYPE,CODE_TYPE,1,1,1,1,1,
            /* 2*/1,0,2,1,2,2,0,1,
            /* 3*/0,1,3,5,5,5,5,1,
            /* 4*/2,3,2,1,1,2,2,1,
            /* 5*/2,2,2,2,2,2,2,2,
            /* 6*/0,0,2,3,0,3,0,2,
            /* 7*/5,0,5,1,1,
            /* 8*/1,1,1,6,2,1,4,1,1,2,
            /* 9*/0,1,1,0,0,0,2,1,3,1,
            /*10*/1,6,0,0,4,3,1,4,2,0,
            /*11*/2,2,3,1,1,1,1,1,
            /*12*/1,1,2,2,0,1,1,2,
            /*13*/2,3,2,5,5,6,6,
            /*14*/5,5,0,2,3,
            /*15*/2,3,
            /*16*/2,3,1,2
           };
// Proto
const char* coretype[]=
    /* 1*/ {"I","S","F","Env","Xml","fun[list u0]u0","fun[list u0]list u0","fun[S]S","fun[S]S","fun[u0]u0",
            /* 2*/"fun[u0]u0","fun[]I","fun[u0 I]tab u0","fun[I]I","fun[I I]I","fun[I I]I","fun[]I","fun[I]I",
            /* 3*/"fun[]I","fun[I]S","fun[S I I]S","fun[S I S I I]S","fun[S I S I I]I","fun[S I S I I]I","fun[S I S I I]I","fun[S]I",
            /* 4*/"fun[S I]I","fun[S I I]S","fun[S S]S","fun[tab u0]I","fun[list S]S","fun[I I]I","fun[I I]I","fun[I]I",
            /* 5*/"fun[fun[u0]u100 u0]fun[]u100",
            "fun[fun[u1 u0]u100 u0]fun[u1]u100",
            "fun[fun[u1 u2 u0]u100 u0]fun[u1 u2]u100",
            "fun[fun[u1 u2 u3 u0]u100 u0]fun[u1 u2 u3]u100",
            "fun[fun[u1 u2 u3 u4 u0]u100 u0]fun[u1 u2 u3 u4]u100",
            "fun[fun[u1 u2 u3 u4 u5 u0]u100 u0]fun[u1 u2 u3 u4 u5]u100",
            "fun[fun[u1 u2 u3 u4 u5 u6 u0]u100 u0]fun[u1 u2 u3 u4 u5 u6]u100",
            "fun[fun[u1 u2 u3 u4 u5 u6 u7 u0]u100 u0]fun[u1 u2 u3 u4 u5 u6 u7]u100",
            /* 6*/"fun[]I","fun[]I","fun[I fun[I]I]I","fun[S I I]I","fun[]I","fun[I I fun[S]I]I","fun[]I","fun[S I]I",
            /* 7*/"fun[S I S I I]I","fun[]I","fun[S I S I I]I","fun[S]S","fun[fun[]I]fun[]I",
            /* 8*/"fun[I]I","fun[fun[I S S]I]fun[I S S]I","fun[I]I","fun[I S I S I I]I","fun[S I]I","fun[I]I","fun[I S I I]I","fun[fun[I I S]I]fun[I I S]I","fun[I]I","fun[I I]I",
            /* 9*/"fun[]S","fun[S]S","fun[I]I","fun[]S","fun[]I","fun[]I","fun[I I]I","fun[I]I","fun[S I I]I","fun[I]I",
            /*10*/"fun[fun[S S]I]fun[S S]I","fun[S I I S I I]I","fun[]I","fun[]S","fun[S I I I]I","fun[I S I]I","fun[S]list[S S S I I I I]","fun[[S S S I I I I] I I S][S S S I I I I]","fun[S S]S","fun[]I",
            /*11*/"fun[S I]S","fun[S I]I","fun[S I I]S","fun[S]I","fun[S]I","fun[I]S","fun[I]S","fun[I]S",
            /*12*/"fun[I]S","fun[I]S","fun[list[u0 u1] u0]u1","fun[list[S u1] S]u1","fun[]I","fun[u0]u0","fun[I]I","fun[u0 I]u0",
            /*13*/"fun[I I]I","fun[S I I]I","fun[S S]I","fun[S I S I I]S","fun[S I S I I]S","fun[S I S I I I]S","fun[S I S I I I]S",
            /*14*/"fun[S I I I I]I","fun[S I I I I]I","fun[]list S","fun[S I]S","fun[S I S]I",
            /*15*/"fun[I I]S", "fun[I S I]I",
            /*16*/"fun[S I]S","fun[S I I][S I]","fun[S]S","fun[S S]I"
           };


intptr_t Compiler::addstdlibcore()
{
    intptr_t k;
    if (k=addnative(NBcore,corename,coreval,corecode,coretype,m))
    {
        return k;
    }
    /*	FILE* f=fopen("bc.txt","w");
    	for(k=0;k<NBcore;k++)
    	{
    		if (corename[k]) fprintf(f,"%s%c%s\n",corename[k],9,coretype[k]);
    	}
    	fclose(f);
    */	return 0;
}
