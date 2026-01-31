//===-- LinxISAMCCodeEmitter.cpp - LinxISA MC Code Emitter ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/LinxISAFixupKinds.h"
#include "MCTargetDesc/LinxISAMCTargetDesc.h"
#include "MCTargetDesc/LinxISAOpcodeTables.h"
#include "llvm/MC/MCCodeEmitter.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Support/ErrorHandling.h"

using namespace llvm;

namespace {

class LinxISAMCCodeEmitter : public MCCodeEmitter {
public:
  LinxISAMCCodeEmitter() = default;

  void encodeInstruction(const MCInst &MI, SmallVectorImpl<char> &CB,
                         SmallVectorImpl<MCFixup> &Fixups,
                         const MCSubtargetInfo &STI) const override;
};

static uint64_t maskBits(unsigned Width) {
  if (Width >= 64)
    return ~0ull;
  return (1ull << Width) - 1ull;
}

static void encodeFieldBits(uint64_t &Insn, const linxisa_field &Field,
                            uint64_t Value) {
  for (unsigned j = 0; j < Field.piece_count; ++j) {
    const linxisa_field_piece &P = linxisa_field_pieces[Field.piece_start + j];
    uint64_t Piece = (Value >> P.value_lsb) & maskBits(P.width);
    Insn |= (Piece << P.insn_lsb);
  }
}

void LinxISAMCCodeEmitter::encodeInstruction(const MCInst &MI,
                                             SmallVectorImpl<char> &CB,
                                             SmallVectorImpl<MCFixup> &Fixups,
                                             const MCSubtargetInfo &STI) const {
  const unsigned Opcode = MI.getOpcode();
  if (Opcode >= linxisa_inst_forms_count)
    report_fatal_error("Linx: MC opcode out of range");

  const linxisa_inst_form &Form = linxisa_inst_forms[Opcode];
  uint64_t Insn = Form.match;

  const unsigned FieldCount = Form.field_count;
  for (unsigned i = 0; i < FieldCount && i < MI.getNumOperands(); ++i) {
    const linxisa_field &F = linxisa_fields[Form.field_start + i];
    const MCOperand &Op = MI.getOperand(i);

    if (Op.isImm()) {
      uint64_t V = static_cast<uint64_t>(Op.getImm()) & maskBits(F.bit_width);
      encodeFieldBits(Insn, F, V);
      continue;
    }

    if (!Op.isExpr())
      report_fatal_error("Linx: unsupported MCOperand kind for field");

    const MCExpr *Expr = Op.getExpr();
    StringRef Name = F.name ? StringRef(F.name) : StringRef();
    StringRef Mnemonic = Form.mnemonic ? StringRef(Form.mnemonic) : StringRef();

    // Minimal fixup support for early bring-up: only branch/jump PC-relative
    // label operands are supported.
    MCFixupKind Kind = FK_NONE;
    if (Name == "simm12" && Mnemonic.starts_with("B.")) {
      Kind = static_cast<MCFixupKind>(LinxISA::FIXUP_LINX_B12_PCREL);
    } else if (Name == "simm12" && Mnemonic.starts_with("C.BSTART")) {
      Kind = static_cast<MCFixupKind>(LinxISA::FIXUP_LINX_CBSTART12_PCREL);
    } else if (Name == "simm22" && Mnemonic == "J") {
      Kind = static_cast<MCFixupKind>(LinxISA::FIXUP_LINX_J22_PCREL);
    } else if (Name == "simm17" && Mnemonic.starts_with("BSTART.")) {
      Kind = static_cast<MCFixupKind>(LinxISA::FIXUP_LINX_B17_PCREL);
    } else if (Name == "simm" && Mnemonic.starts_with("HL.BSTART")) {
      Kind = static_cast<MCFixupKind>(LinxISA::FIXUP_LINX_HL_BSTART30_PCREL);
    } else if (Name == "uimm5" && Mnemonic == "C.SETRET") {
      Kind = static_cast<MCFixupKind>(LinxISA::FIXUP_LINX_CSETRET5_PCREL);
    } else if (Name == "imm20" && Mnemonic == "SETRET") {
      Kind = static_cast<MCFixupKind>(LinxISA::FIXUP_LINX_SETRET20_PCREL);
    } else if (Name == "imm32" && Mnemonic == "HL.SETRET") {
      Kind = static_cast<MCFixupKind>(LinxISA::FIXUP_LINX_HL_SETRET32_PCREL);
    } else {
      report_fatal_error("Linx: unsupported expression fixup");
    }

    Fixups.push_back(MCFixup::create(/*Offset=*/0, Expr, Kind, /*PCRel=*/true));
  }

  const unsigned Bytes = Form.length_bits / 8;
  for (unsigned i = 0; i < Bytes; ++i)
    CB.push_back(static_cast<char>((Insn >> (i * 8)) & 0xFF));
}

} // namespace

MCCodeEmitter *llvm::createLinxISAMCCodeEmitter(const MCInstrInfo &MII,
                                                MCContext &Ctx) {
  return new LinxISAMCCodeEmitter();
}
