/*
   The following codes come from ppc-codegen.h:

   Copyright (C)  2001 Radek Doulik

   for testing do the following: ./test | as -o test.o
*/

#ifndef __MONO_PPC_CODEGEN_H__
#define __MONO_PPC_CODEGEN_H__
#include <assert.h>

typedef enum {
	ppc_r0 = 0,
	ppc_r1,
	ppc_sp = ppc_r1,
	ppc_r2,
	ppc_r3,
	ppc_r4,
	ppc_r5,
	ppc_r6,
	ppc_r7,
	ppc_r8,
	ppc_r9,
	ppc_r10,
	ppc_r11,
	ppc_r12,
	ppc_r13,
	ppc_r14,
	ppc_r15,
	ppc_r16,
	ppc_r17,
	ppc_r18,
	ppc_r19,
	ppc_r20,
	ppc_r21,
	ppc_r22,
	ppc_r23,
	ppc_r24,
	ppc_r25,
	ppc_r26,
	ppc_r27,
	ppc_r28,
	ppc_r29,
	ppc_r30,
	ppc_r31
} PPCIntRegister;

typedef enum {
	ppc_f0 = 0,
	ppc_f1,
	ppc_f2,
	ppc_f3,
	ppc_f4,
	ppc_f5,
	ppc_f6,
	ppc_f7,
	ppc_f8,
	ppc_f9,
	ppc_f10,
	ppc_f11,
	ppc_f12,
	ppc_f13,
	ppc_f14,
	ppc_f15,
	ppc_f16,
	ppc_f17,
	ppc_f18,
	ppc_f19,
	ppc_f20,
	ppc_f21,
	ppc_f22,
	ppc_f23,
	ppc_f24,
	ppc_f25,
	ppc_f26,
	ppc_f27,
	ppc_f28,
	ppc_f29,
	ppc_f30,
	ppc_f31
} PPCFloatRegister;

typedef enum {
	ppc_lr = 256,
	ppc_ctr = 256 + 32,
	ppc_xer = 32
} PPCSpecialRegister;

enum {
	/* B0 operand for branches */
	PPC_BR_DEC_CTR_NONZERO_FALSE = 0,
	PPC_BR_LIKELY = 1, /* can be or'ed with the conditional variants */
	PPC_BR_DEC_CTR_ZERO_FALSE = 2,
	PPC_BR_FALSE  = 4,
	PPC_BR_DEC_CTR_NONZERO_TRUE = 8,
	PPC_BR_DEC_CTR_ZERO_TRUE = 10,
	PPC_BR_TRUE   = 12,
	PPC_BR_DEC_CTR_NONZERO = 16,
	PPC_BR_DEC_CTR_ZERO = 18,
	PPC_BR_ALWAYS = 20,
	/* B1 operand for branches */
	PPC_BR_LT     = 0,
	PPC_BR_GT     = 1,
	PPC_BR_EQ     = 2,
	PPC_BR_SO     = 3,
};

enum {
	PPC_TRAP_LT = 1,
	PPC_TRAP_GT = 2,
	PPC_TRAP_EQ = 4,
	PPC_TRAP_LT_UN = 8,
	PPC_TRAP_GT_UN = 16,
	PPC_TRAP_LE = 1 + PPC_TRAP_EQ,
	PPC_TRAP_GE = 2 + PPC_TRAP_EQ,
	PPC_TRAP_LE_UN = 8 + PPC_TRAP_EQ,
	PPC_TRAP_GE_UN = 16 + PPC_TRAP_EQ
};

#define ppc_emit32(c,x) do { *((unsigned int *) (c)) = x; (c) = (unsigned char *)(c) + sizeof (unsigned int);} while (0)

#define ppc_is_imm16(val) ((int)(val) >= (int)-(1<<15) && (int)(val) <= (int)((1<<15)-1))
#define ppc_is_uimm16(val) ((int)(val) >= 0 && (int)(val) <= 65535)

#define ppc_load(c,D,v) do {	\
		if (ppc_is_imm16 ((v)))	{	\
			ppc_li ((c), (D), (unsigned short)(v));	\
		} else {	\
			ppc_lis ((c), (D), (unsigned int)(v) >> 16);	\
			ppc_ori ((c), (D), (D), (unsigned int)(v) & 0xffff);	\
		}	\
	} while (0)

#define ppc_break(c) ppc_tw((c),31,0,0)
#define  ppc_addi(c,D,A,d) ppc_emit32 (c, (14 << 26) | ((D) << 21) | ((A) << 16) | (unsigned short)(d))
#define ppc_addis(c,D,A,d) ppc_emit32 (c, (15 << 26) | ((D) << 21) | ((A) << 16) | (unsigned short)(d))
#define    ppc_li(c,D,v)   ppc_addi   (c, D, 0, (unsigned short)(v));
#define   ppc_lis(c,D,v)   ppc_addis  (c, D, 0, (unsigned short)(v));
#define   ppc_lwz(c,D,d,a) ppc_emit32 (c, (32 << 26) | ((D) << 21) | ((a) << 16) | (unsigned short)(d))
#define   ppc_lhz(c,D,d,a) ppc_emit32 (c, (40 << 26) | ((D) << 21) | ((a) << 16) | (unsigned short)(d))
#define   ppc_lbz(c,D,d,a) ppc_emit32 (c, (34 << 26) | ((D) << 21) | ((a) << 16) | (unsigned short)(d))
#define   ppc_stw(c,S,d,a) ppc_emit32 (c, (36 << 26) | ((S) << 21) | ((a) << 16) | (unsigned short)(d))
#define   ppc_sth(c,S,d,a) ppc_emit32 (c, (44 << 26) | ((S) << 21) | ((a) << 16) | (unsigned short)(d))
#define   ppc_stb(c,S,d,a) ppc_emit32 (c, (38 << 26) | ((S) << 21) | ((a) << 16) | (unsigned short)(d))
#define  ppc_stwu(c,s,d,a) ppc_emit32 (c, (37 << 26) | ((s) << 21) | ((a) << 16) | (unsigned short)(d))
#define    ppc_or(c,a,s,b) ppc_emit32 (c, (31 << 26) | ((s) << 21) | ((a) << 16) | ((b) << 11) | 888)
/* ori updated as pem.fm & MPCFPE32B - Zhe */
#define   ppc_ori(c,A,S,UIMM) ppc_emit32 (c, (24 << 26) | ((S) << 21) | ((A) << 16) | (unsigned short)(UIMM))
#define    ppc_mr(c,a,s)   ppc_or     (c, a, s, s)
#define ppc_mfspr(c,D,spr) ppc_emit32 (c, (31 << 26) | ((D) << 21) | ((spr) << 11) | (339 << 1))
#define  ppc_mflr(c,D)     ppc_mfspr  (c, D, ppc_lr)
#define ppc_mtspr(c,spr,S) ppc_emit32 (c, (31 << 26) | ((S) << 21) | ((spr) << 11) | (467 << 1))
#define  ppc_mtlr(c,S)     ppc_mtspr  (c, ppc_lr, S)
#define  ppc_mtctr(c,S)     ppc_mtspr  (c, ppc_ctr, S)
#define  ppc_mtxer(c,S)     ppc_mtspr  (c, ppc_xer, S)

#define  ppc_b(c,li)       ppc_emit32 (c, (18 << 26) | ((li) << 2))
#define  ppc_bl(c,li)       ppc_emit32 (c, (18 << 26) | ((li) << 2) | 1)
#define  ppc_ba(c,li)       ppc_emit32 (c, (18 << 26) | ((li) << 2) | 2)
#define  ppc_bla(c,li)       ppc_emit32 (c, (18 << 26) | ((li) << 2) | 3)
#define  ppc_blrl(c)       ppc_emit32 (c, 0x4e800021)
#define   ppc_blr(c)       ppc_emit32 (c, 0x4e800020)

#define   ppc_lfs(c,D,d,A) ppc_emit32 (c, (48 << 26) | ((D) << 21) | ((A) << 16) | (unsigned short)(d))
#define   ppc_lfd(c,D,d,A) ppc_emit32 (c, (50 << 26) | ((D) << 21) | ((A) << 16) | (unsigned short)(d))
#define  ppc_stfs(c,S,d,a) ppc_emit32 (c, (52 << 26) | ((S) << 21) | ((a) << 16) | (unsigned short)(d))
#define  ppc_stfd(c,S,d,a) ppc_emit32 (c, (54 << 26) | ((S) << 21) | ((a) << 16) | (unsigned short)(d))

/***********************************************************************
The macros below were tapped out by Christopher Taylor <ct_AT_clemson_DOT_edu>
from 18 November 2002 to 19 December 2002.

Special thanks to rodo, lupus, dietmar, miguel, and duncan for patience,
and motivation.

The macros found in this file are based on the assembler instructions found 
in Motorola and Digital DNA's:

"Programming Enviornments Manual For 32-bit Implementations of the PowerPC Architecture"

MPCFPE32B/AD
12/2001
REV2

see pages 326 - 524 for detailed information regarding each instruction

Also see the "Ximian Copyright Agreement, 2002" for more information regarding
my and Ximian's copyright to this code. ;)
*************************************************************************/

#define ppc_addx(c,D,A,B,OE,Rc) ppc_emit32(c, (31 << 26) | ((D) << 21) | ((A) << 16) | ((B) << 11) | (OE << 10) | (266 << 1) | Rc)
#define ppc_add(c,D,A,B) ppc_addx(c,D,A,B,0,0)
#define ppc_addd(c,D,A,B) ppc_addx(c,D,A,B,0,1)
#define ppc_addo(c,D,A,B) ppc_addx(c,D,A,B,1,0)
#define ppc_addod(c,D,A,B) ppc_addx(c,D,A,B,1,1)

#define ppc_addcx(c,D,A,B,OE,Rc) ppc_emit32(c, (31 << 26) | ((D) << 21) | ((A) << 16) | ((B) << 11) | (OE << 10) | (10 << 1) | Rc)
#define ppc_addc(c,D,A,B) ppc_addcx(c,D,A,B,0,0)
#define ppc_addcd(c,D,A,B) ppc_addcx(c,D,A,B,0,1)
#define ppc_addco(c,D,A,B) ppc_addcx(c,D,A,B,1,0)
#define ppc_addcod(c,D,A,B) ppc_addcx(c,D,A,B,1,1)

#define ppc_addex(c,D,A,B,OE,Rc) ppc_emit32(c, (31 << 26) | ((D) << 21) | ((A) << 16) | ((B) << 11) | (OE << 10) | (138 << 1) | Rc)
#define ppc_adde(c,D,A,B) ppc_addex(c,D,A,B,0,0)
#define ppc_added(c,D,A,B) ppc_addex(c,D,A,B,0,1)
#define ppc_addeo(c,D,A,B) ppc_addex(c,D,A,B,1,0)
#define ppc_addeod(c,D,A,B) ppc_addex(c,D,A,B,1,1)

#define ppc_addic(c,D,A,d) ppc_emit32(c, (12 << 26) | ((D) << 21) | ((A) << 16) | (unsigned short)(d)) 
#define ppc_addicd(c,D,A,d) ppc_emit32(c, (13 << 26) | ((D) << 21) | ((A) << 16) | (unsigned short)(d)) 

#define ppc_addmex(c,D,A,OE,RC) ppc_emit32(c, (31 << 26) | ((D) << 21 ) | ((A) << 16) | (0 << 11) | ((OE) << 10) | (234 << 1) | RC)
#define ppc_addme(c,D,A) ppc_addmex(c,D,A,0,0)
#define ppc_addmed(c,D,A) ppc_addmex(c,D,A,0,1)
#define ppc_addmeo(c,D,A) ppc_addmex(c,D,A,1,0)
#define ppc_addmeod(c,D,A) ppc_addmex(c,D,A,1,1)

#define ppc_addzex(c,D,A,OE,RC) ppc_emit32(c, (31 << 26) | ((D) << 21 ) | ((A) << 16) | (0 << 11) | ((OE) << 10) | (202 << 1) | RC)
#define ppc_addze(c,D,A) ppc_addzex(c,D,A,0,0)
#define ppc_addzed(c,D,A) ppc_addzex(c,D,A,0,1)
#define ppc_addzeo(c,D,A) ppc_addzex(c,D,A,1,0)
#define ppc_addzeod(c,D,A) ppc_addzex(c,D,A,1,1)

/* and* updated as pem.fm & MPCFPE32B - Zhe */
#define ppc_andx(c,A,S,B,RC) ppc_emit32(c, (31 << 26) | ((S) << 21 ) | ((A) << 16) | ((B) << 11) | (28 << 1) | RC)
#define ppc_and(c,A,S,B) ppc_andx(c,A,S,B,0)
#define ppc_andd(c,A,S,B) ppc_andx(c,A,S,B,1)

#define ppc_andcx(c,A,S,B,RC) ppc_emit32(c, (31 << 26) | ((S) << 21 ) | ((A) << 16) | ((B) << 11) | (60 << 1) | RC)
#define ppc_andc(c,A,S,B) ppc_andcx(c,A,S,B,0)
#define ppc_andcd(c,A,S,B) ppc_andcx(c,A,S,B,1)

#define ppc_andid(c,A,S,d) ppc_emit32(c, (28 << 26) | ((S) << 21 ) | ((A) << 16) | ((unsigned short)(d)))
#define ppc_andisd(c,A,S,d) ppc_emit32(c, (29 << 26) | ((S) << 21 ) | ((A) << 16) | ((unsigned short)(d)))

#define ppc_bcx(c,BO,BI,BD,AA,LK) ppc_emit32(c, (16 << 26) | (BO << 21 )| (BI << 16) | ((BD) << 2) | ((AA) << 1) | LK)
#define ppc_bc(c,BO,BI,BD) ppc_bcx(c,BO,BI,BD,0,0) 
#define ppc_bca(c,BO,BI,BD) ppc_bcx(c,BO,BI,BD,1,0)
#define ppc_bcl(c,BO,BI,BD) ppc_bcx(c,BO,BI,BD,0,1)
#define ppc_bcla(c,BO,BI,BD) ppc_bcx(c,BO,BI,BD,1,1)

#define ppc_bcctrx(c,BO,BI,LK) ppc_emit32(c, (19 << 26) | (BO << 21 )| (BI << 16) | (0 << 11) | (528 << 1) | LK)
#define ppc_bcctr(c,BO,BI) ppc_bcctrx(c,BO,BI,0)
#define ppc_bcctrl(c,BO,BI) ppc_bcctrx(c,BO,BI,1)

#define ppc_bnectrp(c,BO,BI) ppc_bcctr(c,BO,BI)
#define ppc_bnectrlp(c,BO,BI) ppc_bcctr(c,BO,BI)

#define ppc_bclrx(c,BO,BI,LK) ppc_emit32(c, (19 << 26) | (BO << 21 )| (BI << 16) | (0 << 11) | (16 << 1) | LK)
#define ppc_bclr(c,BO,BI) ppc_bclrx(c,BO,BI,0)
#define ppc_bclrl(c,BO,BI) ppc_bclrx(c,BO,BI,1)

#define ppc_bnelrp(c,BO,BI) ppc_bclr(c,BO,BI)
#define ppc_bnelrlp(c,BO,BI) ppc_bclr(c,BO,BI)

#define ppc_cmp(c,cfrD,L,A,B) ppc_emit32(c, (31 << 26) | (cfrD << 23) | (0 << 22) | (L << 21) | (A << 16) | (B << 11) | (0x00000 << 1) | 0 )
#define ppc_cmpi(c,cfrD,L,A,B) ppc_emit32(c, (11 << 26) | (cfrD << 23) | (0 << 22) | (L << 21) | (A << 16) | (unsigned short)(B))
#define ppc_cmpl(c,cfrD,L,A,B) ppc_emit32(c, (31 << 26) | (cfrD << 23) | (0 << 22) | (L << 21) | (A << 16) | (B << 11) | (32 << 1) | 0 )
#define ppc_cmpli(c,cfrD,L,A,B) ppc_emit32(c, (10 << 26) | (cfrD << 23) | (0 << 22) | (L << 21) | (A << 16) | (unsigned short)(B))

/* cntlzw* updated as pem.fm & MPCFPE32B - Zhe */
#define ppc_cntlzwx(c,A,S,Rc) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (0 << 11) | (26 << 1) | Rc)
#define ppc_cntlzw(c,A,S) ppc_cntlzwx(c,A,S,0)
#define ppc_cntlzwd(c,A,S) ppc_cntlzwx(c,A,S,1)

#define ppc_crand(c,D,A,B) ppc_emit32(c, (19 << 26) | (D << 21) | (A << 16) | (B << 11) | (257 << 1) | 0)
#define ppc_crandc(c,D,A,B) ppc_emit32(c, (19 << 26) | (D << 21) | (A << 16) | (B << 11) | (129 << 1) | 0)
#define ppc_creqv(c,D,A,B) ppc_emit32(c, (19 << 26) | (D << 21) | (A << 16) | (B << 11) | (289 << 1) | 0)
#define ppc_crnand(c,D,A,B) ppc_emit32(c, (19 << 26) | (D << 21) | (A << 16) | (B << 11) | (225 << 1) | 0)
#define ppc_crnor(c,D,A,B) ppc_emit32(c, (19 << 26) | (D << 21) | (A << 16) | (B << 11) | (33 << 1) | 0)
#define ppc_cror(c,D,A,B) ppc_emit32(c, (19 << 26) | (D << 21) | (A << 16) | (B << 11) | (449 << 1) | 0)
#define ppc_crorc(c,D,A,B) ppc_emit32(c, (19 << 26) | (D << 21) | (A << 16) | (B << 11) | (417 << 1) | 0)
#define ppc_crxor(c,D,A,B) ppc_emit32(c, (19 << 26) | (D << 21) | (A << 16) | (B << 11) | (193 << 1) | 0)

#define ppc_dcba(c,A,B) ppc_emit32(c, (31 << 26) | (0 << 21) | (A << 16) | (B << 11) | (758 << 1) | 0)
#define ppc_dcbf(c,A,B) ppc_emit32(c, (31 << 26) | (0 << 21) | (A << 16) | (B << 11) | (86 << 1) | 0)
#define ppc_dcbi(c,A,B) ppc_emit32(c, (31 << 26) | (0 << 21) | (A << 16) | (B << 11) | (470 << 1) | 0)
#define ppc_dcbst(c,A,B) ppc_emit32(c, (31 << 26) | (0 << 21) | (A << 16) | (B << 11) | (54 << 1) | 0)
/* dcbt updated as pem.fm.2.3 - Zhe */
#define ppc_dcbt(c,A,B,TH) ppc_emit32(c, (31 << 26) | (TH << 21) | (A << 16) | (B << 11) | (278 << 1) | 0)
#define ppc_dcbtst(c,A,B) ppc_emit32(c, (31 << 26) | (0 << 21) | (A << 16) | (B << 11) | (246 << 1) | 0)
#define ppc_dcbz(c,A,B) ppc_emit32(c, (31 << 26) | (0 << 21) | (A << 16) | (B << 11) | (1014 << 1) | 0)

#define ppc_divwx(c,D,A,B,OE,Rc) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (OE << 10) | (491 << 1) | Rc)
#define ppc_divw(c,D,A,B) ppc_divwx(c,D,A,B,0,0)
#define ppc_divwd(c,D,A,B) ppc_divwx(c,D,A,B,0,1)
#define ppc_divwo(c,D,A,B) ppc_divwx(c,D,A,B,1,0)
#define ppc_divwod(c,D,A,B) ppc_divwx(c,D,A,B,1,1)

#define ppc_divwux(c,D,A,B,OE,Rc) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (OE << 10) | (459 << 1) | Rc)
#define ppc_divwu(c,D,A,B) ppc_divwux(c,D,A,B,0,0)
#define ppc_divwud(c,D,A,B) ppc_divwux(c,D,A,B,0,1)
#define ppc_divwuo(c,D,A,B) ppc_divwux(c,D,A,B,1,0)
#define ppc_divwuod(c,D,A,B) ppc_divwux(c,D,A,B,1,1)

#define ppc_eciwx(c,D,A,B) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (310 << 1) | 0)
#define ppc_ecowx(c,S,A,B) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (438 << 1) | 0)
#define ppc_eieio(c) ppc_emit32(c, (31 << 26) | (0 << 21) | (0 << 16) | (0 << 11) | (854 << 1) | 0)

#define ppc_eqvx(c,A,S,B,Rc) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (284 << 1) | Rc)
#define ppc_eqv(c,A,S,B) ppc_eqvx(c,A,S,B,0)
#define ppc_eqvd(c,A,S,B) ppc_eqvx(c,A,S,B,1)

#define ppc_extsbx(c,A,S,Rc) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (0 << 11) | (954 << 1) | Rc) 
#define ppc_extsb(c,A,S) ppc_extsbx(c,A,S,0)
#define ppc_extsbd(c,A,S) ppc_extsbx(c,A,S,1)

#define ppc_extshx(c,A,S,Rc) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (0 << 11) | (922 << 1) | Rc) 
#define ppc_extsh(c,A,S) ppc_extshx(c,A,S,0)
#define ppc_extshd(c,A,S) ppc_extshx(c,A,S,1)

#define ppc_fabsx(c,D,B,Rc) ppc_emit32(c, (63 << 26) | (D << 21) | (0 << 16) | (B << 11) | (264 << 1) | Rc) 
#define ppc_fabs(c,D,B) ppc_fabsx(c,D,B,0)
#define ppc_fabsd(c,D,B) ppc_fabsx(c,D,B,1)

#define ppc_faddx(c,D,A,B,Rc) ppc_emit32(c, (63 << 26) | (D << 21) | (A << 16) | (B << 11) | (0 << 6) | (21 << 1) | Rc)
#define ppc_fadd(c,D,A,B) ppc_faddx(c,D,A,B,0)
#define ppc_faddd(c,D,A,B) ppc_faddx(c,D,A,B,1)

#define ppc_faddsx(c,D,A,B,Rc) ppc_emit32(c, (59 << 26) | (D << 21) | (A << 16) | (B << 11) | (0 << 6) | (21 << 1) | Rc)
#define ppc_fadds(c,D,A,B) ppc_faddsx(c,D,A,B,0)
#define ppc_faddsd(c,D,A,B) ppc_faddsx(c,D,A,B,1)

#define ppc_fcmpo(c,crfD,A,B) ppc_emit32(c, (63 << 26) | (crfD << 23) | (0 << 21) | (A << 16) | (B << 11) | (32 << 1) | 0)
#define ppc_fcmpu(c,crfD,A,B) ppc_emit32(c, (63 << 26) | (crfD << 23) | (0 << 21) | (A << 16) | (B << 11) | (0 << 1) | 0)

#define ppc_fctiwx(c,D,B,Rc) ppc_emit32(c, (63 << 26) | (D << 21) | (0 << 16) | (B << 11) | (14 << 1) | Rc)
#define ppc_fctiw(c,D,B) ppc_fctiwx(c,D,B,0)
#define ppc_fctiwd(c,D,B) ppc_fctiwx(c,D,B,1)

#define ppc_fctiwzx(c,D,B,Rc) ppc_emit32(c, (63 << 26) | (D << 21) | (0 << 16) | (B << 11) | (15 << 1) | Rc)
#define ppc_fctiwz(c,D,B) ppc_fctiwzx(c,D,B,0)
#define ppc_fctiwzd(c,D,B) ppc_fctiwzx(c,D,B,1)

#define ppc_fdivx(c,D,A,B,Rc) ppc_emit32(c, (63 << 26) | (D << 21) | (A << 16) | (B << 11) | (0 << 6) | (18 << 1) | Rc)
#define ppc_fdiv(c,D,A,B) ppc_fdivx(c,D,A,B,0)
#define ppc_fdivd(c,D,A,B) ppc_fdivx(c,D,A,B,1)

#define ppc_fdivsx(c,D,A,B,Rc) ppc_emit32(c, (59 << 26) | (D << 21) | (A << 16) | (B << 11) | (0 << 6) | (18 << 1) | Rc)
#define ppc_fdivs(c,D,A,B) ppc_fdivsx(c,D,A,B,0)
#define ppc_fdivsd(c,D,A,B) ppc_fdivsx(c,D,A,B,1)

#define ppc_fmaddx(c,D,A,B,C,Rc) ppc_emit32(c, (63 << 26) | (D << 21) | (A << 16) | (B << 11) | (C << 6) | (29 << 1) | Rc)
#define ppc_fmadd(c,D,A,B,C) ppc_fmaddx(c,D,A,B,C,0)
#define ppc_fmaddd(c,D,A,B,C) ppc_fmaddx(c,D,A,B,C,1) 

#define ppc_fmaddsx(c,D,A,B,C,Rc) ppc_emit32(c, (59 << 26) | (D << 21) | (A << 16) | (B << 11) | (C << 6) | (29 << 1) | Rc)
#define ppc_fmadds(c,D,A,B,C) ppc_fmaddsx(c,D,A,B,C,0)
#define ppc_fmaddsd(c,D,A,B,C) ppc_fmaddsx(c,D,A,B,C,1) 

#define ppc_fmrx(c,D,B,Rc) ppc_emit32(c, (63 << 26) | (D << 21) | (0 << 16) | (B << 11) | (72 << 1) | Rc)
#define ppc_fmr(c,D,B) ppc_fmrx(c,D,B,0)
#define ppc_fmrd(c,D,B) ppc_fmrx(c,D,B,1)

#define ppc_fmsubx(c,D,A,C,B,Rc) ppc_emit32(c, (63 << 26) | (D << 21) | (A << 16) | (B << 11) | (C << 6) | (28 << 1) | Rc)
#define ppc_fmsub(c,D,A,C,B) ppc_fmsubx(c,D,A,C,B,0)
#define ppc_fmsubd(c,D,A,C,B) ppc_fmsubx(c,D,A,C,B,1)

#define ppc_fmsubsx(c,D,A,C,B,Rc) ppc_emit32(c, (59 << 26) | (D << 21) | (A << 16) | (B << 11) | (C << 6) | (28 << 1) | Rc)
#define ppc_fmsubs(c,D,A,C,B) ppc_fmsubsx(c,D,A,C,B,0)
#define ppc_fmsubsd(c,D,A,C,B) ppc_fmsubsx(c,D,A,C,B,1)

#define ppc_fmulx(c,D,A,C,Rc) ppc_emit32(c, (63 << 26) | (D << 21) | (A << 16) | (0 << 11) | (C << 6) | (25 << 1) | Rc) 
#define ppc_fmul(c,D,A,C) ppc_fmulx(c,D,A,C,0)
#define ppc_fmuld(c,D,A,C) ppc_fmulx(c,D,A,C,1)

#define ppc_fmulsx(c,D,A,C,Rc) ppc_emit32(c, (59 << 26) | (D << 21) | (A << 16) | (0 << 11) | (C << 6) | (25 << 1) | Rc) 
#define ppc_fmuls(c,D,A,C) ppc_fmulsx(c,D,A,C,0)
#define ppc_fmulsd(c,D,A,C) ppc_fmulsx(c,D,A,C,1)

#define ppc_fnabsx(c,D,B,Rc) ppc_emit32(c, (63 << 26) | (D << 21) | (0 << 16) | (B << 11) | (136 << 1) | Rc)
#define ppc_fnabs(c,D,B) ppc_fnabsx(c,D,B,0)
#define ppc_fnabsd(c,D,B) ppc_fnabsx(c,D,B,1)

#define ppc_fnegx(c,D,B,Rc) ppc_emit32(c, (63 << 26) | (D << 21) | (0 << 16) | (B << 11) | (40 << 1) | Rc)
#define ppc_fneg(c,D,B) ppc_fnegx(c,D,B,0)
#define ppc_fnegd(c,D,B) ppc_fnegx(c,D,B,1)

#define ppc_fnmaddx(c,D,A,C,B,Rc) ppc_emit32(c, (63 << 26) | (D << 21) | (A << 16) | (B << 11) | (C << 6) | (31 << 1) | Rc)
#define ppc_fnmadd(c,D,A,C,B) ppc_fnmaddx(c,D,A,C,B,0)
#define ppc_fnmaddd(c,D,A,C,B) ppc_fnmaddx(c,D,A,C,B,1)

#define ppc_fnmaddsx(c,D,A,C,B,Rc) ppc_emit32(c, (59 << 26) | (D << 21) | (A << 16) | (B << 11) | (C << 6) | (31 << 1) | Rc)
#define ppc_fnmadds(c,D,A,C,B) ppc_fnmaddsx(c,D,A,C,B,0)
#define ppc_fnmaddsd(c,D,A,C,B) ppc_fnmaddsx(c,D,A,C,B,1)

#define ppc_fnmsubx(c,D,A,C,B,Rc) ppc_emit32(c, (63 << 26) | (D << 21) | (A << 16) | (B << 11) | (C << 6) | (30 << 1) | Rc)
#define ppc_fnmsub(c,D,A,C,B) ppc_fnmsubx(c,D,A,C,B,0)
#define ppc_fnmsubd(c,D,A,C,B) ppc_fnmsubx(c,D,A,C,B,1)

#define ppc_fnmsubsx(c,D,A,C,B,Rc) ppc_emit32(c, (59 << 26) | (D << 21) | (A << 16) | (B << 11) | (C << 6) | (30 << 1) | Rc)
#define ppc_fnmsubs(c,D,A,C,B) ppc_fnmsubsx(c,D,A,C,B,0)
#define ppc_fnmsubsd(c,D,A,C,B) ppc_fnmsubsx(c,D,A,C,B,1)

#define ppc_fresx(c,D,B,Rc) ppc_emit32(c, (59 << 26) | (D << 21) | (0 << 16) | (B << 11) | (0 << 6) | (24 << 1) | Rc)
#define ppc_fres(c,D,B) ppc_fresx(c,D,B,0)
#define ppc_fresd(c,D,B) ppc_fresx(c,D,B,1)

#define ppc_frspx(c,D,B,Rc) ppc_emit32(c, (63 << 26) | (D << 21) | (0 << 16) | (B << 11) | (12 << 1) | Rc)
#define ppc_frsp(c,D,B) ppc_frspx(c,D,B,0)
#define ppc_frspd(c,D,B) ppc_frspx(c,D,B,1)

#define ppc_frsqrtex(c,D,B,Rc) ppc_emit32(c, (63 << 26) | (D << 21) | (0 << 16) | (B << 11) | (0 << 6) | (26 << 1) | Rc)
#define ppc_frsqrte(c,D,B) ppc_frsqrtex(c,D,B,0)
#define ppc_frsqrted(c,D,B) ppc_frsqrtex(c,D,B,1)

#define ppc_fselx(c,D,A,C,B,Rc) ppc_emit32(c, (63 << 26) | (D << 21) | (A << 16) | (B << 11) | (C << 6) | (23 << 1) | Rc)
#define ppc_fsel(c,D,A,C,B) ppc_fselx(c,D,A,C,B,0)
#define ppc_fseld(c,D,A,C,B) ppc_fselx(c,D,A,C,B,1)

#define ppc_fsqrtx(c,D,B,Rc) ppc_emit32(c, (63 << 26) | (D << 21) | (0 << 16) | (B << 11) | (0 << 6) | (22 << 1) | Rc)
#define ppc_fsqrt(c,D,B) ppc_fsqrtx(c,D,B,0)
#define ppc_fsqrtd(c,D,B) ppc_fsqrtx(c,D,B,1)

#define ppc_fsqrtsx(c,D,B,Rc) ppc_emit32(c, (59 << 26) | (D << 21) | (0 << 16) | (B << 11) | (0 << 6) | (22 << 1) | Rc)
#define ppc_fsqrts(c,D,B) ppc_fsqrtsx(c,D,B,0)
#define ppc_fsqrtsd(c,D,B) ppc_fsqrtsx(c,D,B,1)

#define ppc_fsubx(c,D,A,B,Rc) ppc_emit32(c, (63 << 26) | (D << 21) | (A << 16) | (B << 11) | (0 << 6) | (20 << 1) | Rc)
#define ppc_fsub(c,D,A,B) ppc_fsubx(c,D,A,B,0)
#define ppc_fsubd(c,D,A,B) ppc_fsubx(c,D,A,B,1)

#define ppc_fsubsx(c,D,A,B,Rc) ppc_emit32(c, (59 << 26) | (D << 21) | (A << 16) | (B << 11) | (0 << 6) | (20 << 1) | Rc)
#define ppc_fsubs(c,D,A,B) ppc_fsubsx(c,D,A,B,0)
#define ppc_fsubsd(c,D,A,B) ppc_fsubsx(c,D,A,B,1)

#define ppc_icbi(c,A,B) ppc_emit32(c, (31 << 26) | (0 << 21) | (A << 16) | (B << 11) | (982 << 1) | 0)

#define ppc_isync(c) ppc_emit32(c, (19 << 26) | (0 << 11) | (150 << 1) | 0)

/* lbzu updated as pem.fm & MPCFPE32B - Zhe */
#define ppc_lbzu(c,D,d,A) ppc_emit32(c, (35 << 26) | (D << 21) | (A << 16) | (unsigned short)d)
#define ppc_lbzux(c,D,A,B) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (119 << 1) | 0)
#define ppc_lbzx(c,D,A,B) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (87 << 1) | 0)

/* lfdu updated as pem.fm & MPCFPE32B - Zhe */
#define ppc_lfdu(c,D,d,A) ppc_emit32(c, (51 << 26) | (D << 21) | (A << 16) | (unsigned short)d)
#define ppc_lfdux(c,D,A,B) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (631 << 1) | 0)
#define ppc_lfdx(c,D,A,B) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (599 << 1) | 0)

/* lfs* updated as pem.fm & MPCFPE32B - Zhe */
#define ppc_lfsu(c,D,d,A) ppc_emit32(c, (49 << 26) | (D << 21) | (A << 16) | (unsigned short)d)
#define ppc_lfsux(c,D,A,B) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (567 << 1) | 0)
#define ppc_lfsx(c,D,A,B) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (535 << 1) | 0)

/* lha, lhau, lhzu updated as pem.fm & MPCFPE32B - Zhe */
#define ppc_lha(c,D,d,A) ppc_emit32(c, (42 << 26) | (D << 21) | (A << 16) | (unsigned short)d)
#define ppc_lhau(c,D,d,A) ppc_emit32(c, (43 << 26) | (D << 21) | (A << 16) | (unsigned short)d)
#define ppc_lhaux(c,D,A,B) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (375 << 1) | 0)
#define ppc_lhax(c,D,A,B) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (343 << 1) | 0)
#define ppc_lhbrx(c,D,A,B) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (790 << 1) | 0)
#define ppc_lhzu(c,D,d,A) ppc_emit32(c, (41 << 26) | (D << 21) | (A << 16) | (unsigned short)d)

#define ppc_lhzux(c,D,A,B) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (311 << 1) | 0)
#define ppc_lhzx(c,D,A,B) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (279 << 1) | 0)

/* lmw updated as pem.fm & MPCFPE32B - Zhe */
#define ppc_lmw(c,D,d,A) ppc_emit32(c, (46 << 26) | (D << 21) | (A << 16) | (unsigned short)d)

#define ppc_lswi(c,D,A,NB) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (NB << 11) | (597 << 1) | 0)
#define ppc_lswx(c,D,A,B) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (533 << 1) | 0)
#define ppc_lwarx(c,D,A,B) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (20 << 1) | 0)
#define ppc_lwbrx(c,D,A,B) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (534 << 1) | 0)

/* lwzu updated as pem.fm & MPCFPE32B - Zhe */
#define ppc_lwzu(c,D,d,A) ppc_emit32(c, (33 << 26) | (D << 21) | (A << 16) | (unsigned short)d)
#define ppc_lwzux(c,D,A,B) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (55 << 1) | 0)
#define ppc_lwzx(c,D,A,B) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (23 << 1) | 0)

#define ppc_mcrf(c,crfD,crfS) ppc_emit32(c, (19 << 26) | (crfD << 23) | (0 << 21) | (crfS << 18) | 0)
#define ppc_mcrfs(c,crfD,crfS) ppc_emit32(c, (63 << 26) | (crfD << 23) | (0 << 21) | (crfS << 18) | (0 << 16) | (64 << 1) | 0)
#define ppc_mcrxr(c,crfD) ppc_emit32(c, (31 << 26) | (crfD << 23) | (0 << 16) | (512 << 1) | 0)

#define ppc_mfcr(c,D) ppc_emit32(c, (31 << 26) | (D << 21) | (0 << 16) | (19 << 1) | 0)
#define ppc_mffsx(c,D,Rc) ppc_emit32(c, (63 << 26) | (D << 21) | (0 << 16) | (583 << 1) | Rc)
#define ppc_mffs(c,D) ppc_mffsx(c,D,0)
#define ppc_mffsd(c,D) ppc_mffsx(c,D,1)
#define ppc_mfmsr(c,D) ppc_emit32(c, (31 << 26) | (D << 21) | (0 << 16) | (83 << 1) | 0)
#define ppc_mfsr(c,D,SR) ppc_emit32(c, (31 << 26) | (D << 21) | (0 << 20) | (SR << 16) | (0 << 11) | (595 << 1) | 0)
#define ppc_mfsrin(c,D,B) ppc_emit32(c, (31 << 26) | (D << 21) | (0 << 16) | (B << 11) | (659 << 1) | 0)
#define ppc_mftb(c,D,TBR) ppc_emit32(c, (31 << 26) | (D << 21) | (TBR << 11) | (371 << 1) | 0)

#define ppc_mtcrf(c,CRM,S) ppc_emit32(c, (31 << 26) | (S << 21) | (0 << 20) | (CRM << 12) | (0 << 11) | (144 << 1) | 0)

#define ppc_mtfsb0x(c,CRB,Rc) ppc_emit32(c, (63 << 26) | (CRB << 21) | (0 << 11) | (70 << 1) | Rc)
#define ppc_mtfsb0(c,CRB) ppc_mtfsb0x(c,CRB,0)
#define ppc_mtfsb0d(c,CRB) ppc_mtfsb0x(c,CRB,1)

#define ppc_mtfsb1x(c,CRB,Rc) ppc_emit32(c, (63 << 26) | (CRB << 21) | (0 << 11) | (38 << 1) | Rc)
#define ppc_mtfsb1(c,CRB) ppc_mtfsb1x(c,CRB,0)
#define ppc_mtfsb1d(c,CRB) ppc_mtfsb1x(c,CRB,1)

#define ppc_mtfsfx(c,FM,B,Rc) ppc_emit32(c, (63 << 26) | (0 << 25) | (FM << 22) | (0 << 21) | (B << 11) | (711 << 1) | Rc)
#define ppc_mtfsf(c,FM,B) ppc_mtfsfx(c,FM,B,0)
#define ppc_mtfsfd(c,FM,B) ppc_mtfsfx(c,FM,B,1)

#define ppc_mtfsfix(c,crfD,IMM,Rc) ppc_emit32(c, (63 << 26) | (crfD << 23) | (0 << 16) | (IMM << 12) | (0 << 11) | (134 << 1) | Rc)
#define ppc_mtfsfi(c,crfD,IMM) ppc_mtfsfix(c,crfD,IMM,0)
#define ppc_mtfsfid(c,crfD,IMM) ppc_mtfsfix(c,crfD,IMM,1)

#define ppc_mtmsr(c, S) ppc_emit32(c, (31 << 26) | (S << 21) | (0 << 11) | (146 << 1) | 0)

#define ppc_mtsr(c,SR,S) ppc_emit32(c, (31 << 26) | (S << 21) | (0 << 20) | (SR << 16) | (0 << 11) | (210 << 1) | 0)
#define ppc_mtsrin(c,S,B) ppc_emit32(c, (31 << 26) | (S << 21) | (0 << 16) | (B << 11) | (242 << 1) | 0)

#define ppc_mulhwx(c,D,A,B,Rc) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (0 << 10) | (75 << 1) | Rc)
#define ppc_mulhw(c,D,A,B) ppc_mulhwx(c,D,A,B,0)
#define ppc_mulhwd(c,D,A,B) ppc_mulhwx(c,D,A,B,1)

#define ppc_mulhwux(c,D,A,B,Rc) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (0 << 10) | (11 << 1) | Rc)
#define ppc_mulhwu(c,D,A,B) ppc_mulhwux(c,D,A,B,0)
#define ppc_mulhwud(c,D,A,B) ppc_mulhwux(c,D,A,B,1)

#define ppc_mulli(c,D,A,SIMM) ppc_emit32(c, ((07) << 26) | (D << 21) | (A << 16) | (unsigned short)(SIMM))

#define ppc_mullwx(c,D,A,B,OE,Rc) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (OE << 10) | (235 << 1) | Rc)
#define ppc_mullw(c,D,A,B) ppc_mullwx(c,D,A,B,0,0)
#define ppc_mullwd(c,D,A,B) ppc_mullwx(c,D,A,B,0,1)
#define ppc_mullwo(c,D,A,B) ppc_mullwx(c,D,A,B,1,0)
#define ppc_mullwod(c,D,A,B) ppc_mullwx(c,D,A,B,1,1)

#define ppc_nandx(c,A,S,B,Rc) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (476 << 1) | Rc)
#define ppc_nand(c,A,S,B) ppc_nandx(c,A,S,B,0)
#define ppc_nandd(c,A,S,B) ppc_nandx(c,A,S,B,1)

#define ppc_negx(c,D,A,OE,Rc) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (0 << 11) | (OE << 10) | (104 << 1) | Rc)
#define ppc_neg(c,D,A) ppc_negx(c,D,A,0,0)
#define ppc_negd(c,D,A) ppc_negx(c,D,A,0,1)
#define ppc_nego(c,D,A) ppc_negx(c,D,A,1,0)
#define ppc_negod(c,D,A) ppc_negx(c,D,A,1,1)

#define ppc_norx(c,A,S,B,Rc) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (124 << 1) | Rc)
#define ppc_nor(c,A,S,B) ppc_norx(c,A,S,B,0)
#define ppc_nord(c,A,S,B) ppc_norx(c,A,S,B,1)

#define ppc_not(c,A,S) ppc_norx(c,A,S,S,0)

#define ppc_orx(c,A,S,B,Rc) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (444 << 1) | Rc)
#define ppc_ord(c,A,S,B) ppc_orx(c,A,S,B,1)

#define ppc_orcx(c,A,S,B,Rc) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (412 << 1) | Rc)
#define ppc_orc(c,A,S,B) ppc_orcx(c,A,S,B,0)
#define ppc_orcd(c,A,S,B) ppc_orcx(c,A,S,B,1)

#define ppc_oris(c,A,S,UIMM) ppc_emit32(c, (25 << 26) | (S << 21) | (A << 16) | (unsigned short)(UIMM))

#define ppc_rfi(c) ppc_emit32(c, (19 << 26) | (0 << 11) | (50 << 1) | 0)

/* rlw* updated to avoid gcc warnings - Zhe */
#define ppc_rlwimix(c,A,S,SH,MB,ME,Rc) ppc_emit32(c, (20 << 26) | (S << 21) | (A << 16) | ((SH) << 11) | ((MB) << 6) | ((ME) << 1) | Rc)
#define ppc_rlwimi(c,A,S,SH,MB,ME) ppc_rlwimix(c,A,S,SH,MB,ME,0)
#define ppc_rlwimid(c,A,S,SH,MB,ME) ppc_rlwimix(c,A,S,SH,MB,ME,1)

#define ppc_rlwinmx(c,A,S,SH,MB,ME,Rc) ppc_emit32(c, (21 << 26) | (S << 21) | (A << 16) | ((SH) << 11) | ((MB) << 6) | ((ME) << 1) | Rc)
#define ppc_rlwinm(c,A,S,SH,MB,ME) ppc_rlwinmx(c,A,S,SH,MB,ME,0)
#define ppc_rlwinmd(c,A,S,SH,MB,ME) ppc_rlwinmx(c,A,S,SH,MB,ME,1)

#define ppc_rlwnmx(c,A,S,SH,MB,ME,Rc) ppc_emit32(c, (23 << 26) | (S << 21) | (A << 16) | ((SH) << 11) | ((MB) << 6) | ((ME) << 1) | Rc)
#define ppc_rlwnm(c,A,S,SH,MB,ME) ppc_rlwnmx(c,A,S,SH,MB,ME,0)
#define ppc_rlwnmd(c,A,S,SH,MB,ME) ppc_rlwnmx(c,A,S,SH,MB,ME,1)

#define ppc_sc(c) ppc_emit32(c, (17 << 26) | (0 << 2) | (1 << 1) | 0)

/* slw* updated as pem.fm & MPCFPE32B - Zhe */
#define ppc_slwx(c,A,S,B,Rc) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (24 << 1) | Rc)
#define ppc_slw(c,A,S,B) ppc_slwx(c,A,S,B,0)
#define ppc_slwd(c,A,S,B) ppc_slwx(c,A,S,B,1)

#define ppc_srawx(c,A,S,B,Rc) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (792 << 1) | Rc)
#define ppc_sraw(c,A,S,B) ppc_srawx(c,A,S,B,0)
#define ppc_srawd(c,A,S,B) ppc_srawx(c,A,S,B,1)

#define ppc_srawix(c,A,S,SH,Rc) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (SH << 11) | (824 << 1) | Rc)
#define ppc_srawi(c,A,S,B) ppc_srawix(c,A,S,B,0)
#define ppc_srawid(c,A,S,B) ppc_srawix(c,A,S,B,1)

#define ppc_srwx(c,A,S,SH,Rc) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (SH << 11) | (536 << 1) | Rc)
#define ppc_srw(c,A,S,B) ppc_srwx(c,A,S,B,0)
#define ppc_srwd(c,A,S,B) ppc_srwx(c,A,S,B,1)

/* stbu updated as pem.fm & MPCFPE32B - Zhe */
#define ppc_stbu(c,S,d,A) ppc_emit32(c, (39 << 26) | (S << 21) | (A << 16) | (unsigned short)(d))

#define ppc_stbux(c,S,A,B) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (247 << 1) | 0)
#define ppc_stbx(c,S,A,B) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (215 << 1) | 0)

/* stfdu updated as pem.fm & MPCFPE32B - Zhe */
#define ppc_stfdu(c,S,d,A) ppc_emit32(c, (55 << 26) | (S << 21) | (A << 16) | (unsigned short)(d))

#define ppc_stfdx(c,S,A,B) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (727 << 1) | 0)
#define ppc_stfiwx(c,S,A,B) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (983 << 1) | 0)

/* stfsu, sthu, stmw updated as pem.fm & MPCFPE32B - Zhe */
#define ppc_stfsu(c,S,d,A) ppc_emit32(c, (53 << 26) | (S << 21) | (A << 16) | (unsigned short)(d))
#define ppc_stfsux(c,S,A,B) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (695 << 1) | 0)  
#define ppc_stfsx(c,S,A,B) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (663 << 1) | 0)  
#define ppc_sthbrx(c,S,A,B) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (918 << 1) | 0)  
#define ppc_sthu(c,S,d,A) ppc_emit32(c, (45 << 26) | (S << 21) | (A << 16) | (unsigned short)(d))
#define ppc_sthux(c,S,A,B) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (439 << 1) | 0)
#define ppc_sthx(c,S,A,B) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (407 << 1) | 0)
#define ppc_stmw(c,S,d,A) ppc_emit32(c, (47 << 26) | (S << 21) | (A << 16) | (unsigned short)(d))
#define ppc_stswi(c,S,A,NB) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (NB << 11) | (725 << 1) | 0)
#define ppc_stswx(c,S,A,NB) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (NB << 11) | (661 << 1) | 0)
#define ppc_stwbrx(c,S,A,B) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (662 << 1) | 0)
#define ppc_stwcxd(c,S,A,B) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (150 << 1) | 1)
#define ppc_stwux(c,S,A,B) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (183 << 1) | 0)
#define ppc_stwx(c,S,A,B) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (151 << 1) | 0)

#define ppc_subfx(c,D,A,B,OE,Rc) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (OE << 10) | (40 << 1) | Rc)
#define ppc_subf(c,D,A,B) ppc_subfx(c,D,A,B,0,0)
#define ppc_subfd(c,D,A,B) ppc_subfx(c,D,A,B,0,1)
#define ppc_subfo(c,D,A,B) ppc_subfx(c,D,A,B,1,0)
#define ppc_subfod(c,D,A,B) ppc_subfx(c,D,A,B,1,1)

#define ppc_sub(c,D,A,B) ppc_subf(c,D,B,A)

#define ppc_subfcx(c,D,A,B,OE,Rc) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (OE << 10) | (8 << 1) | Rc)
#define ppc_subfc(c,D,A,B) ppc_subfcx(c,D,A,B,0,0)
#define ppc_subfcd(c,D,A,B) ppc_subfcx(c,D,A,B,0,1)
#define ppc_subfco(c,D,A,B) ppc_subfcx(c,D,A,B,1,0)
#define ppc_subfcod(c,D,A,B) ppc_subfcx(c,D,A,B,1,1)

#define ppc_subfex(c,D,A,B,OE,Rc) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (B << 11) | (OE << 10) | (136 << 1) | Rc)
#define ppc_subfe(c,D,A,B) ppc_subfex(c,D,A,B,0,0)
#define ppc_subfed(c,D,A,B) ppc_subfex(c,D,A,B,0,1)
#define ppc_subfeo(c,D,A,B) ppc_subfex(c,D,A,B,1,0)
#define ppc_subfeod(c,D,A,B) ppc_subfex(c,D,A,B,1,1)

#define ppc_subfic(c,D,A,SIMM) ppc_emit32(c, (8 << 26) | (D << 21) | (A << 16) | (unsigned short)(SIMM))

#define ppc_subfmex(c,D,A,OE,Rc) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (0 << 11) | (OE << 10) | (232 << 1) | Rc)
#define ppc_subfme(c,D,A) ppc_subfmex(c,D,A,0,0)
#define ppc_subfmed(c,D,A) ppc_subfmex(c,D,A,0,1)
#define ppc_subfmeo(c,D,A) ppc_subfmex(c,D,A,1,0)
#define ppc_subfmeod(c,D,A) ppc_subfmex(c,D,A,1,1)

#define ppc_subfzex(c,D,A,OE,Rc) ppc_emit32(c, (31 << 26) | (D << 21) | (A << 16) | (0 << 11) | (OE << 10) | (200 << 1) | Rc)
#define ppc_subfze(c,D,A) ppc_subfzex(c,D,A,0,0)
#define ppc_subfzed(c,D,A) ppc_subfzex(c,D,A,0,1)
#define ppc_subfzeo(c,D,A) ppc_subfzex(c,D,A,1,0)
#define ppc_subfzeod(c,D,A) ppc_subfzex(c,D,A,1,1)

#define ppc_sync(c) ppc_emit32(c, (31 << 26) | (0 << 11) | (598 << 1) | 0)
#define ppc_tlbia(c) ppc_emit32(c, (31 << 26) | (0 << 11) | (370 << 1) | 0)
#define ppc_tlbie(c,B) ppc_emit32(c, (31 << 26) | (0 << 16) | (B << 11) | (306 << 1) | 0)
#define ppc_tlbsync(c) ppc_emit32(c, (31 << 26) | (0 << 11) | (566 << 1) | 0)

#define ppc_tw(c,TO,A,B) ppc_emit32(c, (31 << 26) | (TO << 21) | (A << 16) | (B << 11) | (4 << 1) | 0)
#define ppc_twi(c,TO,A,SIMM) ppc_emit32(c, (3 << 26) | (TO << 21) | (A << 16) | (unsigned short)(SIMM))

#define ppc_xorx(c,A,S,B,RC) ppc_emit32(c, (31 << 26) | (S << 21) | (A << 16) | (B << 11) | (316 << 1) | RC)
#define ppc_xor(c,A,S,B) ppc_xorx(c,A,S,B,0)
#define ppc_xord(c,A,S,B) ppc_xorx(c,A,S,B,1)

/* xori* updated as pem.fm & MPCFPE32B - Zhe */
#define ppc_xori(c,A,S,UIMM) ppc_emit32(c, (26 << 26) | (S << 21) | (A << 16) | (unsigned short)(UIMM))
#define ppc_xoris(c,A,S,UIMM) ppc_emit32(c, (27 << 26) | (S << 21) | (A << 16) | (unsigned short)(UIMM))

/* this marks the end of my work, ct */

/*
 * The following codes come from mini-ppc.c:
 * PowerPC backend for the Mono code generator
 *
 * Authors:
 *   Paolo Molaro (lupus@ximian.com)
 *   Dietmar Maurer (dietmar@ximian.com)
 *
 * (C) 2003 Ximian, Inc.
 */

/* deal with some of the ABI differences here */
#ifdef __APPLE__
#define PPC_RET_ADDR_OFFSET 8
#define PPC_STACK_ALIGNMENT 16
#define PPC_STACK_PARAM_OFFSET 24
#define PPC_MINIMAL_STACK_SIZE 24
#define PPC_FIRST_ARG_REG ppc_r3
#define PPC_LAST_ARG_REG ppc_r10
#define PPC_FIRST_FPARG_REG ppc_f1
#define PPC_LAST_FPARG_REG ppc_f13
#define PPC_PASS_STRUCTS_BY_VALUE 1
#else
/* SVR4 - Linux, NetBSD, etc. */
#define PPC_RET_ADDR_OFFSET 4
#define PPC_STACK_ALIGNMENT 16
#define PPC_STACK_PARAM_OFFSET 8
#define PPC_MINIMAL_STACK_SIZE 8
#define PPC_FIRST_ARG_REG ppc_r3
#define PPC_LAST_ARG_REG ppc_r10
#define PPC_FIRST_FPARG_REG ppc_f1
#define PPC_LAST_FPARG_REG ppc_f8
/* set the next to 0 once inssel-ppc.brg is updated */
#define PPC_PASS_STRUCTS_BY_VALUE 1
#define PPC_SMALL_RET_STRUCT_IN_REG 1

#endif

// code from ppc/tramp.c, try to keep in sync
#define MIN_CACHE_LINE 8

static inline void mono_ppc_flush_icache (unsigned char *code, int size)
{
	unsigned int i;
	unsigned char *p;

	p = code;
	/* use dcbf for smp support, later optimize for UP, see pem._64bit.d20030611.pdf page 211 */
	if (1) {
		for (i = 0; i < size; i += MIN_CACHE_LINE, p += MIN_CACHE_LINE) {
			asm ("dcbf 0,%0;" : : "r"(p) : "memory");
		}
	} else {
		for (i = 0; i < size; i += MIN_CACHE_LINE, p += MIN_CACHE_LINE) {
			asm ("dcbst 0,%0;" : : "r"(p) : "memory");
		}
	}
	asm ("sync");
	p = code;
	for (i = 0; i < size; i += MIN_CACHE_LINE, p += MIN_CACHE_LINE) {
		asm ("icbi 0,%0; sync;" : : "r"(p) : "memory");
	}
	asm ("sync");
	asm ("isync");
}

static inline void ppc_patch (unsigned char *code, unsigned char *target)
{
	unsigned int ins = *(unsigned int*)code;
	unsigned int prim = ins >> 26;
	unsigned int ovf;

	//g_print ("patching 0x%08x (0x%08x) to point to 0x%08x\n", code, ins, target);
	if (prim == 18) {
		// prefer relative branches, they are more position independent (e.g. for AOT compilation).
		int diff = target - code;
		if (diff >= 0){
			if (diff <= 33554431){
				ins = (18 << 26) | (diff) | (ins & 1);
				*(unsigned int*)code = ins;
				return;
			}
		} else {
			/* diff between 0 and -33554432 */
			if (diff >= -33554432){
				ins = (18 << 26) | (diff & ~0xfc000000) | (ins & 1);
				*(unsigned int*)code = ins;
				return;
			}
		}
		
		if ((long)target >= 0){
			if ((long)target <= 33554431){
				ins = (18 << 26) | ((unsigned int) target) | (ins & 1) | 2;
				*(unsigned int*)code = ins;
				return;
			}
		} else {
			if ((long)target >= -33554432){
				ins = (18 << 26) | (((unsigned int)target) & ~0xfc000000) | (ins & 1) | 2;
				*(unsigned int*)code = ins;
				return;
			}
		}

//		handle_thunk (TRUE, code, target);
		return;

		assert (0);
	}
	
	
	if (prim == 16) {
		// absolute address
		if (ins & 2) {
			unsigned int li = (unsigned int)target;
			ins = (ins & 0xffff0000) | (ins & 3);
			ovf  = li & 0xffff0000;
			if (ovf != 0 && ovf != 0xffff0000)
				assert (0);
			li &= 0xffff;
			ins |= li;
			// FIXME: assert the top bits of li are 0
		} else {
			int diff = target - code;
			ins = (ins & 0xffff0000) | (ins & 3);
			ovf  = diff & 0xffff0000;
			if (ovf != 0 && ovf != 0xffff0000)
				assert (0);
			diff &= 0xffff;
			ins |= diff;
		}
		*(unsigned int*)code = ins;
		return;
	}

	if (prim == 15 || ins == 0x4e800021) {
		unsigned int *seq;
		/* the trampoline code will try to patch the blrl */
		if (ins == 0x4e800021) {
			code -= 12;
		}
		/* this is the lis/ori/mtlr/blrl sequence */
		seq = (unsigned int*)code;
		assert ((seq [0] >> 26) == 15);
		assert ((seq [1] >> 26) == 24);
		assert ((seq [2] >> 26) == 31);
		assert (seq [3] == 0x4e800021);
		/* FIXME: make this thread safe */
		ppc_lis (code, ppc_r0, (unsigned int)(target) >> 16);
		ppc_ori (code, ppc_r0, ppc_r0, (unsigned int)(target) & 0xffff);
		mono_ppc_flush_icache (code - 8, 8);
	} else {
		assert (0);
	}
//	g_print ("patched with 0x%08x\n", ins);
}

/* Works done by Zhe Fang (fangzhe@msn.com)
   Most macros here are based on the assembler instructions can
   be founded in

   "Appendix F. Simplified Mnemonics" of IBM's

   "PowerPC Microprocessor Family:
   The Programming Enviornments for 32-Bit Microprocessors"

   G522-0290-01
   02/21/2000

   also referenced in
   
   "Appendix E. Simplified Mnemonics" of IBM's
   
   "PowerPC Microprocessor Family:
   The Programming Enviornments for 32 and 64-Bit Microprocessors"

   pem.fm.2.3
   March 31, 2005

   and

   "Appendix E. Simplified Mnemonics for PowerPC Instructions"
   of Freescale Semiconductor's
   
   "Programming Enviornments Manual for 32-Bit Implementations
   of the PowerPC Architecture"
   
   MPCFPE32B
   Rev. 3, 9/2005

   The Branch-Conditional-Prediction relative codes are followed
   the descriptions in

   "4.2.4.2 Conditional Branch Control" of IBM's

   "PowerPC Microprocessor Family:
   The Programming Enviornments Manual for 32 and 64-bit Microprocessors"

   pem.fm.2.3
   March 31, 2005

   and also compatiable with other manuals above
*/
 
typedef enum {
	ppc_cr0 = 0,
	ppc_cr1,
	ppc_cr2,
	ppc_cr3,
	ppc_cr4,
	ppc_cr5,
	ppc_cr6,
	ppc_cr7
} PPCCRField;

#define ppc_crbf(f,b) (4*f+b)

enum {
	/* for BO */
	PPC_BR_FALSE_UNLIKELY  = 6,
	PPC_BR_FALSE_LIKELY  = 7,
	PPC_BR_TRUE_UNLIKELY   = 14,
	PPC_BR_TRUE_LIKELY   = 15,
	PPC_BR_DEC_CTR_NONZERO_UNLIKELY = 24,
	PPC_BR_DEC_CTR_NONZERO_LIKELY = 25,
	PPC_BR_DEC_CTR_ZERO_UNLIKELY = 26,
	PPC_BR_DEC_CTR_ZERO_LIKELY = 27,
	/* for BH */
	PPC_BR_BCLR_SUB_RETURN = 0,
	PPC_BR_BCCTR_NOT_SUB_RETURN = 0,
	PPC_BR_BCLR_NOT_SUB_RETURN = 1,
	PPC_BR_BCLR_NOT_PREDICTABLE  = 3,
	PPC_BR_BCCTR_NOT_PREDICTABLE  = 3
};

/*#define ppc_bcctrx(c,BO,BI,BH,LK) ppc_emit32(c, (19 << 26) | (BO << 21 )| (BI << 16) | (BH << 11) | (528 << 1) | LK)
#define ppc_bcctr(c,BO,BI,BH) ppc_bcctrx(c,BO,BI,BH,0)
#define ppc_bcctrl(c,BO,BI,BH) ppc_bcctrx(c,BO,BI,BH,1)

#define ppc_bclrx(c,BO,BI,BH,LK) ppc_emit32(c, (19 << 26) | (BO << 21 )| (BI << 16) | (BH << 11) | (16 << 1) | LK)
#define ppc_bclr(c,BO,BI,BH) ppc_bclrx(c,BO,BI,BH,0)
#define ppc_bclrl(c,BO,BI,BH) ppc_bclrx(c,BO,BI,BH,1)*/

#define ppc_subi(c,D,A,d) ppc_addi(c,D,A,-(d))
#define ppc_subis(c,D,A,d) ppc_addis(c,D,A,-(d))
#define ppc_subic(c,D,A,d) ppc_addic(c,D,A,-(d))
#define ppc_subicd(c,D,A,d) ppc_addicd(c,D,A,-(d))

#define ppc_sub(c,D,A,B) ppc_subf(c,D,B,A)
#define ppc_subc(c,D,A,B) ppc_subfc(c,D,B,A)

#define ppc_cmpwi(c,cfrD,A,B) ppc_cmpi(c,cfrD,0,A,B)
#define ppc_cmpw(c,cfrD,A,B) ppc_cmp(c,cfrD,0,A,B)
#define ppc_cmplwi(c,cfrD,A,B) ppc_cmpli(c,cfrD,0,A,B)
#define ppc_cmplw(c,cfrD,A,B) ppc_cmpl(c,cfrD,0,A,B)

#define ppc_extlwi(c,A,S,n,b) ppc_rlwinm(c,A,S,b,0,((n-1) & 0x1f))
#define ppc_extrwi(c,A,S,n,b) ppc_rlwinm(c,A,S,((b+n) & 0x1f),((32-(n)) & 0x1f),31)
#define ppc_inslwi(c,A,S,n,b) ppc_rlwimi(c,A,S,(32-(b) & 0x1f),b,((b+n-1) & 0x1f))
#define ppc_insrwi(c,A,S,n,b) ppc_rlwimi(c,A,S,((32-(b)-(n)) & 0x1f),b,((b+n-1) & 0x1f))
#define ppc_rotlwi(c,A,S,n) ppc_rlwinm(c,A,S,n,0,31)
#define ppc_rotrwi(c,A,S,n) ppc_rlwinm(c,A,S,((32-(n)) & 0x1f),0,31)
#define ppc_rotlw(c,A,S,B) ppc_rlwinm(c,A,S,B,0,31)
#define ppc_slwi(c,A,S,n) ppc_rlwinm(c,A,S,n,0,31-(n))
#define ppc_srwi(c,A,S,n) ppc_rlwinm(c,A,S,((32-(n)) & 0x1f),n,31)
#define ppc_clrlwi(c,A,S,n) ppc_rlwinm(c,A,S,0,n,31)
#define ppc_clrrwi(c,A,S,n) ppc_rlwinm(c,A,S,0,0,31-(n))
#define ppc_clrlslwi(c,A,S,b,n) ppc_rlwinm(c,A,S,n,((b-(n)) & 0x1f),31-(n))

#define ppc_bctr(c) ppc_emit32(c,0x4e800420)
#define ppc_bctrl(c) ppc_emit32(c,0x4e800421)


#define ppc_crset(c,bx) ppc_creqv(c,bx,bx,bx)
#define ppc_crclr(c,bx) ppc_crxor(c,bx,bx,bx)
#define ppc_crmove(c,bx,by) ppc_cror(c,bx,by,by)
#define ppc_crnot(c,bx,by) ppc_crnor(c,bx,by,by)


#define ppc_nop(c) ppc_ori(c,0,0,0)

//#define ppc_la(c,D,d,A) ppc_addi(c,D,A,d)
#define ppc_la(c,D,v) ppc_addi(c,D,v,v)

#define ppc_mtcr(c,S) ppc_mtcrf(c,0xff,S)

/* Helper macros for assembler */
#define ppc_hi16(v) ((unsigned int)(v) >> 16)
#define ppc_ha16(v) (ppc_hi16(v) + ((v & 0x8000) >> 15))
#define ppc_lo16(v) ((unsigned int)(v) & 0xffff)

#endif
