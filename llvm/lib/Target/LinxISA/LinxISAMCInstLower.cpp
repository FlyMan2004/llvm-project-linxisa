//===-- LinxISAMCInstLower.cpp - Lower LinxISA MachineInstr to MCInst ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LinxISAMCInstLower.h"
#include "LinxISA.h"
#include "MCTargetDesc/LinxISAMCTargetDesc.h"
#include "MCTargetDesc/LinxISAOpcodeTables.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/Twine.h"
#include "llvm/CodeGen/AsmPrinter.h"
#include "llvm/CodeGen/MachineBasicBlock.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineOperand.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/CodeGen/TargetOpcodes.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include <string>
#include <vector>

using namespace llvm;

static unsigned findSpecOpcode(StringRef Mnemonic, unsigned LengthBits,
                               unsigned FieldCount) {
  for (unsigned Opc = 0; Opc < linxisa_inst_forms_count; ++Opc) {
    const linxisa_inst_form &F = linxisa_inst_forms[Opc];
    if (unsigned(F.length_bits) != LengthBits)
      continue;
    if (!F.mnemonic || Mnemonic != StringRef(F.mnemonic))
      continue;
    if (FieldCount && unsigned(F.field_count) != FieldCount)
      continue;
    return Opc;
  }

  SmallString<64> Msg;
  raw_svector_ostream OS(Msg);
  OS << "Linx: missing spec opcode for " << Mnemonic << " (" << LengthBits
     << "b)";
  report_fatal_error(OS.str());
}

static unsigned getSpecOpcode(StringRef Mnemonic, unsigned LengthBits,
                              unsigned FieldCount) {
  // Memoize by a stable key to keep lowering cheap.
  struct CacheEntry {
    std::string Key;
    unsigned Opcode;
  };
  static std::vector<CacheEntry> Cache;

  std::string Key =
      (Mnemonic + "/" + Twine(LengthBits) + "/" + Twine(FieldCount)).str();
  for (const CacheEntry &E : Cache)
    if (E.Key == Key)
      return E.Opcode;

  unsigned Opc = findSpecOpcode(Mnemonic, LengthBits, FieldCount);
  Cache.push_back({Key, Opc});
  return Opc;
}

static unsigned findSpecOpcodeByAsmFmt(StringRef AsmFmt, unsigned LengthBits) {
  for (unsigned Opc = 0; Opc < linxisa_inst_forms_count; ++Opc) {
    const linxisa_inst_form &F = linxisa_inst_forms[Opc];
    if (unsigned(F.length_bits) != LengthBits)
      continue;
    if (!F.asm_fmt || AsmFmt != StringRef(F.asm_fmt))
      continue;
    return Opc;
  }

  SmallString<96> Msg;
  raw_svector_ostream OS(Msg);
  OS << "Linx: missing spec opcode for asm fmt '" << AsmFmt << "' (" << LengthBits
     << "b)";
  report_fatal_error(OS.str());
}

static unsigned getSpecOpcodeByAsmFmt(StringRef AsmFmt, unsigned LengthBits) {
  struct CacheEntry {
    std::string Key;
    unsigned Opcode;
  };
  static std::vector<CacheEntry> Cache;

  std::string Key = (AsmFmt + "/" + Twine(LengthBits)).str();
  for (const CacheEntry &E : Cache)
    if (E.Key == Key)
      return E.Opcode;

  unsigned Opc = findSpecOpcodeByAsmFmt(AsmFmt, LengthBits);
  Cache.push_back({Key, Opc});
  return Opc;
}

unsigned LinxISAMCInstLower::getReg5Encoding(unsigned Reg) const {
  return TRI.getEncodingValue(Reg) & 0x1F;
}

static const MCExpr *withOffset(const MCExpr *Expr, int64_t Offset,
                                MCContext &Ctx) {
  if (!Offset)
    return Expr;
  return MCBinaryExpr::createAdd(Expr, MCConstantExpr::create(Offset, Ctx),
                                 Ctx);
}

bool LinxISAMCInstLower::lowerOperand(const MachineOperand &MO,
                                      MCOperand &OutOp) const {
  switch (MO.getType()) {
  case MachineOperand::MO_Register:
    if (MO.isImplicit())
      return false;
    OutOp = MCOperand::createImm(getReg5Encoding(MO.getReg()));
    return true;
  case MachineOperand::MO_Immediate:
    OutOp = MCOperand::createImm(MO.getImm());
    return true;
  case MachineOperand::MO_MachineBasicBlock: {
    const MCExpr *Expr =
        MCSymbolRefExpr::create(MO.getMBB()->getSymbol(), Ctx);
    OutOp = MCOperand::createExpr(Expr);
    return true;
  }
  case MachineOperand::MO_GlobalAddress: {
    const MCExpr *Expr = MCSymbolRefExpr::create(Printer.getSymbol(MO.getGlobal()), Ctx);
    Expr = withOffset(Expr, MO.getOffset(), Ctx);
    OutOp = MCOperand::createExpr(Expr);
    return true;
  }
  case MachineOperand::MO_ExternalSymbol: {
    const MCExpr *Expr =
        MCSymbolRefExpr::create(Printer.GetExternalSymbolSymbol(MO.getSymbolName()), Ctx);
    Expr = withOffset(Expr, MO.getOffset(), Ctx);
    OutOp = MCOperand::createExpr(Expr);
    return true;
  }
  case MachineOperand::MO_JumpTableIndex: {
    const MCExpr *Expr =
        MCSymbolRefExpr::create(Printer.GetJTISymbol(MO.getIndex()), Ctx);
    Expr = withOffset(Expr, MO.getOffset(), Ctx);
    OutOp = MCOperand::createExpr(Expr);
    return true;
  }
  case MachineOperand::MO_RegisterMask:
    return false;
  default:
    return false;
  }
}

void LinxISAMCInstLower::Lower(const MachineInstr *MI, MCInst &OutMI) const {
  OutMI.clear();

  const unsigned Opc = MI->getOpcode();
  auto R = [&](unsigned OpNo) -> int64_t {
    if (OpNo >= MI->getNumOperands()) {
      MI->print(errs());
      report_fatal_error("Linx: missing register operand in MC lowering");
    }
    const MachineOperand &MO = MI->getOperand(OpNo);
    if (!MO.isReg()) {
      MI->print(errs());
      report_fatal_error("Linx: expected register operand in MC lowering");
    }
    Register Reg = MO.getReg();
    if (!Reg.isPhysical()) {
      MI->print(errs());
      report_fatal_error("Linx: expected physical register operand in MC lowering");
    }
    return static_cast<int64_t>(getReg5Encoding(Reg));
  };
  auto I = [&](unsigned OpNo) -> int64_t {
    if (OpNo >= MI->getNumOperands()) {
      MI->print(errs());
      report_fatal_error("Linx: missing immediate operand in MC lowering");
    }
    const MachineOperand &MO = MI->getOperand(OpNo);
    if (!MO.isImm()) {
      MI->print(errs());
      report_fatal_error("Linx: expected immediate operand in MC lowering");
    }
    return MO.getImm();
  };

  auto lowerBranchTarget = [&](unsigned OpNo) -> MCOperand {
    const MachineOperand &MO = MI->getOperand(OpNo);
    MCOperand Op;
    if (!lowerOperand(MO, Op) || !Op.isExpr()) {
      MI->print(errs());
      report_fatal_error("Linx: expected branch target expression operand");
    }
    return Op;
  };

  switch (Opc) {
  case LinxISA::CBSTART_STD: {
    // Compressed block start marker: `C.BSTART.STD BrType`.
    OutMI.setOpcode(getSpecOpcode("C.BSTART.STD", /*LengthBits=*/16, /*Fields=*/1));
    OutMI.addOperand(MCOperand::createImm(I(0))); // BrType
    return;
  }

  case LinxISA::BSTART_STD_FALL: {
    OutMI.setOpcode(getSpecOpcodeByAsmFmt("BSTART.STD FALL<, fixup_label>", /*LengthBits=*/32));
    return;
  }
  case LinxISA::BSTART_STD_DIRECT: {
    // Prefer the compressed form; the assembler can relax to wider forms if
    // the target is out of range.
    OutMI.setOpcode(getSpecOpcodeByAsmFmt("C.BSTART DIRECT, label", /*LengthBits=*/16));
    OutMI.addOperand(lowerBranchTarget(0));
    return;
  }
  case LinxISA::BSTART_STD_COND: {
    // Prefer the compressed form; the assembler can relax to wider forms if
    // the target is out of range.
    OutMI.setOpcode(getSpecOpcodeByAsmFmt("C.BSTART COND,  label", /*LengthBits=*/16));
    OutMI.addOperand(lowerBranchTarget(0));
    return;
  }
  case LinxISA::BSTART_STD_CALL: {
    OutMI.setOpcode(getSpecOpcodeByAsmFmt("BSTART.STD CALL, <label>", /*LengthBits=*/32));
    OutMI.addOperand(lowerBranchTarget(0));
    return;
  }
  case LinxISA::BSTART_STD_IND: {
    OutMI.setOpcode(getSpecOpcodeByAsmFmt("BSTART.STD IND", /*LengthBits=*/32));
    return;
  }
  case LinxISA::BSTART_STD_ICALL: {
    OutMI.setOpcode(getSpecOpcodeByAsmFmt("BSTART.STD ICALL", /*LengthBits=*/32));
    return;
  }
  case LinxISA::BSTART_STD_RET: {
    OutMI.setOpcode(getSpecOpcodeByAsmFmt("BSTART.STD RET", /*LengthBits=*/32));
    return;
  }

  case LinxISA::BSTOP: {
    // Block stop marker: prefer the compressed form `C.BSTOP` (16-bit, 0x0000).
    OutMI.setOpcode(getSpecOpcode("C.BSTOP", /*LengthBits=*/16, /*Fields=*/0));
    return;
  }

  case LinxISA::CSETC_EQ:
  case LinxISA::CSETC_NE: {
    StringRef Mnem;
    switch (Opc) {
    case LinxISA::CSETC_EQ:
      Mnem = "C.SETC.EQ";
      break;
    case LinxISA::CSETC_NE:
      Mnem = "C.SETC.NE";
      break;
    default:
      llvm_unreachable("Unexpected opcode");
    }

    OutMI.setOpcode(getSpecOpcode(Mnem, /*LengthBits=*/16, /*Fields=*/2));
    OutMI.addOperand(MCOperand::createImm(R(0))); // SrcL
    OutMI.addOperand(MCOperand::createImm(R(1))); // SrcR
    return;
  }

  case LinxISA::CSETC_TGT: {
    OutMI.setOpcode(getSpecOpcode("C.SETC.TGT", /*LengthBits=*/16, /*Fields=*/1));
    OutMI.addOperand(MCOperand::createImm(R(0))); // SrcL
    return;
  }

  case LinxISA::SETC_EQ:
  case LinxISA::SETC_NE:
  case LinxISA::SETC_LT:
  case LinxISA::SETC_GE:
  case LinxISA::SETC_LTU:
  case LinxISA::SETC_GEU: {
    StringRef Mnem;
    switch (Opc) {
    case LinxISA::SETC_EQ:
      Mnem = "SETC.EQ";
      break;
    case LinxISA::SETC_NE:
      Mnem = "SETC.NE";
      break;
    case LinxISA::SETC_LT:
      Mnem = "SETC.LT";
      break;
    case LinxISA::SETC_GE:
      Mnem = "SETC.GE";
      break;
    case LinxISA::SETC_LTU:
      Mnem = "SETC.LTU";
      break;
    case LinxISA::SETC_GEU:
      Mnem = "SETC.GEU";
      break;
    default:
      llvm_unreachable("Unexpected opcode");
    }

    OutMI.setOpcode(getSpecOpcode(Mnem, /*LengthBits=*/32, /*Fields=*/3));
    OutMI.addOperand(MCOperand::createImm(R(0))); // SrcL
    OutMI.addOperand(MCOperand::createImm(R(1))); // SrcR
    OutMI.addOperand(MCOperand::createImm(3));    // SrcRType (default: no modifier)
    return;
  }

  case LinxISA::SETC_TGT: {
    OutMI.setOpcode(getSpecOpcode("SETC.TGT", /*LengthBits=*/32, /*Fields=*/1));
    OutMI.addOperand(MCOperand::createImm(R(0))); // SrcL
    return;
  }

  case LinxISA::SETRET: {
    // Prefer the compressed form; the assembler can relax to wider forms if
    // the target is out of range.
    OutMI.setOpcode(getSpecOpcode("C.SETRET", /*LengthBits=*/16, /*Fields=*/1));
    OutMI.addOperand(lowerBranchTarget(0));
    return;
  }

  case TargetOpcode::COPY: {
    // Prefer the compressed form.
    OutMI.setOpcode(getSpecOpcode("C.MOVR", /*LengthBits=*/16, /*Fields=*/2));
    OutMI.addOperand(MCOperand::createImm(R(0))); // RegDst
    OutMI.addOperand(MCOperand::createImm(R(1))); // SrcL
    return;
  }

  case LinxISA::ADDrr_SH:
  case LinxISA::ADDWrr_SH: {
    const StringRef Mnem = (Opc == LinxISA::ADDrr_SH) ? "ADD" : "ADDW";
    OutMI.setOpcode(getSpecOpcode(Mnem, /*LengthBits=*/32, /*Fields=*/5));
    OutMI.addOperand(MCOperand::createImm(R(0))); // RegDst
    OutMI.addOperand(MCOperand::createImm(R(1))); // SrcL
    OutMI.addOperand(MCOperand::createImm(R(2))); // SrcR
    OutMI.addOperand(MCOperand::createImm(3));    // SrcRType (default: no modifier)
    OutMI.addOperand(MCOperand::createImm(I(3))); // shamt
    return;
  }

  case LinxISA::ADDrr:
  case LinxISA::SUBrr:
  case LinxISA::ANDrr:
  case LinxISA::ORrr:
  case LinxISA::XORrr:
  case LinxISA::ADDWrr:
  case LinxISA::SUBWrr:
  case LinxISA::ANDWrr:
  case LinxISA::ORWrr:
  case LinxISA::XORWrr: {
    // Prefer compressed arithmetic when the destination is the T-hand output.
    const Register DstReg = MI->getOperand(0).getReg();
    if (DstReg == LinxISA::U4) {
      StringRef CMnem;
      switch (Opc) {
      case LinxISA::ADDrr:
        CMnem = "C.ADD";
        break;
      case LinxISA::SUBrr:
        CMnem = "C.SUB";
        break;
      case LinxISA::ANDrr:
        CMnem = "C.AND";
        break;
      case LinxISA::ORrr:
        CMnem = "C.OR";
        break;
      default:
        break;
      }

      if (!CMnem.empty()) {
        OutMI.setOpcode(getSpecOpcode(CMnem, /*LengthBits=*/16, /*Fields=*/2));
        OutMI.addOperand(MCOperand::createImm(R(1))); // SrcL
        OutMI.addOperand(MCOperand::createImm(R(2))); // SrcR
        return;
      }
    }

    StringRef Mnem;
    switch (Opc) {
    case LinxISA::ADDrr:
      Mnem = "ADD";
      break;
    case LinxISA::SUBrr:
      Mnem = "SUB";
      break;
    case LinxISA::ANDrr:
      Mnem = "AND";
      break;
    case LinxISA::ORrr:
      Mnem = "OR";
      break;
    case LinxISA::XORrr:
      Mnem = "XOR";
      break;
    case LinxISA::ADDWrr:
      Mnem = "ADDW";
      break;
    case LinxISA::SUBWrr:
      Mnem = "SUBW";
      break;
    case LinxISA::ANDWrr:
      Mnem = "ANDW";
      break;
    case LinxISA::ORWrr:
      Mnem = "ORW";
      break;
    case LinxISA::XORWrr:
      Mnem = "XORW";
      break;
    default:
      llvm_unreachable("Unexpected opcode");
    }

    OutMI.setOpcode(getSpecOpcode(Mnem, /*LengthBits=*/32, /*Fields=*/5));
    OutMI.addOperand(MCOperand::createImm(R(0))); // RegDst
    OutMI.addOperand(MCOperand::createImm(R(1))); // SrcL
    OutMI.addOperand(MCOperand::createImm(R(2))); // SrcR
    OutMI.addOperand(MCOperand::createImm(3));    // SrcRType (default: no modifier)
    OutMI.addOperand(MCOperand::createImm(0));    // shamt
    return;
  }

  case LinxISA::ADDIri:
  case LinxISA::SUBIri:
  case LinxISA::ANDIri:
  case LinxISA::ORIri:
  case LinxISA::XORIri:
  case LinxISA::ADDIWri:
  case LinxISA::SUBIWri:
  case LinxISA::ANDIWri:
  case LinxISA::ORIWri:
  case LinxISA::XORIWri: {
    StringRef Mnem;
    switch (Opc) {
    case LinxISA::ADDIri:
      Mnem = "ADDI";
      break;
    case LinxISA::SUBIri:
      Mnem = "SUBI";
      break;
    case LinxISA::ANDIri:
      Mnem = "ANDI";
      break;
    case LinxISA::ORIri:
      Mnem = "ORI";
      break;
    case LinxISA::XORIri:
      Mnem = "XORI";
      break;
    case LinxISA::ADDIWri:
      Mnem = "ADDIW";
      break;
    case LinxISA::SUBIWri:
      Mnem = "SUBIW";
      break;
    case LinxISA::ANDIWri:
      Mnem = "ANDIW";
      break;
    case LinxISA::ORIWri:
      Mnem = "ORIW";
      break;
    case LinxISA::XORIWri:
      Mnem = "XORIW";
      break;
    default:
      llvm_unreachable("Unexpected opcode");
    }

    const MachineOperand &Op2 = MI->getOperand(2);
    if (Op2.isImm()) {
      const Register DstReg = MI->getOperand(0).getReg();
      const Register SrcReg = MI->getOperand(1).getReg();
      const int64_t Imm = I(2);

      // Prefer C.MOVR for common reg copies.
      if ((Opc == LinxISA::ADDIri || Opc == LinxISA::SUBIri ||
           Opc == LinxISA::ADDIWri || Opc == LinxISA::SUBIWri) &&
          Imm == 0) {
        OutMI.setOpcode(getSpecOpcode("C.MOVR", /*LengthBits=*/16, /*Fields=*/2));
        OutMI.addOperand(MCOperand::createImm(R(0))); // RegDst
        OutMI.addOperand(MCOperand::createImm(R(1))); // SrcL
        return;
      }

      // Prefer C.MOVI for small immediates materialized from zero.
      if ((Opc == LinxISA::ADDIri || Opc == LinxISA::SUBIri ||
           Opc == LinxISA::ADDIWri || Opc == LinxISA::SUBIWri) &&
          SrcReg == LinxISA::R0 && DstReg != LinxISA::R10) {
        int64_t SImm = (Opc == LinxISA::SUBIri || Opc == LinxISA::SUBIWri)
                           ? -Imm
                           : Imm;
        if (isInt<5>(SImm)) {
          OutMI.setOpcode(getSpecOpcode("C.MOVI", /*LengthBits=*/16, /*Fields=*/2));
          OutMI.addOperand(MCOperand::createImm(R(0))); // RegDst
          OutMI.addOperand(MCOperand::createImm(SImm)); // simm5
          return;
        }
      }

      // Prefer C.ADDI when writing the T-hand output.
      if ((Opc == LinxISA::ADDIri || Opc == LinxISA::SUBIri) &&
          DstReg == LinxISA::U4) {
        int64_t SImm = (Opc == LinxISA::SUBIri) ? -Imm : Imm;
        if (isInt<5>(SImm)) {
          OutMI.setOpcode(getSpecOpcode("C.ADDI", /*LengthBits=*/16, /*Fields=*/2));
          OutMI.addOperand(MCOperand::createImm(R(1))); // SrcL
          OutMI.addOperand(MCOperand::createImm(SImm)); // simm5
          return;
        }
      }

      OutMI.setOpcode(getSpecOpcode(Mnem, /*LengthBits=*/32, /*Fields=*/3));
      OutMI.addOperand(MCOperand::createImm(R(0))); // RegDst
      OutMI.addOperand(MCOperand::createImm(R(1))); // SrcL
      OutMI.addOperand(MCOperand::createImm(I(2))); // imm12
      return;
    }

    if (!Op2.isReg()) {
      MI->print(errs());
      report_fatal_error("Linx: expected imm/reg operand for *I instruction");
    }

    // If a constant got materialized into a register late, fall back to the
    // corresponding reg-reg instruction.
    StringRef RegMnem;
    switch (Opc) {
    case LinxISA::ADDIri:
      RegMnem = "ADD";
      break;
    case LinxISA::SUBIri:
      RegMnem = "SUB";
      break;
    case LinxISA::ANDIri:
      RegMnem = "AND";
      break;
    case LinxISA::ORIri:
      RegMnem = "OR";
      break;
    case LinxISA::XORIri:
      RegMnem = "XOR";
      break;
    case LinxISA::ADDIWri:
      RegMnem = "ADDW";
      break;
    case LinxISA::SUBIWri:
      RegMnem = "SUBW";
      break;
    case LinxISA::ANDIWri:
      RegMnem = "ANDW";
      break;
    case LinxISA::ORIWri:
      RegMnem = "ORW";
      break;
    case LinxISA::XORIWri:
      RegMnem = "XORW";
      break;
    default:
      llvm_unreachable("Unexpected opcode");
    }

    OutMI.setOpcode(getSpecOpcode(RegMnem, /*LengthBits=*/32, /*Fields=*/5));
    OutMI.addOperand(MCOperand::createImm(R(0))); // RegDst
    OutMI.addOperand(MCOperand::createImm(R(1))); // SrcL
    OutMI.addOperand(MCOperand::createImm(R(2))); // SrcR
    OutMI.addOperand(MCOperand::createImm(3));    // SrcRType (default: no modifier)
    OutMI.addOperand(MCOperand::createImm(0));    // shamt
    return;
  }

  case LinxISA::SLLrr:
  case LinxISA::SRLrr:
  case LinxISA::SRArr:
  case LinxISA::SLLWrr:
  case LinxISA::SRLWrr:
  case LinxISA::SRAWrr:
  case LinxISA::MULrr:
  case LinxISA::DIVrr:
  case LinxISA::DIVUrr:
  case LinxISA::REMrr:
  case LinxISA::REMUrr:
  case LinxISA::MULWrr:
  case LinxISA::DIVWrr:
  case LinxISA::DIVUWrr:
  case LinxISA::REMWrr:
  case LinxISA::REMUWrr: {
    StringRef Mnem;
    switch (Opc) {
    case LinxISA::SLLrr:
      Mnem = "SLL";
      break;
    case LinxISA::SRLrr:
      Mnem = "SRL";
      break;
    case LinxISA::SRArr:
      Mnem = "SRA";
      break;
    case LinxISA::SLLWrr:
      Mnem = "SLLW";
      break;
    case LinxISA::SRLWrr:
      Mnem = "SRLW";
      break;
    case LinxISA::SRAWrr:
      Mnem = "SRAW";
      break;
    case LinxISA::MULrr:
      Mnem = "MUL";
      break;
    case LinxISA::DIVrr:
      Mnem = "DIV";
      break;
    case LinxISA::DIVUrr:
      Mnem = "DIVU";
      break;
    case LinxISA::REMrr:
      Mnem = "REM";
      break;
    case LinxISA::REMUrr:
      Mnem = "REMU";
      break;
    case LinxISA::MULWrr:
      Mnem = "MULW";
      break;
    case LinxISA::DIVWrr:
      Mnem = "DIVW";
      break;
    case LinxISA::DIVUWrr:
      Mnem = "DIVUW";
      break;
    case LinxISA::REMWrr:
      Mnem = "REMW";
      break;
    case LinxISA::REMUWrr:
      Mnem = "REMUW";
      break;
    default:
      llvm_unreachable("Unexpected opcode");
    }

    OutMI.setOpcode(getSpecOpcode(Mnem, /*LengthBits=*/32, /*Fields=*/3));
    OutMI.addOperand(MCOperand::createImm(R(0))); // RegDst
    OutMI.addOperand(MCOperand::createImm(R(1))); // SrcL
    OutMI.addOperand(MCOperand::createImm(R(2))); // SrcR
    return;
  }

  case LinxISA::SLLIri:
  case LinxISA::SRLIri:
  case LinxISA::SRAIri:
  case LinxISA::SLLIWri:
  case LinxISA::SRLIWri:
  case LinxISA::SRAIWri: {
    StringRef Mnem;
    switch (Opc) {
    case LinxISA::SLLIri:
      Mnem = "SLLI";
      break;
    case LinxISA::SRLIri:
      Mnem = "SRLI";
      break;
    case LinxISA::SRAIri:
      Mnem = "SRAI";
      break;
    case LinxISA::SLLIWri:
      Mnem = "SLLIW";
      break;
    case LinxISA::SRLIWri:
      Mnem = "SRLIW";
      break;
    case LinxISA::SRAIWri:
      Mnem = "SRAIW";
      break;
    default:
      llvm_unreachable("Unexpected opcode");
    }

    const MachineOperand &Op2 = MI->getOperand(2);
    if (Op2.isImm()) {
      OutMI.setOpcode(getSpecOpcode(Mnem, /*LengthBits=*/32, /*Fields=*/3));
      OutMI.addOperand(MCOperand::createImm(R(0))); // RegDst
      OutMI.addOperand(MCOperand::createImm(R(1))); // SrcL
      OutMI.addOperand(MCOperand::createImm(I(2))); // shamt
      return;
    }

    if (!Op2.isReg()) {
      MI->print(errs());
      report_fatal_error("Linx: expected imm/reg operand for *I shift instruction");
    }

    // If the shift amount got materialized into a register, fall back to the
    // corresponding reg-reg shift instruction.
    StringRef RegMnem;
    switch (Opc) {
    case LinxISA::SLLIri:
      RegMnem = "SLL";
      break;
    case LinxISA::SRLIri:
      RegMnem = "SRL";
      break;
    case LinxISA::SRAIri:
      RegMnem = "SRA";
      break;
    case LinxISA::SLLIWri:
      RegMnem = "SLLW";
      break;
    case LinxISA::SRLIWri:
      RegMnem = "SRLW";
      break;
    case LinxISA::SRAIWri:
      RegMnem = "SRAW";
      break;
    default:
      llvm_unreachable("Unexpected opcode");
    }

    OutMI.setOpcode(getSpecOpcode(RegMnem, /*LengthBits=*/32, /*Fields=*/3));
    OutMI.addOperand(MCOperand::createImm(R(0))); // RegDst
    OutMI.addOperand(MCOperand::createImm(R(1))); // SrcL
    OutMI.addOperand(MCOperand::createImm(R(2))); // SrcR
    return;
  }

  case LinxISA::LUI: {
    OutMI.setOpcode(getSpecOpcode("LUI", /*LengthBits=*/32, /*Fields=*/2));
    OutMI.addOperand(MCOperand::createImm(R(0))); // RegDst
    OutMI.addOperand(MCOperand::createImm(I(1))); // imm20
    return;
  }

  case LinxISA::HLLUI: {
    OutMI.setOpcode(getSpecOpcode("HL.LUI", /*LengthBits=*/48, /*Fields=*/2));
    OutMI.addOperand(MCOperand::createImm(R(0))); // RegDst
    OutMI.addOperand(MCOperand::createImm(I(1))); // imm
    return;
  }

  case LinxISA::ADDTPC: {
    // ADDTPC is used for PC-relative addressing of global symbols.
    // Format: ADDTPC rd, imm20  (rd = PC + sext(imm20))
    OutMI.setOpcode(getSpecOpcode("ADDTPC", /*LengthBits=*/32, /*Fields=*/2));
    OutMI.addOperand(MCOperand::createImm(R(0))); // RegDst

    // The second operand can be an immediate or a global address expression
    const MachineOperand &MO = MI->getOperand(1);
    MCOperand Op;
    if (lowerOperand(MO, Op)) {
      OutMI.addOperand(Op);
    } else {
      report_fatal_error("Linx ADDTPC: failed to lower operand");
    }
    return;
  }

  case LinxISA::LBI:
  case LinxISA::LBUI:
  case LinxISA::LHI:
  case LinxISA::LHUI:
  case LinxISA::LWI:
  case LinxISA::LWUI:
  case LinxISA::LDI: {
    StringRef Mnem;
    switch (Opc) {
    case LinxISA::LBI:
      Mnem = "LBI";
      break;
    case LinxISA::LBUI:
      Mnem = "LBUI";
      break;
    case LinxISA::LHI:
      Mnem = "LHI";
      break;
    case LinxISA::LHUI:
      Mnem = "LHUI";
      break;
    case LinxISA::LWI:
      Mnem = "LWI";
      break;
    case LinxISA::LWUI:
      Mnem = "LWUI";
      break;
    case LinxISA::LDI:
      Mnem = "LDI";
      break;
    default:
      llvm_unreachable("Unexpected opcode");
    }

    const Register DstReg = MI->getOperand(0).getReg();
    if (DstReg == LinxISA::U4) {
      if (Opc == LinxISA::LWI && isInt<5>(I(2))) {
        OutMI.setOpcode(getSpecOpcode("C.LWI", /*LengthBits=*/16, /*Fields=*/2));
        OutMI.addOperand(MCOperand::createImm(R(1))); // SrcL (base)
        OutMI.addOperand(MCOperand::createImm(I(2))); // simm5 (scaled)
        return;
      }
      if (Opc == LinxISA::LDI && isInt<5>(I(2))) {
        OutMI.setOpcode(getSpecOpcode("C.LDI", /*LengthBits=*/16, /*Fields=*/2));
        OutMI.addOperand(MCOperand::createImm(R(1))); // SrcL (base)
        OutMI.addOperand(MCOperand::createImm(I(2))); // simm5 (scaled)
        return;
      }
    }

    OutMI.setOpcode(getSpecOpcode(Mnem, /*LengthBits=*/32, /*Fields=*/3));
    OutMI.addOperand(MCOperand::createImm(R(0))); // RegDst
    OutMI.addOperand(MCOperand::createImm(R(1))); // SrcL (base)
    OutMI.addOperand(MCOperand::createImm(I(2))); // simm12 (scaled)
    return;
  }

  case LinxISA::LB:
  case LinxISA::LBU:
  case LinxISA::LH:
  case LinxISA::LHU:
  case LinxISA::LW:
  case LinxISA::LWU:
  case LinxISA::LD: {
    StringRef Mnem;
    switch (Opc) {
    case LinxISA::LB:
      Mnem = "LB";
      break;
    case LinxISA::LBU:
      Mnem = "LBU";
      break;
    case LinxISA::LH:
      Mnem = "LH";
      break;
    case LinxISA::LHU:
      Mnem = "LHU";
      break;
    case LinxISA::LW:
      Mnem = "LW";
      break;
    case LinxISA::LWU:
      Mnem = "LWU";
      break;
    case LinxISA::LD:
      Mnem = "LD";
      break;
    default:
      llvm_unreachable("Unexpected opcode");
    }

    OutMI.setOpcode(getSpecOpcode(Mnem, /*LengthBits=*/32, /*Fields=*/5));
    OutMI.addOperand(MCOperand::createImm(R(0))); // RegDst
    OutMI.addOperand(MCOperand::createImm(R(1))); // SrcL (base)
    OutMI.addOperand(MCOperand::createImm(R(2))); // SrcR (index)
    OutMI.addOperand(MCOperand::createImm(3));    // SrcRType (default: no modifier)
    OutMI.addOperand(MCOperand::createImm(I(3))); // shamt
    return;
  }

  case LinxISA::SBI:
  case LinxISA::SHI:
  case LinxISA::SWI:
  case LinxISA::SDI: {
    StringRef Mnem;
    switch (Opc) {
    case LinxISA::SBI:
      Mnem = "SBI";
      break;
    case LinxISA::SHI:
      Mnem = "SHI";
      break;
    case LinxISA::SWI:
      Mnem = "SWI";
      break;
    case LinxISA::SDI:
      Mnem = "SDI";
      break;
    default:
      llvm_unreachable("Unexpected opcode");
    }

    // Prefer compressed stores when storing the most recent T-hand value.
    // C.SWI/C.SDI implicitly store t#1.
    if ((Opc == LinxISA::SWI || Opc == LinxISA::SDI) &&
        MI->getOperand(0).isReg() && MI->getOperand(0).getReg() == LinxISA::T1 &&
        isInt<5>(I(2))) {
      OutMI.setOpcode(getSpecOpcode(Opc == LinxISA::SWI ? "C.SWI" : "C.SDI",
                                    /*LengthBits=*/16, /*Fields=*/2));
      OutMI.addOperand(MCOperand::createImm(R(1))); // SrcL (base)
      OutMI.addOperand(MCOperand::createImm(I(2))); // simm5 (scaled)
      return;
    }

    OutMI.setOpcode(getSpecOpcode(Mnem, /*LengthBits=*/32, /*Fields=*/3));
    OutMI.addOperand(MCOperand::createImm(R(0))); // SrcL (value)
    OutMI.addOperand(MCOperand::createImm(R(1))); // SrcR (base)
    OutMI.addOperand(MCOperand::createImm(I(2))); // simm12 (scaled)
    return;
  }

  case LinxISA::SB:
  case LinxISA::SH:
  case LinxISA::SW:
  case LinxISA::SD: {
    StringRef Mnem;
    switch (Opc) {
    case LinxISA::SB:
      Mnem = "SB";
      break;
    case LinxISA::SH:
      Mnem = "SH";
      break;
    case LinxISA::SW:
      Mnem = "SW";
      break;
    case LinxISA::SD:
      Mnem = "SD";
      break;
    default:
      llvm_unreachable("Unexpected opcode");
    }

    OutMI.setOpcode(getSpecOpcode(Mnem, /*LengthBits=*/32, /*Fields=*/4));
    OutMI.addOperand(MCOperand::createImm(R(0))); // SrcD (value)
    OutMI.addOperand(MCOperand::createImm(R(1))); // SrcL (base)
    OutMI.addOperand(MCOperand::createImm(R(2))); // SrcR (index)
    OutMI.addOperand(MCOperand::createImm(3));    // SrcRType (default: no modifier)
    return;
  }

  case LinxISA::CMPEQ:
  case LinxISA::CMPNE:
  case LinxISA::CMPLT:
  case LinxISA::CMPGE:
  case LinxISA::CMPLTU:
  case LinxISA::CMPGEU: {
    StringRef Mnem;
    switch (Opc) {
    case LinxISA::CMPEQ:
      Mnem = "CMP.EQ";
      break;
    case LinxISA::CMPNE:
      Mnem = "CMP.NE";
      break;
    case LinxISA::CMPLT:
      Mnem = "CMP.LT";
      break;
    case LinxISA::CMPGE:
      Mnem = "CMP.GE";
      break;
    case LinxISA::CMPLTU:
      Mnem = "CMP.LTU";
      break;
    case LinxISA::CMPGEU:
      Mnem = "CMP.GEU";
      break;
    default:
      llvm_unreachable("Unexpected opcode");
    }

    OutMI.setOpcode(getSpecOpcode(Mnem, /*LengthBits=*/32, /*Fields=*/4));
    OutMI.addOperand(MCOperand::createImm(R(0))); // RegDst
    OutMI.addOperand(MCOperand::createImm(R(1))); // SrcL
    OutMI.addOperand(MCOperand::createImm(R(2))); // SrcR
    OutMI.addOperand(MCOperand::createImm(3));    // SrcRType (default: no modifier)
    return;
  }

  case LinxISA::CSELrrr: {
    // `CSEL SrcP, SrcL, SrcR<.neg>, ->{t, u, Rd}`
    // LinxISA csel semantics: if pred != 0, use SrcR (true case), else SrcL (false)
    // LLVM CSELrrr operands: (rd, pred, src_true, src_false)
    // Map: SrcL = false case, SrcR = true case, SrcP = predicate
    OutMI.setOpcode(getSpecOpcode("CSEL", /*LengthBits=*/32, /*Fields=*/5));
    OutMI.addOperand(MCOperand::createImm(R(0))); // RegDst
    OutMI.addOperand(MCOperand::createImm(R(3))); // SrcL = false case
    OutMI.addOperand(MCOperand::createImm(R(1))); // SrcP = predicate
    OutMI.addOperand(MCOperand::createImm(R(2))); // SrcR = true case
    OutMI.addOperand(MCOperand::createImm(3));    // SrcRType (default: no modifier)
    return;
  }

  case LinxISA::JUMP: {
    OutMI.setOpcode(getSpecOpcode("J", /*LengthBits=*/32, /*Fields=*/1));
    OutMI.addOperand(lowerBranchTarget(0));
    return;
  }

  case LinxISA::BEQ:
  case LinxISA::BNE:
  case LinxISA::BLT:
  case LinxISA::BGE:
  case LinxISA::BLTU:
  case LinxISA::BGEU: {
    StringRef Mnem;
    switch (Opc) {
    case LinxISA::BEQ:
      Mnem = "B.EQ";
      break;
    case LinxISA::BNE:
      Mnem = "B.NE";
      break;
    case LinxISA::BLT:
      Mnem = "B.LT";
      break;
    case LinxISA::BGE:
      Mnem = "B.GE";
      break;
    case LinxISA::BLTU:
      Mnem = "B.LTU";
      break;
    case LinxISA::BGEU:
      Mnem = "B.GEU";
      break;
    default:
      llvm_unreachable("Unexpected opcode");
    }

    OutMI.setOpcode(getSpecOpcode(Mnem, /*LengthBits=*/32, /*Fields=*/3));
    OutMI.addOperand(MCOperand::createImm(R(0))); // SrcL
    OutMI.addOperand(MCOperand::createImm(R(1))); // SrcR
    OutMI.addOperand(lowerBranchTarget(2));       // simm12 (pcrel)
    return;
  }

  case LinxISA::JR: {
    OutMI.setOpcode(getSpecOpcode("JR", /*LengthBits=*/32, /*Fields=*/3));
    OutMI.addOperand(MCOperand::createImm(R(0))); // SrcL
    OutMI.addOperand(MCOperand::createImm(0));    // SrcZero (zero)
    OutMI.addOperand(MCOperand::createImm(0));    // simm12
    return;
  }

  //===----------------------------------------------------------------------===//
  // Function Entry/Exit Macro Instructions (LinxISA spec)
  //===----------------------------------------------------------------------===//

  case LinxISA::FENTRY: {
    // FENTRY [Begin ~ End], sp!, stacksize
    // Fields: SrcBegin, SrcEnd, uimm (split encoding)
    OutMI.setOpcode(getSpecOpcode("FENTRY", /*LengthBits=*/32, /*Fields=*/3));
    OutMI.addOperand(MCOperand::createImm(I(0))); // reg_begin
    OutMI.addOperand(MCOperand::createImm(I(1))); // reg_end
    OutMI.addOperand(MCOperand::createImm(I(2))); // stacksize
    return;
  }

  case LinxISA::FEXIT: {
    // FEXIT [Begin ~ End], sp!, stacksize
    OutMI.setOpcode(getSpecOpcode("FEXIT", /*LengthBits=*/32, /*Fields=*/3));
    OutMI.addOperand(MCOperand::createImm(I(0))); // reg_begin
    OutMI.addOperand(MCOperand::createImm(I(1))); // reg_end
    OutMI.addOperand(MCOperand::createImm(I(2))); // stacksize
    return;
  }

  case LinxISA::FRET_RA: {
    // FRET.RA [Begin ~ End], sp!, stacksize
    OutMI.setOpcode(getSpecOpcode("FRET.RA", /*LengthBits=*/32, /*Fields=*/3));
    OutMI.addOperand(MCOperand::createImm(I(0))); // reg_begin
    OutMI.addOperand(MCOperand::createImm(I(1))); // reg_end
    OutMI.addOperand(MCOperand::createImm(I(2))); // stacksize
    return;
  }

  case LinxISA::FRET_STK: {
    // FRET.STK [Begin ~ End], sp!, stacksize
    OutMI.setOpcode(getSpecOpcode("FRET.STK", /*LengthBits=*/32, /*Fields=*/3));
    OutMI.addOperand(MCOperand::createImm(I(0))); // reg_begin
    OutMI.addOperand(MCOperand::createImm(I(1))); // reg_end
    OutMI.addOperand(MCOperand::createImm(I(2))); // stacksize
    return;
  }

  default:
    MI->print(errs());
    report_fatal_error("Linx: unsupported machine instruction in MC lowering");
  }
}
