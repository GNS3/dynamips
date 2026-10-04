/*
 * arm64-codegen.h: Macros for generating arm64 code
 */


#ifndef ARM64_H
#define ARM64_H

#define MAX_LD64_IMM_DISP 32760

typedef enum {
    ARM64_X0 = 0,
    ARM64_X1 = 1,
    ARM64_X2 = 2,
    ARM64_X3 = 3,
    ARM64_X4 = 4,
    ARM64_X5 = 5,
    ARM64_X6 = 6,
    ARM64_X7 = 7,
    ARM64_X8 = 8,
    ARM64_X9 = 9,
    ARM64_X10 = 10,
    ARM64_X11 = 11,
    ARM64_X12 = 12,
    ARM64_X13 = 13,
    ARM64_X14 = 14,
    ARM64_X15 = 15,
    ARM64_X16 = 16,
    ARM64_X17 = 17,
    ARM64_X18 = 18,
    ARM64_X19 = 19,
    ARM64_X20 = 20,
    ARM64_X21 = 21,
    ARM64_X22 = 22,
    ARM64_X23 = 23,
    ARM64_X24 = 24,
    ARM64_X25 = 25,
    ARM64_X26 = 26,
    ARM64_X27 = 27,
    ARM64_X28 = 28,
    ARM64_FP = 29,
    ARM64_LR = 30,
    ARM64_SP = 31,
    ARM64_XZR = 31,
} ARM64_Reg_No;

typedef enum {
    ARM64_W0 = 0,
    ARM64_W1 = 1,
    ARM64_W2 = 2,
    ARM64_W3 = 3,
    ARM64_W4 = 4,
    ARM64_W5 = 5,
    ARM64_W6 = 6,
    ARM64_W7 = 7,
    ARM64_W8 = 8,
    ARM64_W9 = 9,
    ARM64_W10 = 10,
    ARM64_W11 = 11,
    ARM64_W12 = 12,
    ARM64_W13 = 13,
    ARM64_W14 = 14,
    ARM64_W15 = 15,
    ARM64_W16 = 16,
    ARM64_W17 = 17,
    ARM64_W18 = 18,
    ARM64_W19 = 19,
    ARM64_W20 = 20,
    ARM64_W21 = 21,
    ARM64_W22 = 22,
    ARM64_W23 = 23,
    ARM64_W24 = 24,
    ARM64_W25 = 25,
    ARM64_W26 = 26,
    ARM64_W27 = 27,
    ARM64_W28 = 28,
    ARM64_W29 = 29,
    ARM64_W30 = 30,
    ARM64_WSP = 31,
    ARM64_WZR = 31,
} ARM64_WReg_No;

typedef enum {
    ARM64_M_UXTW = 2,
    ARM64_M_LSL = 3,
    ARM64_M_SXTW = 6,
    ARM64_M_SXTX = 7,
} ARM64_ExtShiftM;

typedef enum {
    ARM64_A_LSL = 0,
    ARM64_A_LSR = 1,
    ARM64_A_ASR = 2,
    ARM64_A_ROR = 3,
} ARM64_ShiftA;

typedef enum {
    ARM64_CC_EQ,
    ARM64_CC_NE,
    ARM64_CC_CS,
    ARM64_CC_CC,
    ARM64_CC_MI,
    ARM64_CC_PL,
    ARM64_CC_VS,
    ARM64_CC_VC,
    ARM64_CC_HI,
    ARM64_CC_LS,
    ARM64_CC_GE,
    ARM64_CC_LT,
    ARM64_CC_GT,
    ARM64_CC_LE,
    ARM64_CC_AL,
    ARM64_CC_NV,
} ARM64_BrCond;

#define arm64_emit32(c,x) do { *((unsigned int *) (c)) = x; (c) = (unsigned char *)(c) + sizeof (unsigned int);} while (0)

#define arm64_ret(c) arm64_emit32((c), 0xD65F03C0)
#define arm64_movk64(c, reg, imm, shift) arm64_emit32((c), 0xF2800000 | ((shift) >> 4 << 21) | ((imm) << 5) | (reg))
#define arm64_movz64(c, reg, imm, shift) arm64_emit32((c), 0xD2800000 | ((shift) >> 4 << 21) | ((imm) << 5) | (reg))
#define arm64_str64_imm(c, xt, xn, offs) arm64_emit32((c), 0xF9000000 | ((offs) / 8 << 10) | ((xn) << 5) | (xt))
#define arm64_str32_imm(c, xt, xn, offs) arm64_emit32((c), 0xB9000000 | ((offs) / 4 << 10) | ((xn) << 5) | (xt))
#define arm64_ldr64_imm(c, xt, xn, offs) arm64_emit32((c), 0xF9400000 | ((offs) / 8 << 10) | ((xn) << 5) | (xt))
#define arm64_ldr32_imm(c, wt, xn, offs) arm64_emit32((c), 0xB9400000 | ((offs) / 4 << 10) | ((xn) << 5) | (wt))
#define arm64_ldr64_reg(c, xt, xn, xm, extend, shift) arm64_emit32((c), 0xF8600800 | ((xm) << 16) | ((extend) << 13) | ((shift) / 3 << 12) | ((xn) << 5) | (xt))
#define arm64_ldr32_reg(c, wt, xn, xm, extend, shift) arm64_emit32((c), 0xB8600800 | ((xm) << 16) | ((extend) << 13) | ((shift) / 2 << 12) | ((xn) << 5) | (wt))
#define arm64_str64_reg(c, xt, xn, xm, extend, shift) arm64_emit32((c), 0xF8200800 | ((xm) << 16) | ((extend) << 13) | ((shift) / 3 << 12) | ((xn) << 5) | (xt))
#define arm64_str32_reg(c, wt, xn, xm, extend, shift) arm64_emit32((c), 0xB8200800 | ((xm) << 16) | ((extend) << 13) | ((shift) / 3 << 12) | ((xn) << 5) | (wt))
#define arm64_stp64_preidx(c, xt1, xt2, xn, imm) arm64_emit32((c), 0xA9800000 | (((imm) / 8 & 0x7F) << 15) | ((xt2) << 10) | ((xn) << 5) | (xt1))
#define arm64_ldp64_postidx(c, xt1, xt2, xn, imm) arm64_emit32((c), 0xA8C00000 | (((imm) / 8 & 0x7F) << 15) | ((xt2) << 10) | ((xn) << 5) | (xt1))
#define arm64_mov64(c, xd, xm) arm64_emit32((c), 0xAA0003E0 | ((xm) << 16) | (xd))
#define arm64_mov32(c, xd, xm) arm64_emit32((c), 0x2A0003E0 | ((xm) << 16) | (xd))
#define arm64_blr(c, xn) arm64_emit32((c), 0xD63F0000 | ((xn) << 5))
#define arm64_and64_reg(c, xd, xn, xm, shift, amount) arm64_emit32((c), 0x8A000000 | ((shift) << 22) | ((xm) << 16) | ((amount) << 10) | ((xn) << 5) | (xd))
#define arm64_and32_reg(c, wd, wn, wm, shift, amount) arm64_emit32((c), 0x0A000000 | ((shift) << 22) | ((wm) << 16) | ((amount) << 10) | ((wn) << 5) | (wd))
#define arm64_ands64_reg(c, xd, xn, xm, shift, amount) arm64_emit32((c), 0xEA000000 | ((shift) << 22) | ((xm) << 16) | ((amount) << 10) | ((xn) << 5) | (xd))
#define arm64_tst64_reg(c, xn, xm, shift, amount) arm64_ands64_reg(c, ARM64_XZR, xn, xm, shift, amount)
#define arm64_bcond(c, cond, offs) arm64_emit32((c), 0x54000000 | ((offs) / 4 << 5) | cond)
#define arm64_b(c, offs) arm64_emit32((c), 0x14000000 | ((offs) / 4 & 0x3FFFFFF))
#define arm64_bl(c, offs) arm64_emit32((c), 0x94000000 | ((offs) / 4 & 0x3FFFFFF))
#define arm64_jump_code(inst, target) \
   do { \
      int t = (unsigned char*)(target) - (inst); \
      arm64_b(inst, t); \
   } while (0)

#define arm64_cbz64(c, xt, offs) arm64_emit32((c), 0xB4000000 | ((offs) / 4 << 5) | xt)
#define arm64_cbnz64(c, xt, offs) arm64_emit32((c), 0xB5000000 | ((offs) / 4 << 5) | xt)
#define arm64_add64_imm(c, xd, xn, imm, shift) arm64_emit32((c), 0x91000000 | ((shift) / 12 << 22) | ((imm) << 10) | ((xn) << 5) | (xd))
#define arm64_add32_imm(c, wd, wn, imm, shift) arm64_emit32((c), 0x11000000 | ((shift) / 12 << 22) | ((imm) << 10) | ((wn) << 5) | (wd))
#define arm64_add64_reg(c, xd, xn, xm, shift, amount) arm64_emit32((c), 0x8B000000 | ((shift) << 22) | ((xm) << 16) | ((amount) << 10) | ((xn) << 5) | (xd))
#define arm64_add32_reg(c, wd, wn, wm, shift, amount) arm64_emit32((c), 0x0B000000 | ((shift) << 22) | ((wm) << 16) | ((amount) << 10) | ((wn) << 5) | (wd))
#define arm64_subs64_reg(c, xd, xn, xm, shift, amount) arm64_emit32((c), 0xEB000000 | ((shift) << 22) | ((xm) << 16) | ((amount) << 10) | ((xn) << 5) | (xd))
#define arm64_cmp64_reg(c, xn, xm, shift, amount) arm64_subs64_reg((c), ARM64_XZR, (xn), (xm), (shift), (amount))
#define arm64_subs64_imm(c, xd, xn, imm, shift) arm64_emit32((c), 0xF1000000 | ((shift) / 12 << 22) | ((imm) << 10) | ((xn) << 5) | (xd))
#define arm64_cmp64_imm(c, xn, imm, shift) arm64_subs64_imm((c), ARM64_XZR, (xn), (imm), (shift))
#define arm64_adds64_imm(c, xd, xn, imm, shift) arm64_emit32((c), 0xB1000000 | ((shift) / 12 << 22) | ((imm) << 10) | ((xn) << 5) | (xd))
#define arm64_cmn64_imm(c, xn, imm, shift) arm64_adds64_imm((c), ARM64_XZR, (xn), (imm), (shift))
#define arm64_sbfm64(c, xd, xn, immr, imms) arm64_emit32((c), 0x93400000 | ((immr) << 16) | ((imms) << 10) | ((xn) << 5) | (xd))
#define arm64_sbfm32(c, wd, wn, immr, imms) arm64_emit32((c), 0x13000000 | ((immr) << 16) | ((imms) << 10) | ((wn) << 5) | (wd))
#define arm64_sxtw(c, xd, wn) arm64_sbfm64((c), (xd), (wn), 0, 31)
#define arm64_udiv32(c, wd, wn, wm) arm64_emit32((c), 0x1AC00800 | ((wm) << 16) | ((wn) << 5) | (wd))
#define arm64_sdiv32(c, wd, wn, wm) arm64_emit32((c), 0x1AC00C00 | ((wm) << 16) | ((wn) << 5) | (wd))
#define arm64_msub32(c, wd, wn, wm, wa) arm64_emit32((c), 0x1B008000 | ((wm) << 16) | ((wa) << 10) | ((wn) << 5) | (wd))
#define arm64_ubfm64(c, xd, xn, immr, imms) arm64_emit32((c), 0xD3400000 | ((immr) << 16) | ((imms) << 10) | ((xn) << 5) | (xd))
#define arm64_ubfm32(c, wd, wn, immr, imms) arm64_emit32((c), 0x53000000 | ((immr) << 16) | ((imms) << 10) | ((wn) << 5) | (wd))
#define arm64_lsl64(c, xd, xn, shift) arm64_ubfm64((c), (xd), (xn), -(shift) & 63, 63 - (shift))
#define arm64_lsl32(c, wd, wn, shift) arm64_ubfm32((c), (wd), (wn), -(shift) & 31, 31 - (shift))
#define arm64_lslv64(c, xd, xn, xm) arm64_emit32((c), 0x9AC02000 | ((xm) << 16) | ((xn) << 5) | (xd))
#define arm64_lslv32(c, xd, xn, xm) arm64_emit32((c), 0x1AC02000 | ((xm) << 16) | ((xn) << 5) | (xd))
#define arm64_asr64(c, xd, xn, shift) arm64_sbfm64((c), (xd), (xn), shift, 63)
#define arm64_asr32(c, wd, wn, shift) arm64_sbfm32((c), (wd), (wn), shift, 31)
#define arm64_asrv64(c, xd, xn, xm) arm64_emit32((c), 0x9AC02800 | ((xm) << 16) | ((xn) << 5) | (xd))
#define arm64_asrv32(c, wd, wn, wm) arm64_emit32((c), 0x1AC02800 | ((wm) << 16) | ((wn) << 5) | (wd))
#define arm64_lsr64(c, xd, xn, shift) arm64_ubfm64((c), (xd), (xn), shift, 63)
#define arm64_lsr32(c, wd, wn, shift) arm64_ubfm32((c), (wd), (wn), shift, 31)
#define arm64_lsrv64(c, xd, xn, xm) arm64_emit32((c), 0x9AC02400 | ((xm) << 16) | ((xn) << 5) | (xd))
#define arm64_lsrv32(c, wd, wn, wm) arm64_emit32((c), 0x1AC02400 | ((wm) << 16) | ((wn) << 5) | (wd))
#define arm64_sub64_reg(c, xd, xn, xm, shift, amount) arm64_emit32((c), 0xCB000000 | ((shift) << 22) | ((xm) << 16) | ((amount) << 10) | ((xn) << 5) | (xd))
#define arm64_madd32(c, wd, wn, wm, wa) arm64_emit32((c), 0x1B000000 | ((wm) << 16) | ((wa) << 10) | ((wn) << 5) | (wd))
#define arm64_mul32(c, wd, wn, wm) arm64_madd32((c), (wd), (wn), (wm), ARM64_WZR)
#define arm64_smaddl(c, xd, wn, wm, xa) arm64_emit32((c), 0x9B200000 | ((wm) << 16) | ((xa) << 10) | ((wn) << 5) | (xd))
#define arm64_smull(c, xd, wn, wm) arm64_smaddl((c), (xd), (wn), (wm), ARM64_WZR)
#define arm64_umaddl(c, xd, wn, wm, xa) arm64_emit32((c), 0x9BA00000 | ((wm) << 16) | ((xa) << 10) | ((wn) << 5) | (xd))
#define arm64_umull(c, xd, wn, wm) arm64_umaddl((c), (xd), (wn), (wm), ARM64_WZR)
#define arm64_orr64_reg(c, xd, xn, xm, shift, amount) arm64_emit32((c), 0xAA000000 | ((shift) << 22) | ((xm) << 16) | ((amount) << 10) | ((xn) << 5) | (xd))
#define arm64_orn64_reg(c, xd, xn, xm, shift, amount) arm64_emit32((c), 0xAA200000 | ((shift) << 22) | ((xm) << 16) | ((amount) << 10) | ((xn) << 5) | (xd))
#define arm64_mvn64_reg(c, xd, xm, shift, amount) arm64_orn64_reg((c), (xd), ARM64_XZR, (xm), (shift), (amount))
#define arm64_nop(c) arm64_emit32((c), 0xD503201F)
#define arm64_eor64_reg(c, xd, xn, xm, shift, amount) arm64_emit32((c), 0xCA000000 | ((shift) << 22) | ((xm) << 16) | ((amount) << 10) | ((xn) << 5) | (xd))
#define arm64_eor32_reg(c, wd, wn, wm, shift, amount) arm64_emit32((c), 0x4A000000 | ((shift) << 22) | ((wm) << 16) | ((amount) << 10) | ((wn) << 5) | (wd))
#define arm64_rev64(c, xd, xn) arm64_emit32((c), 0xDAC00C00 | ((xn) << 5) | (xd))
#define arm64_rev32(c, xd, xn) arm64_emit32((c), 0xDAC00800 | ((xn) << 5) | (xd))
#define arm64_rev16(c, xd, xn) arm64_emit32((c), 0xDAC00400 | ((xn) << 5) | (xd))
#define arm64_mov64_sp(c, xd, xn) arm64_add64_imm((c), (xd), (xn), 0, 0)
#define arm64_br(c, xn) arm64_emit32((c), 0xD61F0000 | ((xn) << 5))
#define arm64_adrp(c, xd, offs) arm64_emit32((c), 0x90000000 | (((offs) >> 12 & 0x3) << 29) | (((offs) >> 14) << 5) | xd)
#define arm64_sub32_imm(c, wd, wn, imm, shift) arm64_emit32((c), 0x51000000 | ((shift) / 12 << 22) | ((imm) << 10) | ((wn) << 5) | (wd))
#define arm64_sub64_imm(c, xd, xn, imm, shift) arm64_emit32((c), 0xD1000000 | ((shift) / 12 << 22) | ((imm) << 10) | ((xn) << 5) | (xd))
#define arm64_movn64(c, reg, imm, shift) arm64_emit32((c), 0x92800000 | ((shift) >> 4 << 21) | ((imm) << 5) | (reg))
#define arm64_movn32(c, reg, imm, shift) arm64_emit32((c), 0x12800000 | ((shift) >> 4 << 21) | ((imm) << 5) | (reg))
#define arm64_and64_imm(c, xd, xn, bitmask) arm64_emit32((c), 0x92000000 | (bitmask) | ((xn) << 5) | (xd))
#define arm64_and32_imm(c, wd, wn, bitmask) arm64_emit32((c), 0x12000000 | (bitmask) | ((wn) << 5) | (wd))
#define arm64_ands64_imm(c, xd, xn, bitmask) arm64_emit32((c), 0xF2000000 | (bitmask) | ((xn) << 5) | (xd))
#define arm64_tst64_imm(c, xn, bitmask) arm64_ands64_imm((c), ARM64_XZR, (xn), (bitmask))
#define arm64_orr64_imm(c, xd, xn, bitmask) arm64_emit32((c), 0xB2000000 | (bitmask) | ((xn) << 5) | (xd))
#define arm64_eor64_imm(c, xd, xn, bitmask) arm64_emit32((c), 0xD2000000 | (bitmask) | ((xn) << 5) | (xd))
#define arm64_ldrb_reg(c, wt, xn, xm, extend, shift) arm64_emit32((c), 0x38600800 | ((xm) << 16) | ((extend) << 13) | ((shift) / 2 << 12) | ((xn) << 5) | (wt))
#define arm64_ldrh_reg(c, wt, xn, xm, extend, shift) arm64_emit32((c), 0x78600800 | ((xm) << 16) | ((extend) << 13) | ((shift) / 2 << 12) | ((xn) << 5) | (wt))
#define arm64_ldrsb64_reg(c, xt, xn, xm, extend, shift) arm64_emit32((c), 0x38A00800 | ((xm) << 16) | ((extend) << 13) | ((shift) / 2 << 12) | ((xn) << 5) | (xt))
#define arm64_ldrsh64_reg(c, xt, xn, xm, extend, shift) arm64_emit32((c), 0x78A00800 | ((xm) << 16) | ((extend) << 13) | ((shift) / 2 << 12) | ((xn) << 5) | (xt))
#define arm64_strh_reg(c, wt, xn, xm, extend, shift) arm64_emit32((c), 0x78200800 | ((xm) << 16) | ((extend) << 13) | ((shift) / 2 << 12) | ((xn) << 5) | (wt))
#define arm64_strb_reg(c, wt, xn, xm, extend, shift) arm64_emit32((c), 0x38200800 | ((xm) << 16) | ((extend) << 13) | ((shift) / 2 << 12) | ((xn) << 5) | (wt))

static inline int _arm64_is_mask(m_uint64_t val, m_uint64_t lim)
{
   val = (val - 1) | val;
   return val == lim || ((val + 1) & val) == 0;
}
static inline m_uint32_t arm64_encode_bitmasks(m_uint64_t val, size_t isz)
{
   m_uint64_t imm = val;
   m_uint64_t lim = isz == 64 ? 0xFFFFFFFFFFFFFFFF : 0xFFFFFFFF;
   m_uint64_t mask, to, lr, lo, immr, imms, n;
   m_uint32_t size = isz;
   if (!val || val == lim)
      return -1;
   for (;;) {
      size >>= 1;
      mask = (1ULL << size) - 1;
      if ((imm & mask) != ((imm >> size) & mask)) {
         size <<= 1;
         break;
      }
      if (size <= 2)
         break;
   }
   mask = lim >> (isz - size);
   imm &= mask;
   if (_arm64_is_mask(imm, lim)) {
      lr = __builtin_ctzll(imm);
      to = __builtin_ctzll(~(imm >> lr));
   } else {
      imm |= ~mask;
      if (!_arm64_is_mask(~imm, lim))
         return -1;
      lo = __builtin_clzll(~imm);
      lr = isz - lo;
      to = lo + __builtin_ctzll(~imm) - (isz - size);
   }
   immr = (size - lr) & (size - 1);
   imms = (~(size - 1) << 1) | (to - 1);
   n = ((imms >> 6) & 1) ^ 1;
   return (n << 22) | ((immr & 0x3f) << 16) | ((imms & 0x3f) << 10);
}

static inline void arm64_jump_code_fn(u_char **instp,u_char *target)
{
   arm64_jump_code(*instp,target);
}
#endif // ARM64_H
