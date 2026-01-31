//===- LinxISAInstrInfo.td - LinxISA instruction definitions -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

include "LinxISAInstrFormats.td"

//===----------------------------------------------------------------------===//
// DAG nodes for calls
//===----------------------------------------------------------------------===//

def SDT_LinxISACallSeqStart : SDCallSeqStart<[SDTCisVT<0, i64>,
                                             SDTCisVT<1, i64>]>;
def SDT_LinxISACallSeqEnd : SDCallSeqEnd<[SDTCisVT<0, i64>,
                                         SDTCisVT<1, i64>]>;
def SDT_LinxISACall : SDTypeProfile<0, -1, [SDTCisVT<0, iPTR>]>;

def CallSeqStart : SDNode<"ISD::CALLSEQ_START", SDT_LinxISACallSeqStart,
                          [SDNPHasChain, SDNPOutGlue]>;
def CallSeqEnd : SDNode<"ISD::CALLSEQ_END", SDT_LinxISACallSeqEnd,
                        [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue]>;

def Call : SDNode<"LinxISD::CALL", SDT_LinxISACall,
                  [SDNPHasChain, SDNPOptInGlue, SDNPOutGlue, SDNPVariadic]>;
def RetGlue : SDNode<"LinxISD::RET_GLUE", SDTNone,
                     [SDNPHasChain, SDNPOptInGlue, SDNPVariadic]>;

//===----------------------------------------------------------------------===//
// Operands
//===----------------------------------------------------------------------===//

def BrTargetAsmOperand : AsmOperandClass {
  let Name = "BrTarget";
}

def CallTargetAsmOperand : AsmOperandClass {
  let Name = "CallTarget";
}

def simm12 : Operand<i64>, PatLeaf<(i64 imm), [{ return isInt<12>(N->getSExtValue()); }]> {
  let ParserMatchClass = ImmAsmOperand;
}

def uimm12 : Operand<i64>, PatLeaf<(i64 imm), [{ return isUInt<12>(N->getZExtValue()); }]> {
  let ParserMatchClass = ImmAsmOperand;
}

def imm20 : Operand<i64>, PatLeaf<(i64 imm), [{ return isInt<20>(N->getSExtValue()); }]> {
  let ParserMatchClass = ImmAsmOperand;
}

def imm32 : Operand<i64>, PatLeaf<(i64 imm), [{ return isInt<32>(N->getSExtValue()); }]> {
  let ParserMatchClass = ImmAsmOperand;
}

def uimm5 : Operand<i64>, PatLeaf<(i64 imm), [{ return isUInt<5>(N->getZExtValue()); }]> {
  let ParserMatchClass = ImmAsmOperand;
}

def simm12_w : Operand<i32>, PatLeaf<(i32 imm), [{ return isInt<12>(N->getSExtValue()); }]> {
  let ParserMatchClass = ImmAsmOperand;
}

def uimm12_w : Operand<i32>, PatLeaf<(i32 imm), [{ return isUInt<12>(N->getZExtValue()); }]> {
  let ParserMatchClass = ImmAsmOperand;
}

def uimm5_w : Operand<i32>, PatLeaf<(i32 imm), [{ return isUInt<5>(N->getZExtValue()); }]> {
  let ParserMatchClass = ImmAsmOperand;
}

// Branch target operand.
def BrTarget : Operand<OtherVT> {
  let ParserMatchClass = BrTargetAsmOperand;
}

// Call target operand.
def CallTarget : Operand<iPTR> {
  let ParserMatchClass = CallTargetAsmOperand;
}

//===----------------------------------------------------------------------===//
// Pseudo instructions (expanded in the AsmPrinter)
//===----------------------------------------------------------------------===//

let isCall = 1, hasSideEffects = 1, Size = 6, Uses = [R1], Defs = [R10] in
def PSEUDO_CALL : Pseudo<(outs), (ins CallTarget:$dst), "# PSEUDO_CALL", []>;

let isReturn = 1, isTerminator = 1, Size = 4, Uses = [R10] in
def PSEUDO_RET : Pseudo<(outs), (ins), "# PSEUDO_RET", [(RetGlue)]>;

def : Pat<(Call tglobaladdr:$dst), (PSEUDO_CALL tglobaladdr:$dst)>;
def : Pat<(Call texternalsym:$dst), (PSEUDO_CALL texternalsym:$dst)>;

// Call frame pseudos.
let Defs = [R1], Uses = [R1] in {
  def ADJCALLSTACKDOWN : Pseudo<(outs), (ins i64imm:$amt1, i64imm:$amt2),
                                "#ADJCALLSTACKDOWN $amt1 $amt2",
                                [(CallSeqStart timm:$amt1, timm:$amt2)]>;
  def ADJCALLSTACKUP : Pseudo<(outs), (ins i64imm:$amt1, i64imm:$amt2),
                              "#ADJCALLSTACKUP $amt1 $amt2",
                              [(CallSeqEnd timm:$amt1, timm:$amt2)]>;
}

//===----------------------------------------------------------------------===//
// Integer arithmetic/logical ops (selected by patterns)
//===----------------------------------------------------------------------===//

def ADDrr  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", [(set (i64 GPR:$rd), (add (i64 GPR:$rs1), (i64 GPR:$rs2)))]>;
def SUBrr  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", [(set (i64 GPR:$rd), (sub (i64 GPR:$rs1), (i64 GPR:$rs2)))]>;
def ANDrr  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", [(set (i64 GPR:$rd), (and (i64 GPR:$rs1), (i64 GPR:$rs2)))]>;
def ORrr   : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", [(set (i64 GPR:$rd), (or  (i64 GPR:$rs1), (i64 GPR:$rs2)))]>;
def XORrr  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", [(set (i64 GPR:$rd), (xor (i64 GPR:$rs1), (i64 GPR:$rs2)))]>;

// Shifted-operand arithmetic (introduced by a late peephole pass).
def ADDrr_SH : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2, uimm5:$shamt), "", []>;

def ADDIri : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, uimm12:$imm), "", [(set (i64 GPR:$rd), (add (i64 GPR:$rs1), uimm12:$imm))]>;
def SUBIri : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, uimm12:$imm), "", [(set (i64 GPR:$rd), (sub (i64 GPR:$rs1), uimm12:$imm))]>;
def ANDIri : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, simm12:$imm), "", [(set (i64 GPR:$rd), (and (i64 GPR:$rs1), simm12:$imm))]>;
def ORIri  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, simm12:$imm), "", [(set (i64 GPR:$rd), (or  (i64 GPR:$rs1), simm12:$imm))]>;
def XORIri : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, simm12:$imm), "", [(set (i64 GPR:$rd), (xor (i64 GPR:$rs1), simm12:$imm))]>;

def SLLrr  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", [(set (i64 GPR:$rd), (shl (i64 GPR:$rs1), (i64 GPR:$rs2)))]>;
def SRLrr  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", [(set (i64 GPR:$rd), (srl (i64 GPR:$rs1), (i64 GPR:$rs2)))]>;
def SRArr  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", [(set (i64 GPR:$rd), (sra (i64 GPR:$rs1), (i64 GPR:$rs2)))]>;

def SLLIri : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, uimm5:$shamt), "", [(set (i64 GPR:$rd), (shl (i64 GPR:$rs1), uimm5:$shamt))]>;
def SRLIri : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, uimm5:$shamt), "", [(set (i64 GPR:$rd), (srl (i64 GPR:$rs1), uimm5:$shamt))]>;
def SRAIri : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, uimm5:$shamt), "", [(set (i64 GPR:$rd), (sra (i64 GPR:$rs1), uimm5:$shamt))]>;

// 32-bit ops (selected by patterns)
def ADDWrr  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", []>;
def SUBWrr  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", []>;
def ANDWrr  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", []>;
def ORWrr   : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", []>;
def XORWrr  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", []>;

def ADDWrr_SH : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2, uimm5:$shamt), "", []>;
def SLLWrr  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", []>;
def SRLWrr  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", []>;
def SRAWrr  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", []>;

def ADDIWri : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, uimm12_w:$imm), "", []>;
def SUBIWri : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, uimm12_w:$imm), "", []>;
def ANDIWri : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, simm12_w:$imm), "", []>;
def ORIWri  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, simm12_w:$imm), "", []>;
def XORIWri : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, simm12_w:$imm), "", []>;

def SLLIWri : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, uimm5_w:$shamt), "", []>;
def SRLIWri : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, uimm5_w:$shamt), "", []>;
def SRAIWri : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, uimm5_w:$shamt), "", []>;

// Prefer immediate forms when the RHS is a constant.
def : Pat<(add i32:$a, uimm12_w:$b), (ADDIWri $a, $b)>;
def : Pat<(sub i32:$a, uimm12_w:$b), (SUBIWri $a, $b)>;
def : Pat<(and i32:$a, simm12_w:$b), (ANDIWri $a, $b)>;
def : Pat<(or  i32:$a, simm12_w:$b), (ORIWri  $a, $b)>;
def : Pat<(xor i32:$a, simm12_w:$b), (XORIWri $a, $b)>;

def : Pat<(shl i32:$a, uimm5_w:$b), (SLLIWri $a, $b)>;
def : Pat<(srl i32:$a, uimm5_w:$b), (SRLIWri $a, $b)>;
def : Pat<(sra i32:$a, uimm5_w:$b), (SRAIWri $a, $b)>;

// Reg-reg forms.
def : Pat<(add i32:$a, i32:$b), (ADDWrr $a, $b)>;
def : Pat<(sub i32:$a, i32:$b), (SUBWrr $a, $b)>;
def : Pat<(and i32:$a, i32:$b), (ANDWrr $a, $b)>;
def : Pat<(or  i32:$a, i32:$b), (ORWrr  $a, $b)>;
def : Pat<(xor i32:$a, i32:$b), (XORWrr $a, $b)>;
def : Pat<(shl i32:$a, i32:$b), (SLLWrr $a, $b)>;
def : Pat<(srl i32:$a, i32:$b), (SRLWrr $a, $b)>;
def : Pat<(sra i32:$a, i32:$b), (SRAWrr $a, $b)>;

//===----------------------------------------------------------------------===//
// Memory/Control-flow ops (selected manually in C++)
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Block markers (BlockISA)
//
// These are inserted very late (pre-emit) to model the ISA block boundaries.
// The current bring-up treats them as markers around existing control-flow.
//===----------------------------------------------------------------------===//

let hasSideEffects = 1, isBarrier = 1, Size = 2 in
def CBSTART_STD : LinxISAInst<(outs), (ins i64imm:$brtype), "", []>;

// 32-bit block start forms used by the BlockISA control-flow model.
//
// These are inserted very late (pre-emit) and are not selected by the DAG.
let hasSideEffects = 1, isBarrier = 1, Size = 4 in {
  def BSTART_STD_FALL  : LinxISAInst<(outs), (ins), "", []>;
  def BSTART_STD_DIRECT : LinxISAInst<(outs), (ins BrTarget:$dst), "", []>;
  def BSTART_STD_COND  : LinxISAInst<(outs), (ins BrTarget:$dst), "", []>;
  def BSTART_STD_CALL  : LinxISAInst<(outs), (ins CallTarget:$dst), "", []>;
  def BSTART_STD_IND   : LinxISAInst<(outs), (ins), "", []>;
  def BSTART_STD_ICALL : LinxISAInst<(outs), (ins), "", []>;
  def BSTART_STD_RET   : LinxISAInst<(outs), (ins), "", []>;
}

let hasSideEffects = 1, isBarrier = 1, isTerminator = 1, Size = 4 in
def BSTOP : LinxISAInst<(outs), (ins), "", []>;

// Branch-condition setters used by BlockISA (no explicit branch instructions).
let hasSideEffects = 1, Size = 2 in {
  def CSETC_EQ  : LinxISAInst<(outs), (ins GPR:$rs1, GPR:$rs2), "", []>;
  def CSETC_NE  : LinxISAInst<(outs), (ins GPR:$rs1, GPR:$rs2), "", []>;
  def CSETC_TGT : LinxISAInst<(outs), (ins GPR:$rs), "", []>;
}

// Full-width condition setters. These map to `SETC.*` forms in the ISA spec.
let hasSideEffects = 1, Size = 4 in {
  def SETC_EQ  : LinxISAInst<(outs), (ins GPR:$rs1, GPR:$rs2), "", []>;
  def SETC_NE  : LinxISAInst<(outs), (ins GPR:$rs1, GPR:$rs2), "", []>;
  def SETC_LT  : LinxISAInst<(outs), (ins GPR:$rs1, GPR:$rs2), "", []>;
  def SETC_GE  : LinxISAInst<(outs), (ins GPR:$rs1, GPR:$rs2), "", []>;
  def SETC_LTU : LinxISAInst<(outs), (ins GPR:$rs1, GPR:$rs2), "", []>;
  def SETC_GEU : LinxISAInst<(outs), (ins GPR:$rs1, GPR:$rs2), "", []>;

  def SETC_TGT : LinxISAInst<(outs), (ins GPR:$rs), "", []>;
}

// Set return address for CALL blocks (BlockISA). Writes `ra` (R10) implicitly.
let hasSideEffects = 1, Defs = [R10] in
def SETRET : LinxISAInst<(outs), (ins BrTarget:$dst), "", []>;

let mayLoad = 1 in
def LBI   : LinxISAInst<(outs GPR:$rd), (ins GPR:$base, simm12:$off), "", []>;
let mayLoad = 1 in
def LBUI  : LinxISAInst<(outs GPR:$rd), (ins GPR:$base, simm12:$off), "", []>;
let mayLoad = 1 in
def LHI   : LinxISAInst<(outs GPR:$rd), (ins GPR:$base, simm12:$off), "", []>;
let mayLoad = 1 in
def LHUI  : LinxISAInst<(outs GPR:$rd), (ins GPR:$base, simm12:$off), "", []>;
let mayLoad = 1 in
def LWI   : LinxISAInst<(outs GPR:$rd), (ins GPR:$base, simm12:$off), "", []>;
let mayLoad = 1 in
def LWUI  : LinxISAInst<(outs GPR:$rd), (ins GPR:$base, simm12:$off), "", []>;
let mayLoad = 1 in
def LDI   : LinxISAInst<(outs GPR:$rd), (ins GPR:$base, simm12:$off), "", []>;

// Load register offset: base + (idx << shamt).
let mayLoad = 1 in {
  def LB   : LinxISAInst<(outs GPR:$rd), (ins GPR:$base, GPR:$idx, uimm5:$shamt), "", []>;
  def LBU  : LinxISAInst<(outs GPR:$rd), (ins GPR:$base, GPR:$idx, uimm5:$shamt), "", []>;
  def LH   : LinxISAInst<(outs GPR:$rd), (ins GPR:$base, GPR:$idx, uimm5:$shamt), "", []>;
  def LHU  : LinxISAInst<(outs GPR:$rd), (ins GPR:$base, GPR:$idx, uimm5:$shamt), "", []>;
  def LW   : LinxISAInst<(outs GPR:$rd), (ins GPR:$base, GPR:$idx, uimm5:$shamt), "", []>;
  def LWU  : LinxISAInst<(outs GPR:$rd), (ins GPR:$base, GPR:$idx, uimm5:$shamt), "", []>;
  def LD   : LinxISAInst<(outs GPR:$rd), (ins GPR:$base, GPR:$idx, uimm5:$shamt), "", []>;
}

let mayStore = 1 in
def SBI   : LinxISAInst<(outs), (ins GPR:$rs, GPR:$base, simm12:$off), "", []>;
let mayStore = 1 in
def SHI   : LinxISAInst<(outs), (ins GPR:$rs, GPR:$base, simm12:$off), "", []>;
let mayStore = 1 in
def SWI   : LinxISAInst<(outs), (ins GPR:$rs, GPR:$base, simm12:$off), "", []>;
let mayStore = 1 in
def SDI   : LinxISAInst<(outs), (ins GPR:$rs, GPR:$base, simm12:$off), "", []>;

// Store register offset: base + (idx << scale). The scale is implied by the
// store width (SB=<<0, SH=<<1, SW=<<2, SD=<<3).
let mayStore = 1 in {
  def SB   : LinxISAInst<(outs), (ins GPR:$rs, GPR:$base, GPR:$idx), "", []>;
  def SH   : LinxISAInst<(outs), (ins GPR:$rs, GPR:$base, GPR:$idx), "", []>;
  def SW   : LinxISAInst<(outs), (ins GPR:$rs, GPR:$base, GPR:$idx), "", []>;
  def SD   : LinxISAInst<(outs), (ins GPR:$rs, GPR:$base, GPR:$idx), "", []>;
}

def LUI   : LinxISAInst<(outs GPR:$rd), (ins imm20:$imm), "", []>;
let Size = 6 in
def HLLUI : LinxISAInst<(outs GPR:$rd), (ins imm32:$imm), "", []>;
def ADDTPC : LinxISAInst<(outs GPR:$rd), (ins imm20:$imm), "", []>;

let isBranch = 1, isBarrier = 1, isTerminator = 1 in
def JUMP : LinxISAInst<(outs), (ins BrTarget:$dst), "", []>;

let isBranch = 1, isTerminator = 1 in
def BEQ  : LinxISAInst<(outs), (ins GPR:$rs1, GPR:$rs2, BrTarget:$dst), "", []>;
let isBranch = 1, isTerminator = 1 in
def BNE  : LinxISAInst<(outs), (ins GPR:$rs1, GPR:$rs2, BrTarget:$dst), "", []>;
let isBranch = 1, isTerminator = 1 in
def BLT  : LinxISAInst<(outs), (ins GPR:$rs1, GPR:$rs2, BrTarget:$dst), "", []>;
let isBranch = 1, isTerminator = 1 in
def BGE  : LinxISAInst<(outs), (ins GPR:$rs1, GPR:$rs2, BrTarget:$dst), "", []>;
let isBranch = 1, isTerminator = 1 in
def BLTU : LinxISAInst<(outs), (ins GPR:$rs1, GPR:$rs2, BrTarget:$dst), "", []>;
let isBranch = 1, isTerminator = 1 in
def BGEU : LinxISAInst<(outs), (ins GPR:$rs1, GPR:$rs2, BrTarget:$dst), "", []>;

let isBranch = 1, isBarrier = 1, isIndirectBranch = 1, isTerminator = 1 in
def JR : LinxISAInst<(outs), (ins GPR:$rs), "", []>;

// SetCC (returns 0/1)
def CMPEQ  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", []>;
def CMPNE  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", []>;
def CMPLT  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", []>;
def CMPGE  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", []>;
def CMPLTU : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", []>;
def CMPGEU : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", []>;

// Conditional select. SrcP is a 0/1 predicate value.
let isSelect = 1 in
def CSELrrr : LinxISAInst<(outs GPR:$rd),
                          (ins GPR:$pred, GPR:$src_true, GPR:$src_false),
                          "", []>;

// Multiply/divide/remainder (selected by patterns)
def MULrr  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", [(set (i64 GPR:$rd), (mul (i64 GPR:$rs1), (i64 GPR:$rs2)))]>;
def DIVrr  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", [(set (i64 GPR:$rd), (sdiv (i64 GPR:$rs1), (i64 GPR:$rs2)))]>;
def DIVUrr : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", [(set (i64 GPR:$rd), (udiv (i64 GPR:$rs1), (i64 GPR:$rs2)))]>;
def REMrr  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", [(set (i64 GPR:$rd), (srem (i64 GPR:$rs1), (i64 GPR:$rs2)))]>;
def REMUrr : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", [(set (i64 GPR:$rd), (urem (i64 GPR:$rs1), (i64 GPR:$rs2)))]>;

def MULWrr  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", []>;
def DIVWrr  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", []>;
def DIVUWrr : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", []>;
def REMWrr  : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", []>;
def REMUWrr : LinxISAInst<(outs GPR:$rd), (ins GPR:$rs1, GPR:$rs2), "", []>;

def : Pat<(mul i32:$a, i32:$b), (MULWrr $a, $b)>;
def : Pat<(sdiv i32:$a, i32:$b), (DIVWrr $a, $b)>;
def : Pat<(udiv i32:$a, i32:$b), (DIVUWrr $a, $b)>;
def : Pat<(srem i32:$a, i32:$b), (REMWrr $a, $b)>;
def : Pat<(urem i32:$a, i32:$b), (REMUWrr $a, $b)>;
