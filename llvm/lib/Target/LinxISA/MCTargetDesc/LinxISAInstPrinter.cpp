#include "MCTargetDesc/LinxISAInstPrinter.h"
#include "MCTargetDesc/LinxISAOpcodeTables.h"
#include "llvm/ADT/APInt.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCAsmInfo.h"
#include "llvm/MC/MCInst.h"
#include "llvm/Support/raw_ostream.h"
#include <optional>

using namespace llvm;

static StringRef reg5Name(unsigned Code) {
  static constexpr const char *Names[32] = {
      "zero", "sp",  "a0",  "a1",  "a2",  "a3",  "a4",  "a5",
      "a6",   "a7",  "ra",  "s0",  "s1",  "s2",  "s3",  "s4",
      "s5",   "s6",  "s7",  "s8",  "x0",  "x1",  "x2",  "x3",
      "t#1",  "t#2", "t#3", "t#4", "u#1", "u#2", "u#3", "u#4",
  };
  if (Code < 32)
    return Names[Code];
  return "r?";
}

static StringRef brTypeName(unsigned BrType) {
  switch (BrType & 0x7) {
  case 1:
    return "FALL";
  case 2:
    return "DIRECT";
  case 3:
    return "COND";
  case 4:
    return "CALL";
  case 5:
    return "IND";
  case 6:
    return "ICALL";
  case 7:
    return "RET";
  default:
    return "BR?";
  }
}

static StringRef blockTypeSuffix(unsigned BlockType) {
  switch (BlockType & 0x1f) {
  case 0:
    return "STD";
  case 1:
    return "SYS";
  case 2:
    return "FP";
  default:
    return StringRef();
  }
}

static int64_t shlSigned64(int64_t V, unsigned Shift) {
  APInt A(64, static_cast<uint64_t>(V), /*isSigned=*/true);
  A <<= Shift;
  return A.getSExtValue();
}

static uint64_t shlUnsigned64(uint64_t V, unsigned Shift) {
  APInt A(64, V, /*isSigned=*/false);
  A <<= Shift;
  return A.getZExtValue();
}

std::pair<const char *, uint64_t>
LinxISAInstPrinter::getMnemonic(const MCInst &MI) const {
  static constexpr const char *BadOpcode = "<bad-opcode>";
  static constexpr const char *Unknown = "<unknown>";

  const unsigned Opcode = MI.getOpcode();
  if (Opcode >= linxisa_inst_forms_count)
    return {BadOpcode, 0};

  const linxisa_inst_form &Form = linxisa_inst_forms[Opcode];
  if (Form.mnemonic && Form.mnemonic[0])
    return {Form.mnemonic, 0};
  if (Form.id && Form.id[0])
    return {Form.id, 0};
  return {Unknown, 0};
}

void LinxISAInstPrinter::printRegName(raw_ostream &OS, MCRegister Reg) {
  // Reg numbering is target-defined; for early bring-up keep this stable by
  // printing numeric registers.
  OS << "r" << Reg.id();
}

static bool asmImpliesArrowDest(StringRef Asm, StringRef Dest) {
  SmallString<64> Compact;
  Compact.reserve(Asm.size());
  for (char C : Asm) {
    if (C == ' ' || C == '\t')
      continue;
    Compact.push_back(llvm::toLower(C));
  }
  return StringRef(Compact).contains(Dest);
}

void LinxISAInstPrinter::printInst(const MCInst *MI, uint64_t Address,
                                   StringRef Annot,
                                   const MCSubtargetInfo & /*STI*/,
                                   raw_ostream &OS) {
  const unsigned Opcode = MI->getOpcode();
  if (Opcode >= linxisa_inst_forms_count) {
    OS << "<bad-opcode>";
    return;
  }

  const linxisa_inst_form &Form = linxisa_inst_forms[Opcode];
  StringRef AsmFmt(Form.asm_fmt ? Form.asm_fmt : "");

  // Map field name -> operand (immediate or expression) from MCInst operands in
  // spec field order.
  SmallVector<std::pair<StringRef, MCOperand>, 16> Fields;
  const unsigned FieldCount = Form.field_count;
  for (unsigned i = 0; i < FieldCount; ++i) {
    if (i >= MI->getNumOperands())
      break;
    const linxisa_field &F = linxisa_fields[Form.field_start + i];
    if (!F.name)
      continue;
    const MCOperand &Op = MI->getOperand(i);
    if (!Op.isImm() && !Op.isExpr())
      continue;
    Fields.push_back({StringRef(F.name), Op});
  }

  auto findField = [&](StringRef Name) -> std::optional<MCOperand> {
    for (auto &KV : Fields)
      if (KV.first == Name)
        return KV.second;
    return std::nullopt;
  };

  auto findFieldImm = [&](StringRef Name) -> std::optional<int64_t> {
    if (auto Op = findField(Name)) {
      if (Op->isImm())
        return Op->getImm();
    }
    return std::nullopt;
  };

  auto emitReg = [&](StringRef FieldName) {
    auto V = findField(FieldName);
    if (!V)
      return;
    if (!V->isImm())
      return;
    unsigned Code = static_cast<unsigned>(V->getImm()) & 0x1F;
    OS << reg5Name(Code);
  };

  auto emitPcRelTargetHexScaled = [&](StringRef FieldName, bool Signed,
                                      unsigned Shift) -> bool {
    auto Op = findField(FieldName);
    if (!Op)
      return false;
    if (Op->isExpr()) {
      MAI.printExpr(OS, *Op->getExpr());
      return true;
    }
    if (!Op->isImm())
      return false;

    uint64_t Target = 0;
    if (Signed) {
      int64_t Delta = shlSigned64(Op->getImm(), Shift);
      int64_t SignedTarget = static_cast<int64_t>(Address) + Delta;
      Target = static_cast<uint64_t>(SignedTarget);
    } else {
      uint64_t Delta = shlUnsigned64(static_cast<uint64_t>(Op->getImm()), Shift);
      Target = Address + Delta;
    }

    OS << "0x" << utohexstr(Target, /*LowerCase=*/true);
    return true;
  };

  auto emitPcRelTargetHex = [&](StringRef FieldName, bool Signed) -> bool {
    return emitPcRelTargetHexScaled(FieldName, Signed, /*Shift=*/1);
  };

  auto emitSetRetTarget = [&]() -> bool {
    // setret/c.setret immediate is an instruction-halfword offset.
    if (findField("uimm5")) {
      return emitPcRelTargetHex("uimm5", /*Signed=*/false);
    }
    if (findField("imm20")) {
      return emitPcRelTargetHex("imm20", /*Signed=*/false);
    }
    if (findField("imm32")) {
      return emitPcRelTargetHex("imm32", /*Signed=*/true);
    }
    return false;
  };

  auto emitBlockTarget = [&]() -> bool {
    // BSTART label fields are instruction-halfword offsets.
    if (findField("simm25"))
      return emitPcRelTargetHex("simm25", /*Signed=*/true);
    if (findField("simm17"))
      return emitPcRelTargetHex("simm17", /*Signed=*/true);
    if (findField("simm12"))
      return emitPcRelTargetHex("simm12", /*Signed=*/true);
    // HL.BSTART uses an instruction-aligned byte offset (simm[0] is implicit 0).
    if (findField("simm"))
      return emitPcRelTargetHexScaled("simm", /*Signed=*/true, /*Shift=*/0);
    return false;
  };

  // Special-case: setret/c.setret want a printed target address instead of the
  // raw immediate encoding.
  if (AsmFmt.starts_with_insensitive("setret") ||
      AsmFmt.starts_with_insensitive("c.setret") ||
      AsmFmt.starts_with_insensitive("hl.setret")) {
    StringRef Tok = AsmFmt.empty() ? StringRef("setret")
                                   : AsmFmt.split(' ').first.rtrim(",");
    OS << Tok;
    OS << "\t";
    if (!emitSetRetTarget())
      OS << "0x0";
    OS << ",\t->ra";
    printAnnotation(OS, Annot);
    return;
  }

  // Special-case: block split instructions. LLVM uses these as block
  // terminators; Linx uses halfword PC-relative offsets.
  const bool IsCBSTART = AsmFmt.starts_with("C.BSTART");
  const bool IsBSTART = AsmFmt.starts_with("HL.BSTART") ||
                        AsmFmt.starts_with("BSTART.STD") ||
                        AsmFmt.starts_with("BSTART.FP") ||
                        AsmFmt.starts_with("BSTART.SYS") ||
                        AsmFmt.starts_with("BSTART.") ||
                        AsmFmt.starts_with("BSTART ");
  if (IsCBSTART || IsBSTART) {
    StringRef FirstTok = AsmFmt.split(' ').first.rtrim(",");

    // Render C.BSTART<.BlockType> as C.BSTART.<suffix>.
    SmallString<32> PrintedMnemonic;
    if (FirstTok.contains("<.BlockType>")) {
      unsigned BT = 0;
      if (auto V = findFieldImm("BlockType"))
        BT = static_cast<unsigned>(*V);
      PrintedMnemonic = FirstTok.take_front(FirstTok.find('<'));
      // STD is the default and is omitted for readability.
      if ((BT & 0x1f) != 0) {
        PrintedMnemonic += ".";
        if (StringRef S = blockTypeSuffix(BT); !S.empty())
          PrintedMnemonic += S;
        else {
          PrintedMnemonic += "BT";
          PrintedMnemonic += utostr(BT & 0x1f);
        }
      }
      OS << PrintedMnemonic;
    } else if (FirstTok == "C.BSTART" &&
               (AsmFmt.contains(" DIRECT") || AsmFmt.contains(" COND"))) {
      // These encodings are scalar-block forms; the default BlockType is STD.
      OS << "C.BSTART";
    } else if (FirstTok == "BSTART" &&
               (AsmFmt.contains("{DIRECT, CALL}") || AsmFmt.contains(" COND"))) {
      // These encodings are scalar-block forms; the default BlockType is STD.
      OS << "BSTART";
    } else if (FirstTok == "C.BSTART.STD") {
      OS << "C.BSTART";
    } else if (FirstTok == "BSTART.STD") {
      OS << "BSTART";
    } else {
      OS << FirstTok;
    }

    enum class BrKind {
      Unknown,
      Fall,
      Direct,
      Cond,
      Call,
      Ind,
      ICall,
      Ret,
    };

    BrKind K = BrKind::Unknown;
    if (auto V = findFieldImm("BrType")) {
      switch (static_cast<unsigned>(*V) & 0x7) {
      case 1:
        K = BrKind::Fall;
        break;
      case 2:
        K = BrKind::Direct;
        break;
      case 3:
        K = BrKind::Cond;
        break;
      case 4:
        K = BrKind::Call;
        break;
      case 5:
        K = BrKind::Ind;
        break;
      case 6:
        K = BrKind::ICall;
        break;
      case 7:
        K = BrKind::Ret;
        break;
      default:
        K = BrKind::Unknown;
        break;
      }
    } else {
      // Order matters: ICALL contains "CALL" as a substring.
      if (AsmFmt.contains("{DIRECT, CALL}"))
        K = BrKind::Direct;
      else if (AsmFmt.contains(" ICALL"))
        K = BrKind::ICall;
      else if (AsmFmt.contains(" IND"))
        K = BrKind::Ind;
      else if (AsmFmt.contains(" RET"))
        K = BrKind::Ret;
      else if (AsmFmt.contains(" COND"))
        K = BrKind::Cond;
      else if (AsmFmt.contains(" DIRECT"))
        K = BrKind::Direct;
      else if (AsmFmt.contains(" CALL"))
        K = BrKind::Call;
      else if (AsmFmt.contains(" FALL"))
        K = BrKind::Fall;
    }

    auto emitKind = [&](StringRef S) {
      if (!S.empty())
        OS << "\t" << S;
    };

    auto emitKindAndLabel = [&](StringRef S) {
      OS << "\t" << S << ", ";
      emitBlockTarget();
    };

    switch (K) {
    case BrKind::Unknown:
      // Best-effort: if this is the BrType-form, show symbolic BrType.
      if (auto V = findFieldImm("BrType")) {
        StringRef N = brTypeName(static_cast<unsigned>(*V));
        if (N != "FALL")
          emitKind(N);
      }
      // Otherwise, fall back to generic printing below.
      break;
    case BrKind::Fall: {
      // For .STD/.SYS/.FP, FALL is the default and can be omitted. If a fixup
      // label is encoded (non-zero offset), show it.
      int64_t Off = 0;
      if (auto V = findFieldImm("simm17"))
        Off = *V;
      else if (auto V = findFieldImm("simm25"))
        Off = *V;
      else if (auto V = findFieldImm("simm12"))
        Off = *V;
      if (Off != 0)
        emitKindAndLabel("FALL");
      break;
    }
    case BrKind::Direct:
      emitKindAndLabel("DIRECT");
      break;
    case BrKind::Cond:
      emitKindAndLabel("COND");
      break;
    case BrKind::Call:
      emitKindAndLabel("CALL");
      break;
    case BrKind::Ind:
      emitKind("IND");
      break;
    case BrKind::ICall:
      emitKind("ICALL");
      break;
    case BrKind::Ret:
      emitKind("RET");
      break;
    }

    // Disassembler sugar: if a BSTART CALL MCInst carries an extra operand,
    // render it as a fused return-target annotation (`ra=...`).
    if (K == BrKind::Call && MI->getNumOperands() > FieldCount) {
      const MCOperand &RetOp = MI->getOperand(FieldCount);
      OS << ", ra=";
      if (RetOp.isExpr()) {
        MAI.printExpr(OS, *RetOp.getExpr());
      } else if (RetOp.isImm()) {
        OS << "0x"
           << utohexstr(static_cast<uint64_t>(RetOp.getImm()),
                        /*LowerCase=*/true);
      }
    }

    printAnnotation(OS, Annot);
    return;
  }

  auto srcRTypeSuffix = [&](unsigned V) -> StringRef {
    switch (V & 0x3u) {
    case 0:
      return ".sw";
    case 1:
      return ".uw";
    case 2:
      return ".neg";
    default:
      return StringRef();
    }
  };

  auto emitSrcRWithTypeAndShift = [&](std::optional<int64_t> ForcedShift) {
    emitReg("SrcR");
    if (auto V = findFieldImm("SrcRType")) {
      if (StringRef S = srcRTypeSuffix(static_cast<unsigned>(*V)); !S.empty())
        OS << S;
    }

    std::optional<int64_t> Sh;
    if (ForcedShift)
      Sh = *ForcedShift;
    else if (auto V = findFieldImm("shamt"))
      Sh = *V;

    if (Sh && *Sh != 0)
      OS << "<<" << (*Sh & 0x1f);
  };

  auto emitArrowDest = [&]() {
    if (auto Op = findField("RegDst")) {
      if (Op->isImm()) {
        unsigned Code = static_cast<unsigned>(Op->getImm()) & 0x1F;
        if (Code == 31) {
          OS << ",\t->t";
        } else if (Code == 30) {
          // `->` is the U-hand output by convention.
          OS << ",\t->";
        } else {
          OS << ",\t->" << reg5Name(Code);
        }
        return;
      }
    }

    if (asmImpliesArrowDest(AsmFmt, "->t")) {
      OS << ",\t->t";
      return;
    }
    if (asmImpliesArrowDest(AsmFmt, "->u")) {
      OS << ",\t->";
      return;
    }
    if (asmImpliesArrowDest(AsmFmt, "->ra")) {
      OS << ",\t->ra";
      return;
    }
  };

  // Special-case: FENTRY/FEXIT/FRET.RA/FRET.STK with register range syntax.
  // Format: MNEM [RegBegin ~ RegEnd], sp!, stacksize
  // Must check BEFORE memory operand check since these also contain '['.
  if (AsmFmt.contains("[RegSrc0 ~ RegSrcn]") ||
      AsmFmt.contains("[RegDst0 ~ RegDstn]")) {
    StringRef Tok = Form.mnemonic ? StringRef(Form.mnemonic) : StringRef("FENTRY");
    OS << Tok;
    OS << "\t[";
    
    // Get register range from SrcBegin/SrcEnd or DstBegin/DstEnd fields
    unsigned RegBegin = 10, RegEnd = 14;  // defaults: ra ~ s2
    if (auto V = findFieldImm("SrcBegin"))
      RegBegin = static_cast<unsigned>(*V);
    else if (auto V = findFieldImm("DstBegin"))
      RegBegin = static_cast<unsigned>(*V);
    if (auto V = findFieldImm("SrcEnd"))
      RegEnd = static_cast<unsigned>(*V);
    else if (auto V = findFieldImm("DstEnd"))
      RegEnd = static_cast<unsigned>(*V);
    
    OS << reg5Name(RegBegin & 0x1F) << " ~ " << reg5Name(RegEnd & 0x1F);
    OS << "], sp!, ";
    
    // Get stack size from uimm field
    if (auto V = findFieldImm("uimm")) {
      // uimm is already in bytes (reconstructed from split encoding)
      OS << *V;
    } else {
      OS << "0";
    }
    
    printAnnotation(OS, Annot);
    return;
  }

  // Pretty printer for memory operands.
  if (AsmFmt.contains('[')) {
    StringRef Tok = AsmFmt.empty() ? StringRef("<mem>")
                                   : AsmFmt.split(' ').first.rtrim(",");
    OS << Tok;
    OS << "\t";

    const bool HasDest = AsmFmt.contains("->");
    const StringRef Mnem = Form.mnemonic ? StringRef(Form.mnemonic) : StringRef();

    auto scaleFromMnemonic = [&]() -> int64_t {
      if (Mnem == "LBI" || Mnem == "LBUI" || Mnem == "SBI")
        return 1;
      if (Mnem == "LHI" || Mnem == "LHUI" || Mnem == "SHI")
        return 2;
      if (Mnem == "LWI" || Mnem == "LWUI" || Mnem == "SWI" || Mnem == "C.LWI" ||
          Mnem == "C.SWI")
        return 4;
      if (Mnem == "LDI" || Mnem == "SDI" || Mnem == "C.LDI" || Mnem == "C.SDI")
        return 8;
      return 1;
    };

    auto emitScaledImmOff = [&]() -> bool {
      std::optional<int64_t> Off;
      if (auto V = findFieldImm("simm12"))
        Off = *V;
      else if (auto V = findFieldImm("uimm12"))
        Off = *V;
      else if (auto V = findFieldImm("simm5"))
        Off = *V;
      else if (auto V = findFieldImm("uimm5"))
        Off = *V;

      if (!Off)
        return false;

      const int64_t Scale = scaleFromMnemonic();
      OS << (*Off * Scale);
      return true;
    };

    const bool IsRegOffset = findField("SrcRType") && findField("SrcR");

    if (Mnem == "C.SWI" || Mnem == "C.SDI") {
      // Compressed stores: implicit value `t#1`.
      OS << "t#1, [";
      emitReg("SrcL"); // base
      OS << ", ";
      if (!emitScaledImmOff())
        OS << "0";
      OS << "]";
      printAnnotation(OS, Annot);
      return;
    }

    if (HasDest) {
      // Loads: `[base, off]` / `[base, idx<type><<shamt>] , ->dst`.
      OS << "[";
      emitReg("SrcL"); // base
      OS << ", ";
      if (IsRegOffset) {
        emitSrcRWithTypeAndShift(/*ForcedShift=*/std::nullopt);
      } else {
        if (!emitScaledImmOff())
          OS << "0";
      }
      OS << "]";
      emitArrowDest();
      printAnnotation(OS, Annot);
      return;
    }

    // Stores.
    if (findField("SrcD")) {
      // Reg-offset stores: `SrcD, [SrcL, SrcR<type><<k>]`.
      emitReg("SrcD");
      OS << ", [";
      emitReg("SrcL"); // base
      OS << ", ";
      std::optional<int64_t> ForcedShift;
      if (Mnem == "SH")
        ForcedShift = 1;
      else if (Mnem == "SW")
        ForcedShift = 2;
      else if (Mnem == "SD")
        ForcedShift = 3;
      emitSrcRWithTypeAndShift(ForcedShift);
      OS << "]";
      printAnnotation(OS, Annot);
      return;
    }

    // Imm-offset stores: `SrcL, [SrcR, off]`.
    emitReg("SrcL"); // value
    OS << ", [";
    emitReg("SrcR"); // base
    OS << ", ";
    if (!emitScaledImmOff())
      OS << "0";
    OS << "]";
    printAnnotation(OS, Annot);
    return;
  }

  // Pretty printer for the common "SrcL, SrcR<type><<shamt>" operand form.
  if (findField("SrcL") && findField("SrcR") && findField("SrcRType") &&
      !AsmFmt.contains('[')) {
    StringRef Tok = AsmFmt.empty() ? StringRef("<op>")
                                   : AsmFmt.split(' ').first.rtrim(",");
    OS << Tok;
    OS << "\t";
    emitReg("SrcL");
    OS << ", ";
    emitSrcRWithTypeAndShift(/*ForcedShift=*/std::nullopt);
    if (AsmFmt.contains("->"))
      emitArrowDest();
    printAnnotation(OS, Annot);
    return;
  }

  // Generic printer: best-effort by listing fields in common ISA order.
  if (!AsmFmt.empty())
    OS << AsmFmt.split(' ').first;
  else if (Form.mnemonic && Form.mnemonic[0])
    OS << StringRef(Form.mnemonic).lower();
  else
    OS << "<unknown>";

  const bool IsSetcImm =
      AsmFmt.starts_with_insensitive("setc.") && findField("shamt") &&
      (findField("simm12") || findField("uimm12"));

  bool FirstOp = true;
  auto sep = [&]() {
    OS << (FirstOp ? "\t" : ", ");
    FirstOp = false;
  };

  // Sources (common ones).
  for (StringRef R : {"SrcL", "SrcR", "SrcD", "SrcP", "SrcA"}) {
    if (findField(R)) {
      sep();
      emitReg(R);
    }
  }

  // Common immediates / shift amounts.
  for (auto &KV : Fields) {
    StringRef Name = KV.first;
    if (Name == "RegDst" || Name == "SrcL" || Name == "SrcR" || Name == "SrcD" ||
        Name == "SrcP" || Name == "SrcA")
      continue;
    if (IsSetcImm &&
        (Name.equals_insensitive("shamt") || Name.equals_insensitive("simm12") ||
         Name.equals_insensitive("uimm12")))
      continue;
    if (Name.starts_with_insensitive("imm") ||
        Name.starts_with_insensitive("simm") ||
        Name.starts_with_insensitive("uimm") ||
        Name.starts_with_insensitive("shamt") || Name.equals_insensitive("BrType") ||
        Name.equals_insensitive("BlockType")) {
      sep();
      const MCOperand &Op = KV.second;
      if (Op.isImm())
        OS << Op.getImm();
      else if (Op.isExpr())
        MAI.printExpr(OS, *Op.getExpr());
    }
  }

  // SETC.*I encodes an immediate as (simm12/uimm12) << shamt. Print the
  // computed value rather than the raw fields.
  if (IsSetcImm) {
    unsigned Shamt = 0;
    if (auto V = findFieldImm("shamt"))
      Shamt = static_cast<unsigned>(*V) & 0x1f;
    if (auto Op = findField("simm12")) {
      sep();
      if (Op->isImm())
        OS << shlSigned64(Op->getImm(), Shamt);
      else if (Op->isExpr())
        MAI.printExpr(OS, *Op->getExpr());
    } else if (auto Op = findField("uimm12")) {
      sep();
      if (Op->isImm())
        OS << shlUnsigned64(static_cast<uint64_t>(Op->getImm()), Shamt);
      else if (Op->isExpr())
        MAI.printExpr(OS, *Op->getExpr());
    }
  }

  // Destination (arrow syntax).
  auto dstSep = [&]() {
    OS << (FirstOp ? "\t" : ",\t");
    FirstOp = false;
  };

  if (auto Op = findField("RegDst")) {
    dstSep();
    if (Op->isImm()) {
      unsigned Code = static_cast<unsigned>(Op->getImm()) & 0x1F;
      if (Code == 31)
        OS << "->t";
      else if (Code == 30)
        OS << "->";
      else
        OS << "->" << reg5Name(Code);
    }
  } else if (!AsmFmt.empty()) {
    if (asmImpliesArrowDest(AsmFmt, "->t")) {
      dstSep();
      OS << "->t";
    } else if (asmImpliesArrowDest(AsmFmt, "->u")) {
      dstSep();
      OS << "->";
    } else if (asmImpliesArrowDest(AsmFmt, "->ra")) {
      dstSep();
      OS << "->ra";
    }
  }

  printAnnotation(OS, Annot);
}
