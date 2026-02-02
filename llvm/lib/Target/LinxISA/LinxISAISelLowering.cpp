//===-- LinxISAISelLowering.cpp - LinxISA DAG Lowering --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LinxISAISelLowering.h"
#include "LinxISA.h"
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

using namespace llvm;

//===----------------------------------------------------------------------===//
// Calling convention implementation.
//===----------------------------------------------------------------------===//

#include "LinxISAGenCallingConv.inc"

LinxISATargetLowering::LinxISATargetLowering(const TargetMachine &TM,
                                             const LinxISASubtarget &STI)
    : TargetLowering(TM, STI), STI(STI) {
  addRegisterClass(MVT::i64, &LinxISA::GPRRegClass);
  addRegisterClass(MVT::i32, &LinxISA::GPRRegClass);

  computeRegisterProperties(STI.getRegisterInfo());
  setStackPointerRegisterToSaveRestore(LinxISA::R1);

  // LinxISA comparisons produce 0/1 values.
  setBooleanContents(ZeroOrOneBooleanContent);

  setOperationAction(ISD::BR, MVT::Other, Custom);
  setOperationAction(ISD::BR_CC, MVT::i64, Custom);
  setOperationAction(ISD::BR_CC, MVT::i32, Custom);

  // i1 is promoted to a register-sized integer before isel.
  setOperationAction(ISD::SETCC, MVT::i64, Custom);
  setOperationAction(ISD::SETCC, MVT::i32, Custom);

  // Bring-up: avoid generating jump tables.
  setOperationAction(ISD::BR_JT, MVT::Other, Expand);

  // Lower atomic fences via libcalls (e.g. __sync_synchronize) for bring-up.
  setOperationAction(ISD::ATOMIC_FENCE, MVT::Other, Expand);

  setOperationAction(ISD::SIGN_EXTEND, MVT::i64, Custom);
  setOperationAction(ISD::ZERO_EXTEND, MVT::i64, Custom);
  setOperationAction(ISD::SIGN_EXTEND_INREG, MVT::i64, Custom);

  // Bring-up: expand rotates to shifts + ors.
  setOperationAction(ISD::ROTL, MVT::i32, Expand);
  setOperationAction(ISD::ROTL, MVT::i64, Expand);
  setOperationAction(ISD::ROTR, MVT::i32, Expand);
  setOperationAction(ISD::ROTR, MVT::i64, Expand);

  // Drop prefetches for now.
  setOperationAction(ISD::PREFETCH, MVT::Other, Expand);

  // Bring-up: avoid introducing target-specific select/cmov patterns.
  setOperationAction(ISD::SELECT, MVT::i32, Custom);
  setOperationAction(ISD::SELECT, MVT::i64, Custom);
  setOperationAction(ISD::SELECT_CC, MVT::i32, Expand);
  setOperationAction(ISD::SELECT_CC, MVT::i64, Expand);

  // GlobalAddress must be custom-lowered to PC-relative addressing.
  setOperationAction(ISD::GlobalAddress, MVT::i64, Custom);
  setOperationAction(ISD::GlobalAddress, MVT::i32, Custom);

  //===----------------------------------------------------------------------===//
  // Floating Point Operations
  //===----------------------------------------------------------------------===//
  //
  // For floating point operations, we need to set them to Expand to allow
  // libcall generation. Without this, we get "unsupported library call operation"
  // errors when trying to compile floating point code.

  // 32-bit floating point operations
  setOperationAction(ISD::FADD, MVT::f32, Expand);
  setOperationAction(ISD::FSUB, MVT::f32, Expand);
  setOperationAction(ISD::FMUL, MVT::f32, Expand);
  setOperationAction(ISD::FDIV, MVT::f32, Expand);
  setOperationAction(ISD::FREM, MVT::f32, Expand);
  setOperationAction(ISD::FNEG, MVT::f32, Expand);
  setOperationAction(ISD::FABS, MVT::f32, Expand);
  setOperationAction(ISD::FSQRT, MVT::f32, Expand);
  setOperationAction(ISD::FCOPYSIGN, MVT::f32, Expand);
  setOperationAction(ISD::SETCC, MVT::f32, Expand);
  setOperationAction(ISD::FEXP2, MVT::f32, Expand);
  setOperationAction(ISD::FLOG2, MVT::f32, Expand);

  // 64-bit floating point operations
  setOperationAction(ISD::FADD, MVT::f64, Expand);
  setOperationAction(ISD::FSUB, MVT::f64, Expand);
  setOperationAction(ISD::FMUL, MVT::f64, Expand);
  setOperationAction(ISD::FDIV, MVT::f64, Expand);
  setOperationAction(ISD::FREM, MVT::f64, Expand);
  setOperationAction(ISD::FNEG, MVT::f64, Expand);
  setOperationAction(ISD::FABS, MVT::f64, Expand);
  setOperationAction(ISD::FSQRT, MVT::f64, Expand);
  setOperationAction(ISD::FCOPYSIGN, MVT::f64, Expand);
  setOperationAction(ISD::SETCC, MVT::f64, Expand);
  setOperationAction(ISD::FEXP2, MVT::f64, Expand);
  setOperationAction(ISD::FLOG2, MVT::f64, Expand);

  // Float-to-int conversions
  setOperationAction(ISD::FP_TO_SINT, MVT::i32, Expand);
  setOperationAction(ISD::FP_TO_UINT, MVT::i32, Expand);
  setOperationAction(ISD::FP_TO_SINT, MVT::i64, Expand);
  setOperationAction(ISD::FP_TO_UINT, MVT::i64, Expand);

  // Int-to-float conversions
  setOperationAction(ISD::SINT_TO_FP, MVT::f32, Expand);
  setOperationAction(ISD::UINT_TO_FP, MVT::f32, Expand);
  setOperationAction(ISD::SINT_TO_FP, MVT::f64, Expand);
  setOperationAction(ISD::UINT_TO_FP, MVT::f64, Expand);

  // Float-to-float conversions
  setOperationAction(ISD::FP_ROUND, MVT::f32, Expand);
  setOperationAction(ISD::FP_EXTEND, MVT::f64, Expand);

  // FMA (fused multiply-add)
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
  case ISD::BR_CC:
    return LowerBR_CC(Op, DAG);
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
  case ISD::GlobalAddress:
    return LowerGlobalAddress(Op, DAG);
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
  SDValue ShAmt = DAG.getConstant(Shift, DL, MVT::i64);
  SDValue Shl = DAG.getNode(ISD::SHL, DL, MVT::i64, Val, ShAmt);
  unsigned ShrOpc = IsSigned ? ISD::SRA : ISD::SRL;
  return DAG.getNode(ShrOpc, DL, MVT::i64, Shl, ShAmt);
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
  if (Op.getValueType() != MVT::i64)
    return SDValue();
  SDValue Val = Op.getOperand(0);
  EVT FromVT = cast<VTSDNode>(Op.getOperand(1))->getVT();
  unsigned FromBits = FromVT.getScalarSizeInBits();

  // Fast-path: sign-extend the low 32 bits in a GPR.
  if (FromBits == 32) {
    SDValue Zero = DAG.getRegister(LinxISA::R0, MVT::i64);
    return SDValue(DAG.getMachineNode(LinxISA::ADDWrr, DL, MVT::i64, Val, Zero),
                   0);
  }

  return buildExtendInReg(Val, FromBits, /*IsSigned=*/true, DL, DAG);
}

SDValue LinxISATargetLowering::LowerSELECT(SDValue Op, SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT VT = Op.getValueType();

  if (VT != MVT::i32 && VT != MVT::i64)
    return SDValue();

  SDValue Cond = Op.getOperand(0);
  SDValue TrueVal = Op.getOperand(1);
  SDValue FalseVal = Op.getOperand(2);

  // Lower to the ISA conditional select:
  //   rd = csel pred, true, false
  // with pred as a 0/1 integer value.
  SDValue Pred = Cond;
  if (Pred.getValueType() != VT) {
    unsigned PredBits = Pred.getValueType().getScalarSizeInBits();
    unsigned VBits = VT.getScalarSizeInBits();
    if (PredBits < VBits)
      Pred = DAG.getNode(ISD::ZERO_EXTEND, DL, VT, Pred);
    else
      Pred = DAG.getNode(ISD::TRUNCATE, DL, VT, Pred);
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
  unsigned CmpOpc = 0;
  switch (CC) {
  case ISD::SETEQ:
    CmpOpc = LinxISA::CMPEQ;
    break;
  case ISD::SETNE:
    CmpOpc = LinxISA::CMPNE;
    break;
  case ISD::SETLT:
    CmpOpc = LinxISA::CMPLT;
    break;
  case ISD::SETLE:
    SwapOps = true;
    CmpOpc = LinxISA::CMPGE;
    break;
  case ISD::SETGT:
    SwapOps = true;
    CmpOpc = LinxISA::CMPLT;
    break;
  case ISD::SETGE:
    CmpOpc = LinxISA::CMPGE;
    break;
  case ISD::SETULT:
    CmpOpc = LinxISA::CMPLTU;
    break;
  case ISD::SETULE:
    SwapOps = true;
    CmpOpc = LinxISA::CMPGEU;
    break;
  case ISD::SETUGT:
    SwapOps = true;
    CmpOpc = LinxISA::CMPLTU;
    break;
  case ISD::SETUGE:
    CmpOpc = LinxISA::CMPGEU;
    break;
  default:
    report_fatal_error("Linx: unsupported SETCC condition");
  }

  if (SwapOps)
    std::swap(LHS, RHS);

  return SDValue(
      DAG.getMachineNode(CmpOpc, DL, Op.getValueType(), LHS, RHS), 0);
}

SDValue LinxISATargetLowering::LowerGlobalAddress(SDValue Op,
                                                  SelectionDAG &DAG) const {
  SDLoc DL(Op);
  EVT Ty = Op.getValueType();
  GlobalAddressSDNode *N = cast<GlobalAddressSDNode>(Op);
  const GlobalValue *GV = N->getGlobal();
  int64_t Offset = N->getOffset();

  // Use ADDTPC to compute PC-relative address of the global.
  // ADDTPC rd, imm20 computes rd = PC + sext(imm20)
  // The assembler will create a relocation to resolve the actual offset.
  SDValue GA = DAG.getTargetGlobalAddress(GV, DL, Ty, Offset);
  return SDValue(DAG.getMachineNode(LinxISA::ADDTPC, DL, Ty, GA), 0);
}

static SDValue convertLocVTToValVT(SDValue V, MVT ValVT, const SDLoc &DL,
                                  SelectionDAG &DAG) {
  if (V.getValueType() != ValVT)
    V = DAG.getNode(ISD::TRUNCATE, DL, ValVT, V);
  return V;
}

SDValue LinxISATargetLowering::LowerFormalArguments(
    SDValue Chain, CallingConv::ID CallConv, bool IsVarArg,
    const SmallVectorImpl<ISD::InputArg> &Ins, const SDLoc &DL,
    SelectionDAG &DAG, SmallVectorImpl<SDValue> &InVals) const {
  // Varargs support is limited during bring-up.
  // All varargs must be passed on stack.
  if (IsVarArg && CallConv != CallingConv::C) {
    report_fatal_error("Linx: varargs not supported for non-C calling conventions");
  }

  MachineFunction &MF = DAG.getMachineFunction();
  MachineFrameInfo &MFI = MF.getFrameInfo();

  SmallVector<CCValAssign, 16> ArgLocs;
  CCState CCInfo(CallConv, IsVarArg, MF, ArgLocs, *DAG.getContext());
  const bool Is64 = DAG.getDataLayout().getPointerSizeInBits() == 64;
  CCInfo.AnalyzeFormalArguments(Ins, Is64 ? CC_Linx64 : CC_Linx32);

  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    const CCValAssign &VA = ArgLocs[i];
    MVT LocVT = VA.getLocVT();
    MVT ValVT = VA.getValVT();

    SDValue V;
    if (VA.isRegLoc()) {
      Register PhysReg = VA.getLocReg();
      Register VReg = MF.addLiveIn(PhysReg, &LinxISA::GPRRegClass);
      SDValue Copy = DAG.getCopyFromReg(Chain, DL, VReg, LocVT);
      V = Copy.getValue(0);
      Chain = Copy.getValue(1);
    } else {
      assert(VA.isMemLoc() && "Unknown argument location");
      int FI = MFI.CreateFixedObject(LocVT.getStoreSize(), VA.getLocMemOffset(),
                                     /*IsImmutable=*/true);
      SDValue FIN = DAG.getFrameIndex(FI, PtrVT);
      SDValue Load = DAG.getLoad(LocVT, DL, Chain, FIN,
                                 MachinePointerInfo::getFixedStack(MF, FI));
      V = Load.getValue(0);
      Chain = Load.getValue(1);
    }

    V = convertLocVTToValVT(V, ValVT, DL, DAG);
    InVals.push_back(V);
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
    MVT ValVT = VA.getValVT();

    SDValue Copy = DAG.getCopyFromReg(Chain, DL, VA.getLocReg(), LocVT, InGlue);
    SDValue V = Copy.getValue(0);
    Chain = Copy.getValue(1);
    InGlue = Copy.getValue(2);

    V = convertLocVTToValVT(V, ValVT, DL, DAG);
    InVals.push_back(V);
  }

  return Chain;
}

SDValue LinxISATargetLowering::LowerCall(CallLoweringInfo &CLI,
                                        SmallVectorImpl<SDValue> &InVals) const {
  SelectionDAG &DAG = CLI.DAG;
  SDLoc DL(CLI.DL);

  // Bring-up: do not attempt tail call lowering.
  CLI.IsTailCall = false;

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
  Chain = DAG.getCALLSEQ_START(Chain, NumBytes, 0, DL);

  SmallVector<std::pair<unsigned, SDValue>, 8> RegsToPass;
  SmallVector<SDValue, 16> MemOpChains;
  SDValue StackPtr;
  EVT PtrVT = getPointerTy(DAG.getDataLayout());

  for (unsigned i = 0, e = ArgLocs.size(); i != e; ++i) {
    const CCValAssign &VA = ArgLocs[i];
    SDValue Arg = CLI.OutVals[i];

    switch (VA.getLocInfo()) {
    case CCValAssign::Full:
      break;
    case CCValAssign::SExt:
      Arg = DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::ZExt:
      Arg = DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    case CCValAssign::AExt:
      Arg = DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), Arg);
      break;
    default:
      llvm_unreachable("Unexpected CCValAssign::LocInfo");
    }

    if (VA.isRegLoc()) {
      RegsToPass.push_back({VA.getLocReg(), Arg});
      continue;
    }

    assert(VA.isMemLoc() && "Unknown call argument location");

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
  if (auto *G = dyn_cast<GlobalAddressSDNode>(Callee)) {
    Callee = DAG.getTargetGlobalAddress(G->getGlobal(), DL, PtrVT, 0);
  } else if (auto *E = dyn_cast<ExternalSymbolSDNode>(Callee)) {
    Callee = DAG.getTargetExternalSymbol(E->getSymbol(), PtrVT, 0);
  } else {
    report_fatal_error("Linx: only direct calls supported for now");
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

  MachineSDNode *Call = DAG.getMachineNode(LinxISA::PSEUDO_CALL, DL, NodeTys, Ops);
  Chain = SDValue(Call, 0);
  InGlue = SDValue(Call, 1);

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

    switch (VA.getLocInfo()) {
    case CCValAssign::Full:
      break;
    case CCValAssign::SExt:
      Val = DAG.getNode(ISD::SIGN_EXTEND, DL, VA.getLocVT(), Val);
      break;
    case CCValAssign::ZExt:
      Val = DAG.getNode(ISD::ZERO_EXTEND, DL, VA.getLocVT(), Val);
      break;
    case CCValAssign::AExt:
      Val = DAG.getNode(ISD::ANY_EXTEND, DL, VA.getLocVT(), Val);
      break;
    default:
      llvm_unreachable("Unexpected CCValAssign::LocInfo");
    }

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
