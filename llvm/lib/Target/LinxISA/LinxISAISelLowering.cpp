//===-- LinxISAISelLowering.cpp - LinxISA DAG Lowering --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LinxISAISelLowering.h"
#include "LinxISA.h"
#include "LinxISABaseInfo.h"
#include "LinxISAMachineFunctionInfo.h"
#include "LinxISARegisterInfo.h"
#include "LinxISASubtarget.h"
#include "MCTargetDesc/LinxISAMCTargetDesc.h"
#include "llvm/CodeGen/CallingConvLower.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/SelectionDAG.h"
#include "llvm/CodeGen/TargetCallingConv.h"
#include "llvm/IR/Function.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Target/TargetMachine.h"
#include <limits>
#include <optional>

using namespace llvm;

static int64_t linxEncodeFpSrcType(EVT VT) {
  if (VT == MVT::f64)
    return 0; // fd
  if (VT == MVT::f32)
    return 1; // fs
  report_fatal_error("Linx: unsupported floating SrcType");
}

static int64_t linxEncodeCvtDstTypeFP(EVT VT) {
  if (VT == MVT::f64)
    return 0; // FP64
  if (VT == MVT::f32)
    return 1; // FP32
  report_fatal_error("Linx: unsupported FCVT destination FP type");
}

static int64_t linxEncodeCvtSrcTypeInt(EVT VT) {
  if (VT == MVT::i64)
    return 0; // *d
  if (VT == MVT::i32)
    return 1; // *w
  report_fatal_error("Linx: unsupported FCVT integer source type");
}

static int64_t linxEncodeCvtDstTypeSInt(EVT VT) {
  if (VT == MVT::i64)
    return 8; // s64
  if (VT == MVT::i32)
    return 9; // s32
  report_fatal_error("Linx: unsupported FCVT signed integer destination type");
}

static int64_t linxEncodeCvtDstTypeUInt(EVT VT) {
  if (VT == MVT::i64)
    return 0; // u64
  if (VT == MVT::i32)
    return 1; // u32
  report_fatal_error(
      "Linx: unsupported FCVT unsigned integer destination type");
}

//===----------------------------------------------------------------------===//
// Calling convention implementation.
//===----------------------------------------------------------------------===//

#include "LinxISAGenCallingConv.inc"

LinxISATargetLowering::LinxISATargetLowering(const TargetMachine &TM,
                                             const LinxISASubtarget &STI)
    : TargetLowering(TM, STI), STI(STI) {
  addRegisterClass(MVT::i64, &LinxISA::GPRRegClass);
  addRegisterClass(MVT::i32, &LinxISA::GPRRegClass);
  addRegisterClass(MVT::f64, &LinxISA::GPRRegClass);
  addRegisterClass(MVT::f32, &LinxISA::GPRRegClass);
  // Tile registers (TAU): model hardware tiles as an opaque vector payload.
  //
  // Bring-up rule: tile values are expected to be produced/consumed only by
  // Linx tile intrinsics (e.g. llvm.linx.tma.* / llvm.linx.cube.*). Keep all
  // generic vector ops expanded/custom so the backend does not accidentally
  // start treating <1024 x i32> as a general SIMD type. A small allowlist of
  // elementwise operations is lowered into VPAR decoupled blocks to support
  // normal C/C++ vector arithmetic on tiles during bring-up.
  addRegisterClass(MVT::v1024i32, &LinxISA::TILERegClass);

  // Bring-up: support elementwise add/sub on tile values (selected late into
  // decoupled VPAR blocks). Other generic vector ops remain expanded.
  setOperationAction(ISD::ADD, MVT::v1024i32, Legal);
  setOperationAction(ISD::SUB, MVT::v1024i32, Legal);

  computeRegisterProperties(STI.getRegisterInfo());
  setStackPointerRegisterToSaveRestore(LinxISA::R1);

  // LinxISA comparisons produce 0/1 values.
  setBooleanContents(ZeroOrOneBooleanContent);

  // Bring-up: expand i1 memory ops.
  //
  // Clang/LLVM may materialize boolean globals (e.g. `static bool`) as i1 in
  // memory. LinxISA does not support direct i1 load/store, so expand those
  // operations into byte accesses plus trunc/zext.
  setOperationAction(ISD::LOAD, MVT::i1, Expand);
  setOperationAction(ISD::STORE, MVT::i1, Expand);

  setOperationAction(ISD::BR, MVT::Other, Custom);
  setOperationAction(ISD::BRCOND, MVT::Other, Custom);
  setOperationAction(ISD::BR_CC, MVT::i64, Custom);
  setOperationAction(ISD::BR_CC, MVT::i32, Custom);
  // For floating-point conditionals, prefer expanding BR_CC into SETCC+BRCOND
  // and rely on our custom SETCC lowering to use FEQ/FLT/FGE.
  setOperationAction(ISD::BR_CC, MVT::f32, Expand);
  setOperationAction(ISD::BR_CC, MVT::f64, Expand);
  setOperationAction(ISD::BRIND, MVT::Other, Custom);

  // i1 is promoted to a register-sized integer before isel.
  setOperationAction(ISD::SETCC, MVT::i64, Custom);
  setOperationAction(ISD::SETCC, MVT::i32, Custom);

  // Bring-up: avoid generating jump tables.
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);
  setOperationAction(ISD::JumpTable, MVT::i64, Custom);
  setOperationAction(ISD::JumpTable, MVT::i32, Custom);

  // Lower atomic fences via libcalls (e.g. __sync_synchronize) for bring-up.
  setOperationAction(ISD::ATOMIC_FENCE, MVT::Other, Expand);

  setOperationAction(ISD::SIGN_EXTEND, MVT::i64, Custom);
  setOperationAction(ISD::ZERO_EXTEND, MVT::i64, Custom);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i64, Custom);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i32, Custom);

  // Bring-up: expand rotates to shifts + ors.
  setOperationAction(ISD::ROTL, MVT::i32, Expand);
  setOperationAction(ISD::ROTL, MVT::i64, Expand);
  setOperationAction(ISD::ROTR, MVT::i32, Expand);
  setOperationAction(ISD::ROTR, MVT::i64, Expand);

  // Bring-up: expand byte swaps (used by e.g. networking and entropy code).
  setOperationAction(ISD::BSWAP, MVT::i32, Expand);
  setOperationAction(ISD::BSWAP, MVT::i64, Expand);

  // Bring-up: expand multiply-high operations used by division-by-constant
  // lowering. LinxISA does not have a dedicated mulhi instruction yet.
  setOperationAction(ISD::MULHS, MVT::i32, Custom);
  setOperationAction(ISD::MULHU, MVT::i32, Custom);
  setOperationAction(ISD::MULHS, MVT::i64, Custom);
  setOperationAction(ISD::MULHU, MVT::i64, Custom);
  // Expand {u,s}mul_lohi into MUL + MUL{HU,HS} so generic code (e.g. div64)
  // doesn't leave ISD::UMUL_LOHI nodes for isel.
  setOperationAction(ISD::UMUL_LOHI, MVT::i32, Expand);
  setOperationAction(ISD::UMUL_LOHI, MVT::i64, Expand);
  setOperationAction(ISD::SMUL_LOHI, MVT::i32, Expand);
  setOperationAction(ISD::SMUL_LOHI, MVT::i64, Expand);

  // Bit-manipulation count intrinsics.
  setOperationAction(ISD::CTLZ, MVT::i32, Legal);
  setOperationAction(ISD::CTLZ, MVT::i64, Legal);
  setOperationAction(ISD::CTLZ_ZERO_UNDEF, MVT::i32, Legal);
  setOperationAction(ISD::CTLZ_ZERO_UNDEF, MVT::i64, Legal);
  setOperationAction(ISD::CTTZ, MVT::i32, Legal);
  setOperationAction(ISD::CTTZ, MVT::i64, Legal);
  setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::i32, Legal);
  setOperationAction(ISD::CTTZ_ZERO_UNDEF, MVT::i64, Legal);
  setOperationAction(ISD::CTPOP, MVT::i32, Legal);
  setOperationAction(ISD::CTPOP, MVT::i64, Legal);

  // Drop prefetches for now.
  setOperationAction(ISD::PREFETCH, MVT::Other, Expand);

  // Variadic argument lowering (va_start/va_arg/va_end).
  setOperationAction(ISD::VASTART, MVT::Other, Custom);
  setOperationAction({ISD::VAARG, ISD::VACOPY, ISD::VAEND}, MVT::Other, Expand);

  // Dynamic stack allocations (VLAs/alloca with runtime size).
  //
  // Without explicit legalization, SelectionDAG may leave
  // ISD::DYNAMIC_STACKALLOC in the DAG and instruction selection crashes on
  // functions such as musl's getcwd() that use runtime-sized stack objects.
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i64, Expand);
  setOperationAction(ISD::DYNAMIC_STACKALLOC, MVT::i32, Expand);
  // Likewise expand stack save/restore until dedicated lowering exists.
  // These nodes are keyed on the chain type (MVT::Other), not pointer width.
  // musl locale code can otherwise fail with "Cannot select ... stacksave".
  setOperationAction(ISD::STACKSAVE, MVT::Other, Expand);
  setOperationAction(ISD::STACKRESTORE, MVT::Other, Expand);

  // Bring-up: avoid introducing target-specific select/cmov patterns.
  setOperationAction(ISD::SELECT, MVT::i32, Custom);
  setOperationAction(ISD::SELECT, MVT::i64, Custom);
  setOperationAction(ISD::SELECT, MVT::f32, Custom);
  setOperationAction(ISD::SELECT, MVT::f64, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::i32, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::i64, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::f32, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::f64, Expand);

  // GlobalAddress must be custom-lowered to PC-relative addressing.
  setOperationAction(ISD::GlobalAddress, MVT::i64, Custom);
  setOperationAction(ISD::GlobalAddress, MVT::i32, Custom);
  setOperationAction(ISD::BlockAddress, MVT::i64, Custom);
  setOperationAction(ISD::BlockAddress, MVT::i32, Custom);
  // Bring-up: model TLS addresses as regular symbol addresses. This is enough
  // to build single-threaded runtimes and will be replaced by real TLS
  // lowering once the Linx ELF TLS ABI is finalized.
  setOperationAction(ISD::GlobalTLSAddress, MVT::i64, Custom);
  setOperationAction(ISD::GlobalTLSAddress, MVT::i32, Custom);
  setOperationAction(ISD::ConstantPool, MVT::i64, Custom);
  setOperationAction(ISD::ConstantPool, MVT::i32, Custom);

  //===----------------------------------------------------------------------===//
  // Floating Point Operations
  //===----------------------------------------------------------------------===//
  // LinxISA implements scalar floating-point arithmetic using the existing
  // reg5 register file (no separate architectural FPR file). For bring-up we
  // enable f32/f64 hard-float operations and keep everything else as libcalls.

  // f32/f64 arithmetic.
  setOperationAction(ISD::FADD, MVT::f32, Custom);
  setOperationAction(ISD::FSUB, MVT::f32, Custom);
  setOperationAction(ISD::FMUL, MVT::f32, Custom);
  setOperationAction(ISD::FDIV, MVT::f32, Custom);
  setOperationAction(ISD::FNEG, MVT::f32, Custom);
  setOperationAction(ISD::FABS, MVT::f32, Custom);
  setOperationAction(ISD::SETCC, MVT::f32, Custom);

  setOperationAction(ISD::FADD, MVT::f64, Custom);
  setOperationAction(ISD::FSUB, MVT::f64, Custom);
  setOperationAction(ISD::FMUL, MVT::f64, Custom);
  setOperationAction(ISD::FDIV, MVT::f64, Custom);
  setOperationAction(ISD::FNEG, MVT::f64, Custom);
  setOperationAction(ISD::FABS, MVT::f64, Custom);
  setOperationAction(ISD::SETCC, MVT::f64, Custom);

  // Leave more complex ops as libcalls for now.
  setOperationAction(ISD::FREM, MVT::f32, Expand);
  setOperationAction(ISD::FSQRT, MVT::f32, Expand);
  setOperationAction(ISD::FCOPYSIGN, MVT::f32, Expand);
  setOperationAction(ISD::FEXP2, MVT::f32, Expand);
  setOperationAction(ISD::FLOG2, MVT::f32, Expand);

  setOperationAction(ISD::FREM, MVT::f64, Expand);
  setOperationAction(ISD::FSQRT, MVT::f64, Expand);
  setOperationAction(ISD::FCOPYSIGN, MVT::f64, Expand);
  setOperationAction(ISD::FEXP2, MVT::f64, Expand);
  setOperationAction(ISD::FLOG2, MVT::f64, Expand);

  // Conversions (action keyed by the source operand type).
  setOperationAction(ISD::FP_TO_SINT, MVT::i32, Custom);
  setOperationAction(ISD::FP_TO_UINT, MVT::i32, Custom);
  setOperationAction(ISD::FP_TO_SINT, MVT::i64, Custom);
  setOperationAction(ISD::FP_TO_UINT, MVT::i64, Custom);

  setOperationAction(ISD::SINT_TO_FP, MVT::i32, Custom);
  setOperationAction(ISD::UINT_TO_FP, MVT::i32, Custom);
  setOperationAction(ISD::SINT_TO_FP, MVT::i64, Custom);
  setOperationAction(ISD::UINT_TO_FP, MVT::i64, Custom);

  // FP_ROUND/FP_EXTEND are keyed by the result type.
  setOperationAction(ISD::FP_ROUND, MVT::f32, Custom);
  setOperationAction(ISD::FP_EXTEND, MVT::f64, Custom);

  // FMA (fused multiply-add) stays as a libcall until explicitly enabled.
  setOperationAction(ISD::FMA, MVT::f32, Expand);
  setOperationAction(ISD::FMA, MVT::f64, Expand);

  // Function alignments.
  setMinFunctionAlignment(Align(2));
  setPrefFunctionAlignment(Align(2));
}

SDValue LinxISATargetLowering::LowerOperation(SDValue Op,
                                             SelectionDAG &DAG) const {
  switch (Op.getOpcode()) {
  case ISD::BR:
    return LowerBR(Op, DAG);
  case ISD::BRCOND:
    return LowerBRCOND(Op, DAG);
  case ISD::BR_CC:
    return LowerBR_CC(Op, DAG);
  case ISD::BRIND:
    return LowerBRIND(Op, DAG);
  case ISD::JumpTable:
    return LowerJumpTable(Op, DAG);
  case ISD::MULHS:
    return LowerMULHS(Op, DAG);
  case ISD::MULHU:
    return LowerMULHU(Op, DAG);
  case ISD::SIGN_EXTEND:
    return LowerSIGN_EXTEND(Op, DAG);
  case ISD::ZERO_EXTEND:
    return LowerZERO_EXTEND(Op, DAG);
  case ISD::SIGN_EXTEND_INREG:
    return LowerSIGN_EXTEND_INREG(Op, DAG);
  case ISD::SELECT:
    return LowerSELECT(Op, DAG);
  case ISD::SETCC:
    return LowerSETCC(Op, DAG);
  case ISD::FADD:
    return LowerFPBinOp(Op, DAG, LinxISA::FADDrr);
  case ISD::FSUB:
    return LowerFPBinOp(Op, DAG, LinxISA::FSUBrr);
  case ISD::FMUL:
    return LowerFPBinOp(Op, DAG, LinxISA::FMULrr);
  case ISD::FDIV:
    return LowerFPBinOp(Op, DAG, LinxISA::FDIVrr);
  case ISD::FABS:
    return LowerFPUnOp(Op, DAG, LinxISA::FABSrr);
  case ISD::FNEG: {
    SDLoc DL(Op);
    EVT VT = Op.getValueType();
    SDValue Zero = DAG.getConstantFP(0.0, DL, VT);
    return DAG.getNode(ISD::FSUB, DL, VT, Zero, Op.getOperand(0));
  }
  case ISD::FP_TO_SINT:
    return LowerFP_TO_SINT(Op, DAG);
  case ISD::FP_TO_UINT:
    return LowerFP_TO_UINT(Op, DAG);
  case ISD::SINT_TO_FP:
    return LowerSINT_TO_FP(Op, DAG);
  case ISD::UINT_TO_FP:
    return LowerUINT_TO_FP(Op, DAG);
  case ISD::FP_ROUND:
    return LowerFP_ROUND(Op, DAG);
  case ISD::FP_EXTEND:
    return LowerFP_EXTEND(Op, DAG);
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
  case ISD::BlockAddress:
    return LowerBlockAddress(Op, DAG);
  case ISD::GlobalTLSAddress:
    return LowerGlobalTLSAddress(Op, DAG);
  case ISD::ConstantPool:
    return LowerConstantPool(Op, DAG);
  case ISD::VASTART:
    return LowerVASTART(Op, DAG);
  default:
    return SDValue();
  }
}

SDValue LinxISATargetLowering::LowerBR(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);

  // Operand order from ISD::BR:
  //   (Chain, DestBB)
  SDValue Chain = Op.getOperand(0);
  SDValue Dest = Op.getOperand(1);

  SDValue Ops[] = {Dest, Chain};
  return SDValue(DAG.getMachineNode(LinxISA::JUMP, DL, MVT::Other, Ops), 0);
}

SDValue LinxISATargetLowering::LowerBRIND(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);

  // Operand order from ISD::BRIND:
  //   (Chain, Target)
  SDValue Chain = Op.getOperand(0);
  SDValue Target = Op.getOperand(1);

  if (Target.getValueType() != MVT::i64)
    Target = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, Target);

  SDValue Ops[] = {Target, Chain};
  return SDValue(DAG.getMachineNode(LinxISA::JR, DL, MVT::Other, Ops), 0);
}

SDValue LinxISATargetLowering::LowerJumpTable(SDValue Op,
                                             SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT Ty = Op.getValueType();

  auto *JT = cast<JumpTableSDNode>(Op);

  // Use the same PC-relative addressing sequence as globals:
  //   ADDTPC (page base) + ADDI/ADDIW (low 12 bits).
  SDValue JTI = DAG.getTargetJumpTable(JT->getIndex(), Ty);

  SDValue Page = SDValue(DAG.getMachineNode(LinxISA::ADDTPC, DL, Ty, JTI), 0);
  const unsigned AddOpc =
      (Ty == MVT::i32) ? LinxISA::ADDIWri : LinxISA::ADDIri;
  return SDValue(DAG.getMachineNode(AddOpc, DL, Ty, Page, JTI), 0);
}

static SDValue lowerMulHU64(SDLoc DL, SelectionDAG &DAG, SDValue A, SDValue B) {
  const EVT VT = MVT::i64;

  auto getLo32 = [&](SDValue V) -> SDValue {
    SDValue Lo32 = DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, V);
    return DAG.getNode(ISD::ZERO_EXTEND, DL, VT, Lo32);
  };
  auto getHi32 = [&](SDValue V) -> SDValue {
    SDValue Sh = DAG.getNode(ISD::SRL, DL, VT, V,
                             DAG.getConstant(32, DL, VT));
    SDValue Hi32 = DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Sh);
    return DAG.getNode(ISD::ZERO_EXTEND, DL, VT, Hi32);
  };

  SDValue ALo = getLo32(A);
  SDValue AHi = getHi32(A);
  SDValue BLo = getLo32(B);
  SDValue BHi = getHi32(B);

  // 64x64 -> high 64 bits via 32-bit limbs.
  // See Hacker's Delight, 2nd ed., section 8-3 (multiply).
  SDValue P00 = DAG.getNode(ISD::MUL, DL, VT, ALo, BLo);
  SDValue P01 = DAG.getNode(ISD::MUL, DL, VT, ALo, BHi);
  SDValue P10 = DAG.getNode(ISD::MUL, DL, VT, AHi, BLo);
  SDValue P11 = DAG.getNode(ISD::MUL, DL, VT, AHi, BHi);

  SDValue Mid = DAG.getNode(ISD::ADD, DL, VT, P01, P10);
  SDValue MidCarry = DAG.getSetCC(DL, VT, Mid, P01, ISD::SETULT); // carry out

  SDValue MidHi = DAG.getNode(ISD::SRL, DL, VT, Mid, DAG.getConstant(32, DL, VT));
  SDValue MidCarrySh =
      DAG.getNode(ISD::SHL, DL, VT, MidCarry, DAG.getConstant(32, DL, VT));
  MidHi = DAG.getNode(ISD::ADD, DL, VT, MidHi, MidCarrySh);

  SDValue LowAdd =
      DAG.getNode(ISD::ADD, DL, VT, P00,
                  DAG.getNode(ISD::SHL, DL, VT, Mid, DAG.getConstant(32, DL, VT)));
  SDValue LowCarry = DAG.getSetCC(DL, VT, LowAdd, P00, ISD::SETULT);

  SDValue High = DAG.getNode(ISD::ADD, DL, VT, P11, MidHi);
  return DAG.getNode(ISD::ADD, DL, VT, High, LowCarry);
}

SDValue LinxISATargetLowering::LowerMULHU(SDValue Op,
                                         SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue A = Op.getOperand(0);
  SDValue B = Op.getOperand(1);
  EVT VT = Op.getValueType();

  if (VT == MVT::i32) {
    SDValue A64 = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, A);
    SDValue B64 = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, B);
    SDValue Prod = DAG.getNode(ISD::MUL, DL, MVT::i64, A64, B64);
    SDValue Hi = DAG.getNode(ISD::SRL, DL, MVT::i64, Prod,
                             DAG.getConstant(32, DL, MVT::i64));
    return DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Hi);
  }

  if (VT == MVT::i64)
    return lowerMulHU64(DL, DAG, A, B);

  return SDValue();
}

SDValue LinxISATargetLowering::LowerMULHS(SDValue Op,
                                         SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue A = Op.getOperand(0);
  SDValue B = Op.getOperand(1);
  EVT VT = Op.getValueType();

  if (VT == MVT::i32) {
    SDValue A64 = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i64, A);
    SDValue B64 = DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i64, B);
    SDValue Prod = DAG.getNode(ISD::MUL, DL, MVT::i64, A64, B64);
    SDValue Hi = DAG.getNode(ISD::SRA, DL, MVT::i64, Prod,
                             DAG.getConstant(32, DL, MVT::i64));
    return DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Hi);
  }

  if (VT == MVT::i64) {
    // mulhs(a, b) = mulhu(a, b) - (a < 0 ? b : 0) - (b < 0 ? a : 0)
    SDValue HighUU = lowerMulHU64(DL, DAG, A, B);
    SDValue Zero = DAG.getRegister(LinxISA::R0, MVT::i64);
    SDValue ANeg = DAG.getSetCC(DL, MVT::i64, A, Zero, ISD::SETLT);
    SDValue BNeg = DAG.getSetCC(DL, MVT::i64, B, Zero, ISD::SETLT);
    SDValue AdjA = DAG.getNode(ISD::SELECT, DL, MVT::i64, ANeg, B, Zero);
    SDValue AdjB = DAG.getNode(ISD::SELECT, DL, MVT::i64, BNeg, A, Zero);
    SDValue Tmp = DAG.getNode(ISD::SUB, DL, MVT::i64, HighUU, AdjA);
    return DAG.getNode(ISD::SUB, DL, MVT::i64, Tmp, AdjB);
  }

  return SDValue();
}

SDValue LinxISATargetLowering::LowerBlockAddress(SDValue Op,
                                                 SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT Ty = Op.getValueType();
  auto *BA = cast<BlockAddressSDNode>(Op);

  SDValue TBA = DAG.getTargetBlockAddress(BA->getBlockAddress(), Ty,
                                         BA->getOffset());
  SDValue Page = SDValue(DAG.getMachineNode(LinxISA::ADDTPC, DL, Ty, TBA), 0);
  const unsigned AddOpc =
      (Ty == MVT::i32) ? LinxISA::ADDIWri : LinxISA::ADDIri;
  return SDValue(DAG.getMachineNode(AddOpc, DL, Ty, Page, TBA), 0);
}

SDValue LinxISATargetLowering::LowerBRCOND(SDValue Op,
                                           SelectionDAG &DAG) const {
  SDLoc DL(Op);

  // Operand order from ISD::BRCOND:
  //   (Chain, Cond, DestBB)
  SDValue Chain = Op.getOperand(0);
  SDValue Cond = Op.getOperand(1);
  SDValue Dest = Op.getOperand(2);

  // Branch if Cond != 0.
  if (Cond.getValueType() != MVT::i64) {
    Cond = DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, Cond);
  }
  SDValue Zero = DAG.getRegister(LinxISA::R0, MVT::i64);
  SDValue Ops[] = {Cond, Zero, Dest, Chain};
  return SDValue(DAG.getMachineNode(LinxISA::BNE, DL, MVT::Other, Ops), 0);
}

SDValue LinxISATargetLowering::LowerBR_CC(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);

  // Operand order from ISD::BR_CC:
  //   (Chain, CondCode, LHS, RHS, DestBB)
  SDValue Chain = Op.getOperand(0);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(1))->get();
  SDValue LHS = Op.getOperand(2);
  SDValue RHS = Op.getOperand(3);
  SDValue Dest = Op.getOperand(4);

  auto ExtendToI64 = [&](SDValue V) -> SDValue {
    EVT VT = V.getValueType();
    if (VT == MVT::i64)
      return V;
    if (VT == MVT::i32) {
      switch (CC) {
      case ISD::SETULT:
      case ISD::SETULE:
      case ISD::SETUGT:
      case ISD::SETUGE:
        return DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, V);
      default:
        return DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i64, V);
      }
    }
    if (VT == MVT::i1)
      return DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, V);
    return V;
  };

  LHS = ExtendToI64(LHS);
  RHS = ExtendToI64(RHS);

  bool SwapOps = false;
  unsigned BrOpc = 0;
  switch (CC) {
  case ISD::SETEQ:
    BrOpc = LinxISA::BEQ;
    break;
  case ISD::SETNE:
    BrOpc = LinxISA::BNE;
    break;
  case ISD::SETLT:
    BrOpc = LinxISA::BLT;
    break;
  case ISD::SETLE:
    SwapOps = true;
    BrOpc = LinxISA::BGE;
    break;
  case ISD::SETGT:
    SwapOps = true;
    BrOpc = LinxISA::BLT;
    break;
  case ISD::SETGE:
    BrOpc = LinxISA::BGE;
    break;
  case ISD::SETULT:
    BrOpc = LinxISA::BLTU;
    break;
  case ISD::SETULE:
    SwapOps = true;
    BrOpc = LinxISA::BGEU;
    break;
  case ISD::SETUGT:
    SwapOps = true;
    BrOpc = LinxISA::BLTU;
    break;
  case ISD::SETUGE:
    BrOpc = LinxISA::BGEU;
    break;
  default:
    report_fatal_error("Linx: unsupported BR_CC condition");
  }

  if (SwapOps)
    std::swap(LHS, RHS);

  SDValue Ops[] = {LHS, RHS, Dest, Chain};
  return SDValue(DAG.getMachineNode(BrOpc, DL, MVT::Other, Ops), 0);
}

static SDValue buildExtendInReg(SDValue Val, unsigned FromBits, bool IsSigned,
                                const SDLoc &DL, SelectionDAG &DAG) {
  EVT VT = Val.getValueType();
  if (VT != MVT::i64)
    report_fatal_error("Linx: Extend-in-reg expects i64 value");
  if (FromBits >= 64)
    return Val;

  const unsigned Shift = 64 - FromBits;
  if (Shift == 0)
    return Val;

  // Avoid the canonical (shl, sra/srl) combine pattern: post-legalize DAG
  // combines may fold it back into SIGN_EXTEND_INREG/ZERO_EXTEND, which then
  // survives into isel and becomes unselectable.
  SDValue Zero = DAG.getRegister(LinxISA::R0, MVT::i64);
  SDValue ShImm = DAG.getTargetConstant(Shift, DL, MVT::i64);
  SDValue ShAmt =
      SDValue(DAG.getMachineNode(LinxISA::ADDIri, DL, MVT::i64, Zero, ShImm), 0);

  SDValue Shl =
      SDValue(DAG.getMachineNode(LinxISA::SLLrr, DL, MVT::i64, Val, ShAmt), 0);

  unsigned ShrOpc = IsSigned ? LinxISA::SRArr : LinxISA::SRLrr;
  return SDValue(DAG.getMachineNode(ShrOpc, DL, MVT::i64, Shl, ShAmt), 0);
}

SDValue LinxISATargetLowering::LowerSIGN_EXTEND(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Val = Op.getOperand(0);
  EVT FromVT = Val.getValueType();
  if (Op.getValueType() != MVT::i64)
    return SDValue();

  unsigned FromBits = FromVT.getScalarSizeInBits();

  // Common case: sign-extend an i32 value to i64. Use a 32-bit ALU op which
  // writes a sign-extended result into a GPR (ADDW with zero).
  if (FromVT == MVT::i32) {
    SDValue Wide = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, Val);
    SDValue Zero = DAG.getRegister(LinxISA::R0, MVT::i64);
    return SDValue(DAG.getMachineNode(LinxISA::ADDWrr, DL, MVT::i64, Wide, Zero),
                   0);
  }

  SDValue Wide = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, Val);
  return buildExtendInReg(Wide, FromBits, /*IsSigned=*/true, DL, DAG);
}

SDValue LinxISATargetLowering::LowerZERO_EXTEND(SDValue Op,
                                                SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Val = Op.getOperand(0);
  EVT FromVT = Val.getValueType();
  if (Op.getValueType() != MVT::i64)
    return SDValue();

  unsigned FromBits = FromVT.getScalarSizeInBits();
  if (FromVT == MVT::i32) {
    // Avoid the canonical (shl x, 32) / (srl x, 32) zero-extend pattern since
    // it can get re-combined into ZERO_EXTEND again during post-legalize DAG
    // combines, causing non-termination. Force the shift amount into a register
    // and use reg-reg shifts.
    SDValue Wide = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, Val);
    SDValue Zero = DAG.getRegister(LinxISA::R0, MVT::i64);
    SDValue ShImm = DAG.getTargetConstant(32, DL, MVT::i64);
    SDValue ShAmt =
        SDValue(DAG.getMachineNode(LinxISA::ADDIri, DL, MVT::i64, Zero, ShImm),
                0);

    SDValue Shl =
        SDValue(DAG.getMachineNode(LinxISA::SLLrr, DL, MVT::i64, Wide, ShAmt),
                0);
    return SDValue(
        DAG.getMachineNode(LinxISA::SRLrr, DL, MVT::i64, Shl, ShAmt), 0);
  }

  SDValue Wide = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, Val);
  return buildExtendInReg(Wide, FromBits, /*IsSigned=*/false, DL, DAG);
}

SDValue LinxISATargetLowering::LowerSIGN_EXTEND_INREG(
    SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue Val = Op.getOperand(0);
  EVT FromVT = cast<VTSDNode>(Op.getOperand(1))->getVT();
  unsigned FromBits = FromVT.getScalarSizeInBits();

  if (Op.getValueType() == MVT::i64) {
    // Fast-path: sign-extend the low 32 bits in a GPR.
    if (FromBits == 32) {
      SDValue Zero = DAG.getRegister(LinxISA::R0, MVT::i64);
      return SDValue(
          DAG.getMachineNode(LinxISA::ADDWrr, DL, MVT::i64, Val, Zero), 0);
    }
    return buildExtendInReg(Val, FromBits, /*IsSigned=*/true, DL, DAG);
  }

  if (Op.getValueType() == MVT::i32) {
    // Perform the operation in an i64 GPR and truncate back to i32. LinxISA
    // instruction patterns are defined in terms of i64 operations.
    SDValue Wide = DAG.getNode(ISD::ANY_EXTEND, DL, MVT::i64, Val);
    SDValue Ext = buildExtendInReg(Wide, FromBits, /*IsSigned=*/true, DL, DAG);
    return DAG.getNode(ISD::TRUNCATE, DL, MVT::i32, Ext);
  }

  return SDValue();
}

SDValue LinxISATargetLowering::LowerSELECT(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();

  if (VT != MVT::i32 && VT != MVT::i64 && VT != MVT::f32 && VT != MVT::f64)
    return SDValue();

  SDValue Cond = Op.getOperand(0);
  SDValue TrueVal = Op.getOperand(1);
  SDValue FalseVal = Op.getOperand(2);

  // Lower to the ISA conditional select:
  //   rd = csel pred, true, false
  // with pred as a 0/1 integer value.
  const EVT PredVT = (VT.getScalarSizeInBits() == 32) ? MVT::i32 : MVT::i64;
  SDValue Pred = Cond;
  if (Pred.getValueType() != PredVT) {
    unsigned PredBits = Pred.getValueType().getScalarSizeInBits();
    unsigned WantBits = PredVT.getScalarSizeInBits();
    if (PredBits < WantBits)
      Pred = DAG.getNode(ISD::ZERO_EXTEND, DL, PredVT, Pred);
    else
      Pred = DAG.getNode(ISD::TRUNCATE, DL, PredVT, Pred);
  }

  return SDValue(
      DAG.getMachineNode(LinxISA::CSELrrr, DL, VT, Pred, TrueVal, FalseVal), 0);
}

SDValue LinxISATargetLowering::LowerSETCC(SDValue Op,
                                         SelectionDAG &DAG) const {
  SDLoc DL(Op);
  SDValue LHS = Op.getOperand(0);
  SDValue RHS = Op.getOperand(1);
  ISD::CondCode CC = cast<CondCodeSDNode>(Op.getOperand(2))->get();

  // Hard-float compares: lower scalar f32/f64 SETCC to FEQ/FLT/FGE.
  EVT CmpVT = LHS.getValueType();
  if (CmpVT == MVT::f32 || CmpVT == MVT::f64) {
    const int64_t SrcType = linxEncodeFpSrcType(CmpVT);
    SDValue SrcTypeImm = DAG.getTargetConstant(SrcType, DL, MVT::i64);
    const EVT ResVT = Op.getValueType();

    auto emitCmp = [&](unsigned Opc, SDValue A, SDValue B) -> SDValue {
      return SDValue(
          DAG.getMachineNode(Opc, DL, ResVT, A, B, SrcTypeImm), 0);
    };

    auto invertBool = [&](SDValue V) -> SDValue {
      const unsigned XorOpc =
          (ResVT == MVT::i32) ? LinxISA::XORIWri : LinxISA::XORIri;
      SDValue One =
          DAG.getTargetConstant(1, DL, (ResVT == MVT::i32) ? MVT::i32 : MVT::i64);
      return SDValue(DAG.getMachineNode(XorOpc, DL, ResVT, V, One), 0);
    };

    bool SwapOps = false;
    bool Invert = false;
    unsigned CmpOpc = 0;

    switch (CC) {
    case ISD::SETFALSE:
    case ISD::SETFALSE2:
      return DAG.getConstant(0, DL, ResVT);
    case ISD::SETTRUE:
    case ISD::SETTRUE2:
      return DAG.getConstant(1, DL, ResVT);
    case ISD::SETEQ:
    case ISD::SETOEQ:
      CmpOpc = LinxISA::FEQrr;
      break;
    case ISD::SETLT:
    case ISD::SETOLT:
      CmpOpc = LinxISA::FLTrr;
      break;
    case ISD::SETGE:
    case ISD::SETOGE:
      CmpOpc = LinxISA::FGErr;
      break;
    case ISD::SETLE:
    case ISD::SETOLE:
      SwapOps = true; // a <= b  <=>  b >= a
      CmpOpc = LinxISA::FGErr;
      break;
    case ISD::SETGT:
    case ISD::SETOGT:
      SwapOps = true; // a > b  <=>  b < a
      CmpOpc = LinxISA::FLTrr;
      break;
    case ISD::SETULE:
      // unordered-or-(a <= b)  <=>  !(ordered(a > b))
      // ordered(a > b) <=> ordered(b < a)
      SwapOps = true;
      CmpOpc = LinxISA::FLTrr;
      Invert = true;
      break;
    case ISD::SETULT:
      // unordered-or-(a < b)  <=>  !(ordered(a >= b))
      CmpOpc = LinxISA::FGErr;
      Invert = true;
      break;
    case ISD::SETUGT:
      // unordered-or-(a > b)  <=>  !(ordered(a <= b))
      // ordered(a <= b) <=> ordered(b >= a)
      SwapOps = true;
      CmpOpc = LinxISA::FGErr;
      Invert = true;
      break;
    case ISD::SETUGE:
      // unordered-or-(a >= b)  <=>  !(ordered(a < b))
      CmpOpc = LinxISA::FLTrr;
      Invert = true;
      break;
    case ISD::SETONE: {
      // Ordered and not equal: (a < b) || (a > b).
      SDValue LT = emitCmp(LinxISA::FLTrr, LHS, RHS);
      SDValue GT = emitCmp(LinxISA::FLTrr, RHS, LHS);
      return DAG.getNode(ISD::OR, DL, ResVT, LT, GT);
    }
    case ISD::SETUEQ: {
      // Unordered or equal: !(ordered and not equal).
      SDValue LT = emitCmp(LinxISA::FLTrr, LHS, RHS);
      SDValue GT = emitCmp(LinxISA::FLTrr, RHS, LHS);
      SDValue ONE = DAG.getNode(ISD::OR, DL, ResVT, LT, GT);
      return invertBool(ONE);
    }
    case ISD::SETO: {
      // Ordered: true iff neither operand is NaN.
      SDValue AOrd = emitCmp(LinxISA::FEQrr, LHS, LHS);
      SDValue BOrd = emitCmp(LinxISA::FEQrr, RHS, RHS);
      return DAG.getNode(ISD::AND, DL, ResVT, AOrd, BOrd);
    }
    case ISD::SETUO: {
      // Unordered: true iff either operand is NaN.
      SDValue AOrd = emitCmp(LinxISA::FEQrr, LHS, LHS);
      SDValue BOrd = emitCmp(LinxISA::FEQrr, RHS, RHS);
      SDValue Ord = DAG.getNode(ISD::AND, DL, ResVT, AOrd, BOrd);
      return invertBool(Ord);
    }
    case ISD::SETNE:
    case ISD::SETUNE:
      // C `!=` is unordered-or-not-equal; implement as !(a == b).
      CmpOpc = LinxISA::FEQrr;
      Invert = true;
      break;
    default:
      report_fatal_error("Linx: unsupported floating-point SETCC condition");
    }

    if (SwapOps)
      std::swap(LHS, RHS);

    SDValue Cmp = emitCmp(CmpOpc, LHS, RHS);

    if (!Invert)
      return Cmp;

    return invertBool(Cmp);
  }

  auto ExtendToI64 = [&](SDValue V) -> SDValue {
    EVT VT = V.getValueType();
    if (VT == MVT::i64)
      return V;
    if (VT == MVT::i32) {
      switch (CC) {
      case ISD::SETULT:
      case ISD::SETULE:
      case ISD::SETUGT:
      case ISD::SETUGE:
        return DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, V);
      default:
        return DAG.getNode(ISD::SIGN_EXTEND, DL, MVT::i64, V);
      }
    }
    if (VT == MVT::i1)
      return DAG.getNode(ISD::ZERO_EXTEND, DL, MVT::i64, V);
    return V;
  };

  LHS = ExtendToI64(LHS);
  RHS = ExtendToI64(RHS);

  const EVT ResVT = Op.getValueType();

  auto getImmS = [&](SDValue V) -> std::optional<int64_t> {
    if (auto *C = dyn_cast<ConstantSDNode>(V))
      return C->getSExtValue();
    return std::nullopt;
  };
  auto getImmU = [&](SDValue V) -> std::optional<uint64_t> {
    if (auto *C = dyn_cast<ConstantSDNode>(V))
      return C->getZExtValue();
    return std::nullopt;
  };

  auto emitRR = [&](unsigned Opc, SDValue A, SDValue B) -> SDValue {
    return SDValue(DAG.getMachineNode(Opc, DL, ResVT, A, B), 0);
  };
  auto emitRI = [&](unsigned Opc, SDValue A, int64_t Imm) -> SDValue {
    SDValue ImmOp = DAG.getTargetConstant(Imm, DL, MVT::i64);
    return SDValue(DAG.getMachineNode(Opc, DL, ResVT, A, ImmOp), 0);
  };
  auto emitRUI = [&](unsigned Opc, SDValue A, uint64_t Imm) -> SDValue {
    SDValue ImmOp = DAG.getTargetConstant(static_cast<int64_t>(Imm), DL, MVT::i64);
    return SDValue(DAG.getMachineNode(Opc, DL, ResVT, A, ImmOp), 0);
  };

  switch (CC) {
  case ISD::SETEQ: {
    if (auto Imm = getImmS(RHS); Imm && isInt<12>(*Imm))
      return emitRI(LinxISA::CMPEQI, LHS, *Imm);
    if (auto Imm = getImmS(LHS); Imm && isInt<12>(*Imm))
      return emitRI(LinxISA::CMPEQI, RHS, *Imm);
    return emitRR(LinxISA::CMPEQ, LHS, RHS);
  }
  case ISD::SETNE: {
    // Peephole: icmp ne (and/or x, y), 0  ==>  cmp.{and,or} x, y
    //           icmp ne (and/or x, C), 0  ==>  cmp.{and,or}i x, C
    //
    // These avoid materializing the intermediate AND/OR result just to compare
    // against zero, and match the ISA `CMP.AND*`/`CMP.OR*` helpers.
    auto tryEmitLogicCmp = [&]() -> SDValue {
      auto stripExt = [&](SDValue V) -> SDValue {
        while (true) {
          unsigned Op = V.getOpcode();
          if (Op == ISD::SIGN_EXTEND || Op == ISD::ZERO_EXTEND ||
              Op == ISD::ANY_EXTEND) {
            V = V.getOperand(0);
            continue;
          }
          break;
        }
        return V;
      };
      auto isZero = [&](SDValue V) -> bool {
        V = stripExt(V);
        if (auto *C = dyn_cast<ConstantSDNode>(V))
          return C->isZero();
        return false;
      };
      auto matchLogic = [&](SDValue V, unsigned &OutOpcRR,
                            unsigned &OutOpcRI32, unsigned &OutOpcRI48,
                            SDValue &A, SDValue &BOrImm, bool &IsImm) -> bool {
        if (V.getOpcode() != ISD::AND && V.getOpcode() != ISD::OR)
          return false;

        const bool IsAnd = (V.getOpcode() == ISD::AND);
        OutOpcRR = IsAnd ? LinxISA::CMPAND : LinxISA::CMPOR;
        OutOpcRI32 = IsAnd ? LinxISA::CMPANDI : LinxISA::CMPORI;
        OutOpcRI48 = IsAnd ? LinxISA::HLCMPANDI : LinxISA::HLCMPORI;

        SDValue X = V.getOperand(0);
        SDValue Y = V.getOperand(1);

        // Prefer immediate forms when possible.
        if (auto *C = dyn_cast<ConstantSDNode>(Y)) {
          int64_t Imm = C->getSExtValue();
          IsImm = true;
          A = X;
          BOrImm = DAG.getTargetConstant(Imm, DL, MVT::i64);
          return true;
        }
        if (auto *C = dyn_cast<ConstantSDNode>(X)) {
          int64_t Imm = C->getSExtValue();
          IsImm = true;
          A = Y;
          BOrImm = DAG.getTargetConstant(Imm, DL, MVT::i64);
          return true;
        }

        IsImm = false;
        A = X;
        BOrImm = Y;
        return true;
      };

      SDValue Logic = SDValue();
      if (isZero(RHS))
        Logic = LHS;
      else if (isZero(LHS))
        Logic = RHS;
      else
        return SDValue();

      // Nonzero tests are invariant under extension that preserves the low
      // bits of the boolean source expression. Look through extends so we can
      // still match (sext (and ...)) patterns after type legalization.
      Logic = stripExt(Logic);

      unsigned OpcRR = 0, OpcRI32 = 0, OpcRI48 = 0;
      SDValue A, BOrImm;
      bool IsImm = false;
      if (!matchLogic(Logic, OpcRR, OpcRI32, OpcRI48, A, BOrImm, IsImm))
        return SDValue();

      if (!IsImm)
        return emitRR(OpcRR, A, BOrImm);

      // For immediate forms, choose 32-bit or 48-bit encoding based on range.
      int64_t Imm = cast<ConstantSDNode>(BOrImm)->getSExtValue();
      if (isInt<12>(Imm))
        return emitRI(OpcRI32, A, Imm);
      if (isInt<24>(Imm))
        return SDValue(DAG.getMachineNode(OpcRI48, DL, ResVT, A,
                                         DAG.getTargetConstant(Imm, DL, MVT::i64)),
                       0);
      return SDValue();
    };

    if (SDValue V = tryEmitLogicCmp())
      return V;

    if (auto Imm = getImmS(RHS); Imm && isInt<12>(*Imm))
      return emitRI(LinxISA::CMPNEI, LHS, *Imm);
    if (auto Imm = getImmS(LHS); Imm && isInt<12>(*Imm))
      return emitRI(LinxISA::CMPNEI, RHS, *Imm);
    return emitRR(LinxISA::CMPNE, LHS, RHS);
  }
  case ISD::SETLT: {
    if (auto Imm = getImmS(RHS); Imm && isInt<12>(*Imm))
      return emitRI(LinxISA::CMPLTI, LHS, *Imm);
    if (auto Imm = getImmS(LHS);
        Imm && *Imm != std::numeric_limits<int64_t>::max() &&
        isInt<12>(*Imm + 1))
      return emitRI(LinxISA::CMPGEI, RHS, *Imm + 1); // C < x  <=>  x >= C+1
    return emitRR(LinxISA::CMPLT, LHS, RHS);
  }
  case ISD::SETLE: {
    if (auto Imm = getImmS(RHS);
        Imm && *Imm != std::numeric_limits<int64_t>::max() &&
        isInt<12>(*Imm + 1))
      return emitRI(LinxISA::CMPLTI, LHS, *Imm + 1); // x <= C  <=>  x < C+1
    if (auto Imm = getImmS(LHS); Imm && isInt<12>(*Imm))
      return emitRI(LinxISA::CMPGEI, RHS, *Imm); // C <= x  <=>  x >= C
    return emitRR(LinxISA::CMPGE, RHS, LHS);     // x <= y  <=>  y >= x
  }
  case ISD::SETGT: {
    if (auto Imm = getImmS(RHS);
        Imm && *Imm != std::numeric_limits<int64_t>::max() &&
        isInt<12>(*Imm + 1))
      return emitRI(LinxISA::CMPGEI, LHS, *Imm + 1); // x > C  <=>  x >= C+1
    if (auto Imm = getImmS(LHS); Imm && isInt<12>(*Imm))
      return emitRI(LinxISA::CMPLTI, RHS, *Imm); // C > x  <=>  x < C
    return emitRR(LinxISA::CMPLT, RHS, LHS);     // x > y  <=>  y < x
  }
  case ISD::SETGE: {
    if (auto Imm = getImmS(RHS); Imm && isInt<12>(*Imm))
      return emitRI(LinxISA::CMPGEI, LHS, *Imm);
    if (auto Imm = getImmS(LHS);
        Imm && *Imm != std::numeric_limits<int64_t>::max() &&
        isInt<12>(*Imm + 1))
      return emitRI(LinxISA::CMPLTI, RHS, *Imm + 1); // C >= x  <=>  x < C+1
    return emitRR(LinxISA::CMPGE, LHS, RHS);
  }
  case ISD::SETULT: {
    if (auto Imm = getImmU(RHS); Imm && isUInt<12>(*Imm))
      return emitRUI(LinxISA::CMPLTUI, LHS, *Imm);
    if (auto Imm = getImmU(LHS);
        Imm && *Imm != std::numeric_limits<uint64_t>::max() &&
        isUInt<12>(*Imm + 1))
      return emitRUI(LinxISA::CMPGEUI, RHS, *Imm + 1); // C <u x  <=>  x >=u C+1
    return emitRR(LinxISA::CMPLTU, LHS, RHS);
  }
  case ISD::SETULE: {
    if (auto Imm = getImmU(RHS);
        Imm && *Imm != std::numeric_limits<uint64_t>::max() &&
        isUInt<12>(*Imm + 1))
      return emitRUI(LinxISA::CMPLTUI, LHS, *Imm + 1); // x <=u C  <=>  x <u C+1
    if (auto Imm = getImmU(LHS); Imm && isUInt<12>(*Imm))
      return emitRUI(LinxISA::CMPGEUI, RHS, *Imm); // C <=u x  <=>  x >=u C
    return emitRR(LinxISA::CMPGEU, RHS, LHS);      // x <=u y  <=>  y >=u x
  }
  case ISD::SETUGT: {
    if (auto Imm = getImmU(RHS);
        Imm && *Imm != std::numeric_limits<uint64_t>::max() &&
        isUInt<12>(*Imm + 1))
      return emitRUI(LinxISA::CMPGEUI, LHS, *Imm + 1); // x >u C  <=>  x >=u C+1
    if (auto Imm = getImmU(LHS); Imm && isUInt<12>(*Imm))
      return emitRUI(LinxISA::CMPLTUI, RHS, *Imm); // C >u x  <=>  x <u C
    return emitRR(LinxISA::CMPLTU, RHS, LHS);      // x >u y  <=>  y <u x
  }
  case ISD::SETUGE: {
    if (auto Imm = getImmU(RHS); Imm && isUInt<12>(*Imm))
      return emitRUI(LinxISA::CMPGEUI, LHS, *Imm);
    if (auto Imm = getImmU(LHS);
        Imm && *Imm != std::numeric_limits<uint64_t>::max() &&
        isUInt<12>(*Imm + 1))
      return emitRUI(LinxISA::CMPLTUI, RHS, *Imm + 1); // C >=u x  <=>  x <u C+1
    return emitRR(LinxISA::CMPGEU, LHS, RHS);
  }
  default:
    report_fatal_error("Linx: unsupported SETCC condition");
  }
}

SDValue LinxISATargetLowering::LowerFPBinOp(SDValue Op, SelectionDAG &DAG,
                                           unsigned Opc) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  const int64_t SrcType = linxEncodeFpSrcType(VT);
  SDValue SrcTypeImm = DAG.getTargetConstant(SrcType, DL, MVT::i64);
  return SDValue(
      DAG.getMachineNode(Opc, DL, VT, Op.getOperand(0), Op.getOperand(1),
                         SrcTypeImm),
      0);
}

SDValue LinxISATargetLowering::LowerFPUnOp(SDValue Op, SelectionDAG &DAG,
                                          unsigned Opc) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();
  const int64_t SrcType = linxEncodeFpSrcType(VT);
  SDValue SrcTypeImm = DAG.getTargetConstant(SrcType, DL, MVT::i64);
  return SDValue(
      DAG.getMachineNode(Opc, DL, VT, Op.getOperand(0), SrcTypeImm), 0);
}

SDValue LinxISATargetLowering::LowerFP_TO_SINT(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT DstVT = Op.getValueType();
  SDValue Src = Op.getOperand(0);
  EVT SrcVT = Src.getValueType();

  const int64_t DstType = linxEncodeCvtDstTypeSInt(DstVT);
  const int64_t SrcType = linxEncodeFpSrcType(SrcVT);

  SDValue DstTypeImm = DAG.getTargetConstant(DstType, DL, MVT::i64);
  SDValue SrcTypeImm = DAG.getTargetConstant(SrcType, DL, MVT::i64);
  return SDValue(
      DAG.getMachineNode(LinxISA::FCVTZ, DL, DstVT, Src, DstTypeImm,
                         SrcTypeImm),
      0);
}

SDValue LinxISATargetLowering::LowerFP_TO_UINT(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT DstVT = Op.getValueType();
  SDValue Src = Op.getOperand(0);
  EVT SrcVT = Src.getValueType();

  const int64_t DstType = linxEncodeCvtDstTypeUInt(DstVT);
  const int64_t SrcType = linxEncodeFpSrcType(SrcVT);

  SDValue DstTypeImm = DAG.getTargetConstant(DstType, DL, MVT::i64);
  SDValue SrcTypeImm = DAG.getTargetConstant(SrcType, DL, MVT::i64);
  return SDValue(
      DAG.getMachineNode(LinxISA::FCVTZ, DL, DstVT, Src, DstTypeImm,
                         SrcTypeImm),
      0);
}

SDValue LinxISATargetLowering::LowerSINT_TO_FP(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT DstVT = Op.getValueType();
  SDValue Src = Op.getOperand(0);
  EVT SrcVT = Src.getValueType();

  const int64_t DstType = linxEncodeCvtDstTypeFP(DstVT);
  const int64_t SrcType = linxEncodeCvtSrcTypeInt(SrcVT);

  SDValue DstTypeImm = DAG.getTargetConstant(DstType, DL, MVT::i64);
  SDValue SrcTypeImm = DAG.getTargetConstant(SrcType, DL, MVT::i64);
  return SDValue(
      DAG.getMachineNode(LinxISA::SCVTF, DL, DstVT, Src, DstTypeImm, SrcTypeImm),
      0);
}

SDValue LinxISATargetLowering::LowerUINT_TO_FP(SDValue Op,
                                              SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT DstVT = Op.getValueType();
  SDValue Src = Op.getOperand(0);
  EVT SrcVT = Src.getValueType();

  const int64_t DstType = linxEncodeCvtDstTypeFP(DstVT);
  const int64_t SrcType = linxEncodeCvtSrcTypeInt(SrcVT);

  SDValue DstTypeImm = DAG.getTargetConstant(DstType, DL, MVT::i64);
  SDValue SrcTypeImm = DAG.getTargetConstant(SrcType, DL, MVT::i64);
  return SDValue(
      DAG.getMachineNode(LinxISA::UCVTF, DL, DstVT, Src, DstTypeImm, SrcTypeImm),
      0);
}

SDValue LinxISATargetLowering::LowerFP_ROUND(SDValue Op,
                                            SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT DstVT = Op.getValueType();
  SDValue Src = Op.getOperand(0);
  EVT SrcVT = Src.getValueType();

  const int64_t DstType = linxEncodeCvtDstTypeFP(DstVT);
  const int64_t SrcType = linxEncodeFpSrcType(SrcVT);

  SDValue DstTypeImm = DAG.getTargetConstant(DstType, DL, MVT::i64);
  SDValue SrcTypeImm = DAG.getTargetConstant(SrcType, DL, MVT::i64);
  return SDValue(
      DAG.getMachineNode(LinxISA::FCVT, DL, DstVT, Src, DstTypeImm, SrcTypeImm),
      0);
}

SDValue LinxISATargetLowering::LowerFP_EXTEND(SDValue Op,
                                             SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT DstVT = Op.getValueType();
  SDValue Src = Op.getOperand(0);
  EVT SrcVT = Src.getValueType();

  const int64_t DstType = linxEncodeCvtDstTypeFP(DstVT);
  const int64_t SrcType = linxEncodeFpSrcType(SrcVT);

  SDValue DstTypeImm = DAG.getTargetConstant(DstType, DL, MVT::i64);
  SDValue SrcTypeImm = DAG.getTargetConstant(SrcType, DL, MVT::i64);
  return SDValue(
      DAG.getMachineNode(LinxISA::FCVT, DL, DstVT, Src, DstTypeImm, SrcTypeImm),
      0);
}

SDValue LinxISATargetLowering::LowerGlobalAddress(SDValue Op,
                                                  SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT Ty = Op.getValueType();
  GlobalAddressSDNode *N = cast<GlobalAddressSDNode>(Op);
  const GlobalValue *GV = N->getGlobal();
  int64_t Offset = N->getOffset();

  // PC-relative global address materialization.
  //
  // ADDTPC's immediate is page-scaled (imm20 << 12). Materialize the full
  // address via:
  //   ADDTPC (page of symbol) + ADDI/ADDIW (low 12 bits).
  SDValue GA = DAG.getTargetGlobalAddress(GV, DL, Ty, Offset);

  SDValue Page = SDValue(DAG.getMachineNode(LinxISA::ADDTPC, DL, Ty, GA), 0);
  const unsigned AddOpc =
      (Ty == MVT::i32) ? LinxISA::ADDIWri : LinxISA::ADDIri;
  return SDValue(DAG.getMachineNode(AddOpc, DL, Ty, Page, GA), 0);
}

SDValue LinxISATargetLowering::LowerGlobalTLSAddress(
    SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT Ty = Op.getValueType();
  // SelectionDAG uses GlobalAddressSDNode for both GlobalAddress and
  // GlobalTLSAddress opcodes.
  auto *N = cast<GlobalAddressSDNode>(Op);
  const GlobalValue *GV = N->getGlobal();
  int64_t Offset = N->getOffset();

  // Bring-up: treat TLS variables as normal global symbols. This is sufficient
  // for single-threaded environments and allows building glibc objects before
  // the TLS ABI is finalized.
  SDValue GA = DAG.getTargetGlobalAddress(GV, DL, Ty, Offset);
  SDValue Page = SDValue(DAG.getMachineNode(LinxISA::ADDTPC, DL, Ty, GA), 0);
  const unsigned AddOpc =
      (Ty == MVT::i32) ? LinxISA::ADDIWri : LinxISA::ADDIri;
  return SDValue(DAG.getMachineNode(AddOpc, DL, Ty, Page, GA), 0);
}

SDValue LinxISATargetLowering::LowerConstantPool(SDValue Op,
                                                 SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT Ty = Op.getValueType();
  auto *CP = cast<ConstantPoolSDNode>(Op);

  // PC-relative constant pool address materialization:
  //   ADDTPC (page of CPI) + ADDI/ADDIW (low 12 bits).
  SDValue CPI = DAG.getTargetConstantPool(CP->getConstVal(), Ty, CP->getAlign(),
                                         CP->getOffset());
  SDValue Page = SDValue(DAG.getMachineNode(LinxISA::ADDTPC, DL, Ty, CPI), 0);
  const unsigned AddOpc =
      (Ty == MVT::i32) ? LinxISA::ADDIWri : LinxISA::ADDIri;
  return SDValue(DAG.getMachineNode(AddOpc, DL, Ty, Page, CPI), 0);
}

SDValue LinxISATargetLowering::LowerVASTART(SDValue Op,
                                            SelectionDAG &DAG) const {
  MachineFunction &MF = DAG.getMachineFunction();
  auto *FuncInfo = MF.getInfo<LinxISAMachineFunctionInfo>();

  SDLoc DL(Op);
  EVT PtrVT = getPointerTy(MF.getDataLayout());
  SDValue FI =
      DAG.getFrameIndex(FuncInfo->getVarArgsFrameIndex(), PtrVT);

  // VASTART stores the address of the first vararg slot into the va_list
  // object.
  const Value *SV = cast<SrcValueSDNode>(Op.getOperand(2))->getValue();
  return DAG.getStore(Op.getOperand(0), DL, FI, Op.getOperand(1),
                      MachinePointerInfo(SV));
}

static SDValue bitcastAndResizeToVT(SelectionDAG &DAG, SDValue V, EVT DstVT,
                                    const SDLoc &DL) {
  EVT SrcVT = V.getValueType();
  if (SrcVT == DstVT)
    return V;

  auto toInt = [&](SDValue X) -> SDValue {
    EVT XVT = X.getValueType();
    if (XVT.isInteger())
      return X;
    EVT IntVT = EVT::getIntegerVT(*DAG.getContext(), XVT.getSizeInBits());
    return DAG.getNode(ISD::BITCAST, DL, IntVT, X);
  };

  SDValue IntV = toInt(V);
  EVT DstIntVT =
      DstVT.isInteger() ? DstVT
                        : EVT::getIntegerVT(*DAG.getContext(), DstVT.getSizeInBits());
  unsigned SrcBits = IntV.getValueType().getSizeInBits();
  unsigned DstBits = DstIntVT.getSizeInBits();
  if (SrcBits > DstBits)
    IntV = DAG.getNode(ISD::TRUNCATE, DL, DstIntVT, IntV);
  else if (SrcBits < DstBits)
    IntV = DAG.getNode(ISD::ANY_EXTEND, DL, DstIntVT, IntV);

  if (DstVT.isInteger())
    return IntV;
  return DAG.getNode(ISD::BITCAST, DL, DstVT, IntV);
}

// Convert a location-typed value coming from the ABI into the IR value type.
static SDValue convertLocVTToValVT(SelectionDAG &DAG, SDValue V,
                                   const CCValAssign &VA, const SDLoc &DL) {
  EVT LocVT = VA.getLocVT();
  EVT ValVT = VA.getValVT();

  switch (VA.getLocInfo()) {
  default:
    llvm_unreachable("Unexpected CCValAssign::LocInfo");
  case CCValAssign::Full:
  case CCValAssign::Indirect:
  case CCValAssign::BCvt:
    return bitcastAndResizeToVT(DAG, V, ValVT, DL);
  case CCValAssign::SExt:
  case CCValAssign::ZExt:
  case CCValAssign::AExt:
    if (LocVT.isInteger() && ValVT.isInteger() &&
        LocVT.getSizeInBits() > ValVT.getSizeInBits())
      return DAG.getNode(ISD::TRUNCATE, DL, ValVT, V);
    return bitcastAndResizeToVT(DAG, V, ValVT, DL);
  }
}

// Convert an IR value into the ABI location type before call/return emission.
static SDValue convertValVTToLocVT(SelectionDAG &DAG, SDValue V,
                                   const CCValAssign &VA, const SDLoc &DL) {
  EVT LocVT = VA.getLocVT();
  EVT ValVT = VA.getValVT();

  switch (VA.getLocInfo()) {
  default:
    llvm_unreachable("Unexpected CCValAssign::LocInfo");
  case CCValAssign::Full:
  case CCValAssign::Indirect:
  case CCValAssign::BCvt:
    return bitcastAndResizeToVT(DAG, V, LocVT, DL);
  case CCValAssign::SExt:
  case CCValAssign::ZExt:
  case CCValAssign::AExt:
    if (!(LocVT.isInteger() && ValVT.isInteger()))
      return bitcastAndResizeToVT(DAG, V, LocVT, DL);
    if (ValVT.getSizeInBits() > LocVT.getSizeInBits())
      return DAG.getNode(ISD::TRUNCATE, DL, LocVT, V);
    if (ValVT.getSizeInBits() < LocVT.getSizeInBits()) {
      switch (VA.getLocInfo()) {
      case CCValAssign::SExt:
        return DAG.getNode(ISD::SIGN_EXTEND, DL, LocVT, V);
      case CCValAssign::ZExt:
        return DAG.getNode(ISD::ZERO_EXTEND, DL, LocVT, V);
      case CCValAssign::AExt:
        return DAG.getNode(ISD::ANY_EXTEND, DL, LocVT, V);
      default:
        llvm_unreachable("Unexpected CCValAssign::LocInfo");
      }
    }
    return V;
  }
}

SDValue LinxISATargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  // LinxISA bring-up ABI: reserve a fixed "call frame"/home area above the
  // local stack frame. The QEMU model adjusts SP by (StackSize + CallFrameSize)
  // in the FENTRY/FRET.* template blocks.
  //
  // Stack-passed arguments are laid out by the caller at the entry SP (before
  // FENTRY), so after the template SP adjustment they appear at
  //   SP + StackSize + CallFrameSize + ArgOffset.
  //
  // Our eliminateFrameIndex logic materializes addresses as
  //   SP + StackSize + FixedObjectOffset,
  // so we bias the fixed-object offsets by CallFrameSize to compensate.
  static constexpr int64_t CallFrameSize = 64;

  // Varargs support is limited during bring-up.
  // All varargs must be passed on stack.
  if (IsVarArg && CallConv != CallingConv::C) {
    report_fatal_error("Linx: varargs not supported for non-C calling conventions");
  }

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();
  SmallVector<SDValue, 8> OutChains;

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  const bool Is64 = DAG.getDataLayout().getPointerSizeInBits() == 64;
  CCInfo.AnalyzeFormalArguments(Ins, Is64 ? CC_Linx64 : CC_Linx32);

  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    const CCValAssign &VA = ArgLocs[i];
    MVT LocVT = VA.getLocVT();

    SDValue V;
    if (VA.isRegLoc()) {
      Register PhysReg = VA.getLocReg();
      Register VReg = MF.addLiveIn(PhysReg, &LinxISA::GPRRegClass);
      SDValue Copy = DAG.getCopyFromReg(Chain, DL, VReg, LocVT);
      V = Copy.getValue(0);
      Chain = Copy.getValue(1);
    } else {
      assert(VA.isMemLoc() && "Unknown argument location");
      int FI = MFI.CreateFixedObject(LocVT.getStoreSize(),
                                     VA.getLocMemOffset() + CallFrameSize,
                                     /*IsImmutable=*/true);
      SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
      SDValue Load = DAG.getLoad(LocVT, DL, Chain, FIN,
                                 MachinePointerInfo::getFixedStack(MF, FI));
      V = Load.getValue(0);
      Chain = Load.getValue(1);
    }

    V = convertLocVTToValVT(DAG, V, VA, DL);
    InVals.push_back(V);
  }

  if (IsVarArg) {
    auto *FuncInfo = MF.getInfo<LinxISAMachineFunctionInfo>();
    // Clang uses a simple `void*` va_list for LinxISA. For correctness, all
    // variadic arguments are passed on the stack with natural size/alignment
    // (see LinxISACallingConv.td). The varargs area therefore begins at the
    // first stack slot after the fixed arguments.
    const int VaArgOffset = CCInfo.getStackSize() + CallFrameSize;
    const int FI = MFI.CreateFixedObject(/*Size=*/1, VaArgOffset,
                                         /*IsImmutable=*/true);
    FuncInfo->setVarArgsFrameIndex(FI);
    FuncInfo->setVarArgsSaveSize(0);
  }

  if (!OutChains.empty()) {
    OutChains.push_back(Chain);
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, OutChains);
  }

  return Chain;
}

static SDValue lowerCallResult(SDValue Chain, SDValue InGlue,
                               CallingConv::ID CallConv, bool IsVarArg,
                               const SmallVectorImpl<ISD::InputArg> &Ins,
                               const SDLoc &DL, SelectionDAG &DAG,
                               SmallVectorImpl<SDValue> &InVals) {
  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());

  const bool Is64 = DAG.getDataLayout().getPointerSizeInBits() == 64;
  CCInfo.AnalyzeCallResult(Ins, Is64 ? RetCC_Linx64 : RetCC_Linx32);

  for (const CCValAssign &VA : RVLocs) {
    MVT LocVT = VA.getLocVT();

    SDValue Copy = DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), LocVT, InGlue);
    SDValue V = Copy.getValue(0);
    Chain = Copy.getValue(1);
    InGlue = Copy.getValue(2);

    V = convertLocVTToValVT(DAG, V, VA, DL);
    InVals.push_back(V);
  }

  return Chain;
}

SDValue LinxISATargetLowering::LowerCall(CallLoweringInfo &CLI,
                                        SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc DL(CLI.DL);

  const bool IsMustTail = CLI.IsTailCall && CLI.CB && CLI.CB->isMustTailCall();
  // Tail-call rollout is musttail-first for Linx.
  CLI.IsTailCall = IsMustTail;

  SDValue Chain = CLI.Chain;
  SDValue Callee = CLI.Callee;
  CallingConv::ID CallConv = CLI.CallConv;
  bool IsVarArg = CLI.IsVarArg;

  // Varargs support is limited during bring-up.
  // Allow varargs calls but they must use stack passing only.
  if (IsVarArg && CallConv != CallingConv::C) {
    report_fatal_error("Linx: varargs calls not supported for non-C calling conventions");
  }

  // Analyze operands of the call, assigning locations to each operand.
  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), ArgLocs,
                 *DAG.getContext());
  const bool Is64 = DAG.getDataLayout().getPointerSizeInBits() == 64;
  CCInfo.AnalyzeCallOperands(CLI.Outs, Is64 ? CC_Linx64 : CC_Linx32);

  unsigned NumBytes = CCInfo.getStackSize();
  // Keep the stack aligned at call boundaries.
  NumBytes = alignTo(NumBytes, 16u);
  const bool UseTailCall = CLI.IsTailCall;
  if (UseTailCall) {
    if (NumBytes != 0) {
      report_fatal_error("Linx: musttail with stack arguments is not supported");
    }
    if (IsVarArg) {
      report_fatal_error("Linx: musttail varargs calls are not supported");
    }
  } else {
    Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, DL);
  }

  SmallVector<std::pair<unsigned, SDValue>, 8> RegsToPass;
  SmallVector<SDValue, 16> MemOpChains;
  SDValue StackPtr;
  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    const CCValAssign &VA = ArgLocs[i];
    SDValue Arg = CLI.OutVals[i];

    Arg = convertValVTToLocVT(DAG, Arg, VA, DL);

    if (VA.isRegLoc()) {
      RegsToPass.push_back({VA.getLocReg(), Arg});
      continue;
    }

    assert(VA.isMemLoc() && "Unknown call argument location");
    if (UseTailCall)
      report_fatal_error("Linx: musttail with memory argument location is not supported");

    if (!StackPtr.getNode())
      StackPtr = DAG.getCopyFromReg(Chain, DL, LinxISA::R1, PtrVT);

    SDValue PtrOff =
        DAG.getNode(ISD::ADD, DL, PtrVT, StackPtr,
                    DAG.getIntPtrConstant(VA.getLocMemOffset(), DL));
    MemOpChains.push_back(
        DAG.getStore(Chain, DL, Arg, PtrOff, MachinePointerInfo()));
  }

  if (!MemOpChains.empty())
    Chain = DAG.getNode(ISD::TokenFactor, DL, MVT::Other, MemOpChains);

  SDValue InGlue;
  for (const auto &[Reg, Val] : RegsToPass) {
    Chain = DAG.getCopyToReg(Chain, DL, Reg, Val, InGlue);
    InGlue = Chain.getValue(1);
  }

  // Direct calls: GlobalAddress/ExternalSymbol to target variants.
  const TargetMachine &TM = getTargetMachine();
  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    const GlobalValue *GV = G->getGlobal();
    const unsigned Flags = !TM.shouldAssumeDSOLocal(GV) ? LinxII::MO_PLT : 0;
    Callee = DAG.getTargetGlobalAddress(GV, DL, PtrVT, 0, Flags);
  } else if (auto *E = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), PtrVT, LinxII::MO_PLT);
  } else {
    // Indirect call: Callee is a value (register) computed by the program.
    // The BlockISA lowering pass will translate this into an ICALL block using
    // SETC.TGT to select the target.
    if (Callee.getValueType() != PtrVT) {
      Callee = DAG.getNode(ISD::ZERO_EXTEND, DL, PtrVT, Callee);
    }
  }

  const uint32_t *Mask = STI.getRegisterInfo()->getCallPreservedMask(
      DAG.getMachineFunction(), CallConv);
  assert(Mask && "Missing call preserved mask");
  SDVTList NodeTys = DAG.getVTList(MVT::Other, MVT::Glue);
  SmallVector<SDValue, 16> Ops;
  Ops.push_back(Callee);
  Ops.push_back(DAG.getRegisterMask(Mask));
  for (const auto &[Reg, Val] : RegsToPass)
    Ops.push_back(DAG.getRegister(Reg, Val.getValueType()));
  Ops.push_back(Chain);
  if (InGlue.getNode())
    Ops.push_back(InGlue);

  const unsigned CallOpc =
      UseTailCall ? LinxISA::PSEUDO_TAILCALL : LinxISA::PSEUDO_CALL;
  MachineSDNode *Call = DAG.getMachineNode(CallOpc, DL, NodeTys, Ops);
  Chain = SDValue(Call, 0);
  InGlue = SDValue(Call, 1);

  if (UseTailCall) {
    return Chain;
  }

  Chain = DAG.getCALLSEQ_END(Chain, NumBytes, 0, InGlue, DL);
  InGlue = Chain.getValue(1);

  return lowerCallResult(Chain, InGlue, CallConv, IsVarArg, CLI.Ins, DL, DAG,
                         InVals);
}

SDValue LinxISATargetLowering::LowerReturn(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::OutputArg> &Outs,
    const SmallVectorImpl<SDValue> &OutVals, const SDLoc &DL, SelectionDAG &DAG)
    const {
  if (!Chain.getNode())
    report_fatal_error("Linx: LowerReturn called with null chain");
  // Varargs return support is limited during bring-up.
  if (IsVarArg && CallConv != CallingConv::C) {
    report_fatal_error("Linx: varargs not supported for non-C calling conventions");
  }

  SmallVector<CCValAssign, 16> RVLocs;
  CCState CCInfo(CallConv, IsVarArg, DAG.getMachineFunction(), RVLocs,
                 *DAG.getContext());
  const bool Is64 = DAG.getDataLayout().getPointerSizeInBits() == 64;
  CCInfo.AnalyzeReturn(Outs, Is64 ? RetCC_Linx64 : RetCC_Linx32);

  SDValue Glue;
  SmallVector<SDValue, 8> RetOps;
  for (unsigned i = 0, e = RVLocs.size(); i != e; ++i) {
    const CCValAssign &VA = RVLocs[i];
    SDValue Val = OutVals[i];

    Val = convertValVTToLocVT(DAG, Val, VA, DL);

    Chain = DAG.getCopyToReg(Chain, DL, VA.getLocReg(), Val, Glue);
    Glue = Chain.getValue(1);
    RetOps.push_back(DAG.getRegister(VA.getLocReg(), VA.getLocVT()));
  }

  // Build a return machine node directly to avoid reliance on custom
  // target-opcode SDNodes during bring-up. Include the return registers as
  // (implicit) operands to keep them live-out and prevent return value copies
  // from being DCE'd under optimization.
  RetOps.push_back(Chain);
  if (Glue.getNode())
    RetOps.push_back(Glue);
  return SDValue(DAG.getMachineNode(LinxISA::PSEUDO_RET, DL, MVT::Other, RetOps),
                 0);
}

const char *LinxISATargetLowering::getTargetNodeName(unsigned Opcode) const {
  switch (Opcode) {
  case LinxISD::CALL:
    return "LinxISD::CALL";
  case LinxISD::RET_GLUE:
    return "LinxISD::RET_GLUE";
  case LinxISD::BR_CC:
    return "LinxISD::BR_CC";
  case LinxISD::SETCC:
    return "LinxISD::SETCC";
  default:
    return nullptr;
  }
}

std::pair<unsigned, const TargetRegisterClass *>
LinxISATargetLowering::getRegForInlineAsmConstraint(
    const TargetRegisterInfo *TRI, StringRef Constraint, MVT VT) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'r':
      if (!VT.isVector())
        // The Linx GPR encoding space includes queue pseudo-registers
        // (t#k/u#k and the special RegDst encodings for ->t/->u). These are not
        // general-purpose registers and are not safe for C inline-asm operands
        // (especially for syscall/ACR entry/exit sequences). Restrict the "r"
        // constraint to architectural registers only.
        return std::make_pair(0u, &LinxISA::GPR_ArchRegClass);
      break;
    default:
      break;
    }
  }

  // Explicit register constraints: "{a0}", "{a1}", ...
  if (Constraint.size() > 2 && Constraint.front() == '{' &&
      Constraint.back() == '}') {
    StringRef Name = Constraint.drop_front().drop_back();
    std::string Lower = Name.trim().lower();
    StringRef Up(Lower);
    unsigned Phys = 0;
    bool Found = true;
    if (Up == "zero")
      Phys = LinxISA::R0;
    else if (Up == "sp")
      Phys = LinxISA::R1;
    else if (Up == "a0")
      Phys = LinxISA::R2;
    else if (Up == "a1")
      Phys = LinxISA::R3;
    else if (Up == "a2")
      Phys = LinxISA::R4;
    else if (Up == "a3")
      Phys = LinxISA::R5;
    else if (Up == "a4")
      Phys = LinxISA::R6;
    else if (Up == "a5")
      Phys = LinxISA::R7;
    else if (Up == "a6")
      Phys = LinxISA::R8;
    else if (Up == "a7")
      Phys = LinxISA::R9;
    else if (Up == "ra")
      Phys = LinxISA::R10;
    else if (Up == "s0")
      Phys = LinxISA::R11;
    else if (Up == "s1")
      Phys = LinxISA::R12;
    else if (Up == "s2")
      Phys = LinxISA::R13;
    else if (Up == "s3")
      Phys = LinxISA::R14;
    else if (Up == "s4")
      Phys = LinxISA::R15;
    else if (Up == "s5")
      Phys = LinxISA::R16;
    else if (Up == "s6")
      Phys = LinxISA::R17;
    else if (Up == "s7")
      Phys = LinxISA::R18;
    else if (Up == "s8")
      Phys = LinxISA::R19;
    else if (Up == "x0")
      Phys = LinxISA::R20;
    else if (Up == "x1")
      Phys = LinxISA::R21;
    else if (Up == "x2")
      Phys = LinxISA::R22;
    else if (Up == "x3")
      Phys = LinxISA::R23;
    else
      Found = false;

    if (Found)
      return std::make_pair(Phys, &LinxISA::GPR_ArchRegClass);
  }

  return TargetLowering::getRegForInlineAsmConstraint(TRI, Constraint, VT);
}

void LinxISATargetLowering::LowerAsmOperandForConstraint(
    SDValue Op, StringRef Constraint, std::vector<SDValue> &Ops,
    SelectionDAG &DAG) const {
  if (Constraint.size() == 1) {
    switch (Constraint[0]) {
    case 'i':
    case 'n': {
      if (const auto *C = dyn_cast<ConstantSDNode>(Op)) {
        Ops.push_back(DAG.getTargetConstant(C->getSExtValue(), SDLoc(Op),
                                            Op.getValueType()));
        return;
      }
      break;
    }
    default:
      break;
    }
  }
  TargetLowering::LowerAsmOperandForConstraint(Op, Constraint, Ops, DAG);
}
