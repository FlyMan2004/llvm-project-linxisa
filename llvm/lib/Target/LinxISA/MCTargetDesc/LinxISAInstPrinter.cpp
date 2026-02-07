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

static const char *ssrIdSymbol(uint64_t Id) {
  // SSR_ID is encoded as a 12-bit field in the base forms (SSRGET/SSRSET/SSRSWAP).
  // Keep this mapping aligned with isa.txt (and the ISA manual SSR table).
  unsigned V = static_cast<unsigned>(Id) & 0xfffu;
  switch (V) {
  case 0x000:
    return "TP";
  case 0x001:
    return "GP";
  case 0x010:
    return "TIME";
  case 0xc00:
    return "CYCLE";
  case 0x020:
    return "CSTATE";
  case 0x021:
    return "LXLCID";
  case 0x022:
    return "VENDOR";
  case 0x023:
    return "VERSION";
  case 0x024:
    return "LCFR";
  case 0x025:
    return "LCFR_EN";
  case 0x800:
    return "TR1";
  case 0x801:
    return "TR2";
  case 0x810:
    return "SYSCNT";
  case 0x820:
    return "CW";
  case 0x830:
    return "MSGBCR";
  case 0x831:
    return "MSGBD1";
  case 0x832:
    return "MSGBD2";
  case 0x833:
    return "MSGBD3";
  case 0x834:
    return "MSGBD4";
  case 0x835:
    return "MSGBD5";
  case 0x836:
    return "MSGBD6";
  case 0x837:
    return "MSGBD7";
  case 0x838:
    return "MSGBD8";
  case 0x839:
    return "MSGBD9";
  case 0x83a:
    return "MSGBD10";

  // Privileged/ACR-scoped families (encoded low 12 bits).
  case 0xf00:
    return "ECSTATE_ACRn";
  case 0xf01:
    return "EVBASE_ACRn";
  case 0xf02:
    return "TRAPNO_ACRn";
  case 0xf03:
    return "TRAPARG0_ACRn";
  case 0xf05:
    return "ETEMP_ACRn";
  case 0xf06:
    return "FUTO_ACRn";
  case 0xf07:
    return "ECONFIG_ACRn";
  case 0xf08:
    return "IPENDING_ACRn";
  case 0xf09:
    return "TOPEI_ACRn";
  case 0xf0a:
    return "EOIEI_ACRn";
  case 0xf0b:
    return "EBPC_ACRn";
  case 0xf0c:
    return "EBARG_ACRn";
  case 0xf0d:
    return "ETPC_ACRn";
  case 0xf0e:
    return "EBPCN_ACRn";
  case 0xf10:
    return "MMTBASE_ACRn";
  case 0xf11:
    return "MMCONFIG_ACRn";
  case 0xf20:
    return "TIMER_TIME_ACRn";
  case 0xf21:
    return "TIMER_TIMECMP_ACRn";
  case 0xf30:
    return "XBINFO_ACRn";
  case 0xf31:
    return "ACR_PARAM_ACRn";
  default:
    return nullptr;
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
  static constexpr const char *Invalid = "<invalid>";

  const unsigned Opcode = MI.getOpcode();
  if (Opcode >= linxisa_inst_forms_count)
    return {BadOpcode, 0};

  const linxisa_inst_form &Form = linxisa_inst_forms[Opcode];
  if (Form.mnemonic && Form.mnemonic[0])
    return {Form.mnemonic, 0};
  if (Form.id && Form.id[0])
    return {Form.id, 0};
  return {Invalid, 0};
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
  const StringRef RawTok = AsmFmt.empty() ? StringRef()
                                          : AsmFmt.split(' ').first.rtrim(",");

  auto stripAngleSuffix = [&](StringRef Tok) -> StringRef {
    if (size_t Pos = Tok.find('<'); Pos != StringRef::npos)
      Tok = Tok.take_front(Pos);
    return Tok;
  };

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

  SmallString<32> PrintedMnemonicTok;
  auto mnemonicTok = [&](StringRef Default) -> StringRef {
    PrintedMnemonicTok.clear();
    if (RawTok.empty())
      return Default;

    StringRef Tok = stripAngleSuffix(RawTok);
    if (Tok.equals_insensitive("c.break") && Form.mnemonic &&
        StringRef(Form.mnemonic).equals_insensitive("C.EBREAK"))
      return "c.ebreak";

    if (Tok.empty())
      return Default;

    auto emitFpT = [&](unsigned SrcType) {
      switch (SrcType & 0x3u) {
      case 0:
        PrintedMnemonicTok += "fd";
        break;
      case 1:
        PrintedMnemonicTok += "fs";
        break;
      case 2:
        PrintedMnemonicTok += "fh";
        break;
      case 3:
        PrintedMnemonicTok += "fb";
        break;
      }
    };

    auto emitCvtFpDst = [&](unsigned DstType) {
      switch (DstType & 0x1fu) {
      case 0:
        PrintedMnemonicTok += "fd";
        break;
      case 1:
        PrintedMnemonicTok += "fs";
        break;
      default:
        PrintedMnemonicTok += "dt";
        PrintedMnemonicTok += utostr(DstType & 0x1fu);
        break;
      }
    };

    auto emitCvtIntDst = [&](unsigned DstType) {
      switch (DstType & 0x1fu) {
      case 0:
        PrintedMnemonicTok += "ud";
        break;
      case 1:
        PrintedMnemonicTok += "uw";
        break;
      case 2:
        PrintedMnemonicTok += "uh";
        break;
      case 3:
        PrintedMnemonicTok += "ub";
        break;
      case 8:
        PrintedMnemonicTok += "sd";
        break;
      case 9:
        PrintedMnemonicTok += "sw";
        break;
      case 10:
        PrintedMnemonicTok += "sh";
        break;
      case 11:
        PrintedMnemonicTok += "sb";
        break;
      default:
        PrintedMnemonicTok += "dt";
        PrintedMnemonicTok += utostr(DstType & 0x1fu);
        break;
      }
    };

    auto emitCvtIntSrc = [&](unsigned SrcType, bool Signed) {
      switch (SrcType & 0x3u) {
      case 0:
        PrintedMnemonicTok += Signed ? "sd" : "ud";
        break;
      case 1:
        PrintedMnemonicTok += Signed ? "sw" : "uw";
        break;
      case 2:
        PrintedMnemonicTok += Signed ? "sh" : "uh";
        break;
      case 3:
        PrintedMnemonicTok += Signed ? "sb" : "ub";
        break;
      }
    };

    if (Tok.contains("{T}")) {
      const size_t Pos = Tok.find('{');
      PrintedMnemonicTok = Tok.take_front(Pos);
      unsigned SrcType = 0;
      if (auto V = findFieldImm("SrcType"))
        SrcType = static_cast<unsigned>(*V);
      emitFpT(SrcType);
      return PrintedMnemonicTok;
    }

    if (Tok.contains("{srcT2dstT}")) {
      const size_t Pos = Tok.find('{');
      PrintedMnemonicTok = Tok.take_front(Pos);

      unsigned SrcType = 0;
      if (auto V = findFieldImm("SrcType"))
        SrcType = static_cast<unsigned>(*V);

      unsigned DstType = 0;
      if (auto V = findFieldImm("DstType"))
        DstType = static_cast<unsigned>(*V);

      StringRef Mnem = Form.mnemonic ? StringRef(Form.mnemonic) : StringRef();

      if (Mnem.equals_insensitive("SCVTF")) {
        emitCvtIntSrc(SrcType, /*Signed=*/true);
        PrintedMnemonicTok += "2";
        emitCvtFpDst(DstType);
      } else if (Mnem.equals_insensitive("UCVTF")) {
        emitCvtIntSrc(SrcType, /*Signed=*/false);
        PrintedMnemonicTok += "2";
        emitCvtFpDst(DstType);
      } else if (Mnem.equals_insensitive("FCVTZ")) {
        emitFpT(SrcType);
        PrintedMnemonicTok += "2";
        emitCvtIntDst(DstType);
      } else {
        // FCVT/FCVTA/FCVTM/FCVTN/FCVTP: default to FP->FP naming.
        emitFpT(SrcType);
        PrintedMnemonicTok += "2";
        emitCvtFpDst(DstType);
      }

      return PrintedMnemonicTok;
    }

    return Tok;
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
    OS << mnemonicTok("setret");
    OS << "\t";
    if (!emitSetRetTarget())
      OS << "0x0";
    OS << ",\t->ra";
    printAnnotation(OS, Annot);
    return;
  }

  // Special-case: accelerator/tile block-start instructions (BSTART.TMA/CUBE).
  //
  // These are not control-flow split markers like BSTART.{DIRECT,CALL,COND}.
  // Treat them as normal instructions and print the functional selector.
  if (AsmFmt.starts_with("BSTART.TMA") || AsmFmt.starts_with("BSTART.CUBE")) {
    const StringRef Tok = stripAngleSuffix(RawTok);
    const unsigned Func =
        static_cast<unsigned>(findFieldImm("Function").value_or(0)) & 0x1fu;
    const unsigned DT =
        static_cast<unsigned>(findFieldImm("DataType").value_or(0)) & 0x1fu;

    OS << Tok;
    OS << "\t";

    auto emitDt = [&]() { OS << ", dt" << utostr(DT); };

    if (AsmFmt.starts_with("BSTART.TMA")) {
      switch (Func) {
      case 0:
        OS << "TLOAD";
        break;
      case 1:
        OS << "TSTORE";
        break;
      default:
        OS << "TMA_OP" << utostr(Func);
        break;
      }
      emitDt();
    } else {
      switch (Func) {
      case 0:
        OS << "MAMULB";
        break;
      case 8:
        OS << "ACCCVT";
        break;
      default:
        OS << "CUBE_OP" << utostr(Func);
        break;
      }
      emitDt();
    }

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
    StringRef FirstTok = RawTok;
    StringRef FirstTokBase = stripAngleSuffix(FirstTok);
    const bool HasBlockTypePlaceholder = AsmFmt.contains("<.BlockType>");

    // Render C.BSTART<.BlockType> as C.BSTART.<suffix>.
    SmallString<32> PrintedMnemonic;
    if (HasBlockTypePlaceholder) {
      unsigned BT = 0;
      if (auto V = findFieldImm("BlockType"))
        BT = static_cast<unsigned>(*V);
      PrintedMnemonic = FirstTokBase;
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
    } else if (FirstTokBase == "C.BSTART" &&
               (AsmFmt.contains(" DIRECT") || AsmFmt.contains(" COND"))) {
      // These encodings are scalar-block forms; the default BlockType is STD.
      OS << "C.BSTART";
    } else if (FirstTokBase == "BSTART" &&
               (AsmFmt.contains("{DIRECT, CALL}") || AsmFmt.contains(" COND"))) {
      // These encodings are scalar-block forms; the default BlockType is STD.
      OS << "BSTART";
    } else if (FirstTokBase == "C.BSTART.STD") {
      OS << "C.BSTART";
    } else if (FirstTokBase == "BSTART.STD") {
      OS << "BSTART";
    } else {
      OS << FirstTokBase;
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
          // `->u` is the explicit U-hand queue push selector.
          OS << ",\t->u";
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
      OS << ",\t->u";
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

  // Special-case: tile block IO descriptors (B.IOT / B.IOTI).
  //
  // These use bracket syntax in the ISA, but they are not memory operands and
  // should not be routed through the generic load/store pretty printer.
  if (AsmFmt.starts_with("B.IOT")) {
    const bool IsIOTI = AsmFmt.starts_with("B.IOTI");
    const unsigned S0V =
        static_cast<unsigned>(findFieldImm("S0V").value_or(0)) & 0x1u;
    const unsigned S1V =
        static_cast<unsigned>(findFieldImm("S1V").value_or(0)) & 0x1u;
    const unsigned S0R =
        static_cast<unsigned>(findFieldImm("S0R").value_or(0)) & 0x1u;
    const unsigned S1R =
        static_cast<unsigned>(findFieldImm("S1R").value_or(0)) & 0x1u;
    const unsigned DstTile =
        static_cast<unsigned>(findFieldImm("DstTile").value_or(0)) & 0x7u;
    const unsigned Src0 =
        static_cast<unsigned>(findFieldImm("SrcTile0").value_or(0)) & 0x1fu;
    const unsigned Src1 =
        static_cast<unsigned>(findFieldImm("SrcTile1").value_or(0)) & 0x1fu;
    const unsigned Reg =
        static_cast<unsigned>(findFieldImm("RegSrc").value_or(0)) & 0x1fu;
    std::optional<int64_t> SizeOpt = findFieldImm("Size");
    if (!SizeOpt)
      SizeOpt = findFieldImm("imm5");
    if (!SizeOpt)
      SizeOpt = findFieldImm("uimm5");
    const unsigned Size = static_cast<unsigned>(SizeOpt.value_or(0)) & 0x1fu;

    OS << (IsIOTI ? "B.IOTI" : "B.IOT");
    OS << "\t[";

    bool First = true;
    if (S0V) {
      OS << "tile" << utostr(Src0);
      if (S0R)
        OS << ".reuse";
      First = false;
    }
    if (S1V) {
      if (!First)
        OS << ", ";
      OS << "tile" << utostr(Src1);
      if (S1R)
        OS << ".reuse";
    }

    OS << "], ";
    OS << (AsmFmt.contains("group=1") ? "group=1" : "group=0");
    OS << ", ->tile" << utostr(DstTile) << "<";
    if (IsIOTI) {
      OS << utostr(Size);
    } else {
      OS << reg5Name(Reg);
    }
    OS << ">";

    printAnnotation(OS, Annot);
    return;
  }

  // Special-case: block argument registers (B.DIM).
  //
  // The LB destinations are pseudo-registers in the ISA syntax and are not
  // modeled as normal `->RegDst` operands.
  if (AsmFmt.starts_with("B.DIM")) {
    const unsigned Reg =
        static_cast<unsigned>(findFieldImm("RegSrc").value_or(0)) & 0x1fu;
    std::optional<int64_t> UimmOpt = findFieldImm("uimm");
    if (!UimmOpt)
      UimmOpt = findFieldImm("uimm17");
    if (!UimmOpt)
      UimmOpt = findFieldImm("imm17");
    const unsigned Uimm = static_cast<unsigned>(UimmOpt.value_or(0)) & 0x1ffffu;
    unsigned Lb = 0;
    if (AsmFmt.contains("->LB1"))
      Lb = 1;
    else if (AsmFmt.contains("->LB2"))
      Lb = 2;

    OS << "B.DIM\t" << reg5Name(Reg) << ", " << Uimm << ", ->LB" << utostr(Lb);
    printAnnotation(OS, Annot);
    return;
  }

  // Pretty printer for memory operands.
  if (AsmFmt.contains('[')) {
    const StringRef Mnem = Form.mnemonic ? StringRef(Form.mnemonic) : StringRef();
    const bool IsPcr = Mnem.ends_with(".PCR");
    if (IsPcr) {
      // Prefer a symbol-first syntax for PC-relative accesses:
      //   lw.pcr <sym+addend>, ->rd
      //   sw.pcr rs, <sym+addend>
      OS << mnemonicTok("<pcr>");
      OS << "\t";

      // Loads: (RegDst, simm17) or (RegDst, simm) for HL.*.PCR.
      // Stores: (SrcL, simm).
      auto emitPcrExpr = [&]() {
        OS << "[";
        if (auto Op = findField("simm17")) {
          if (Op->isExpr())
            MAI.printExpr(OS, *Op->getExpr());
          else
            OS << "0x" << utohexstr(static_cast<uint64_t>(Op->getImm()),
                                    /*LowerCase=*/true);
          OS << "]";
          return true;
        }
        if (auto Op = findField("simm")) {
          if (Op->isExpr())
            MAI.printExpr(OS, *Op->getExpr());
          else
            OS << "0x" << utohexstr(static_cast<uint64_t>(Op->getImm()),
                                    /*LowerCase=*/true);
          OS << "]";
          return true;
        }
        OS << "]";
        return false;
      };

      const bool HasDest = AsmFmt.contains("->");
      if (HasDest) {
        if (!emitPcrExpr())
          OS << "0x0";
        emitArrowDest();
        printAnnotation(OS, Annot);
        return;
      }

      // Store: value first, then the symbol.
      emitReg("SrcL");
      OS << ", ";
      if (!emitPcrExpr())
        OS << "0x0";
      printAnnotation(OS, Annot);
      return;
    }

    OS << mnemonicTok("<mem>");
    OS << "\t";

    const bool HasDest = AsmFmt.contains("->");

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
  if (Form.mnemonic && StringRef(Form.mnemonic).equals_insensitive("CSEL") &&
      findField("SrcP") && findField("SrcL") && findField("SrcR") &&
      findField("SrcRType")) {
    OS << mnemonicTok("<op>");
    OS << "\t";
    emitReg("SrcP");
    OS << ", ";
    emitReg("SrcL");
    OS << ", ";
    emitSrcRWithTypeAndShift(/*ForcedShift=*/std::nullopt);
    if (AsmFmt.contains("->"))
      emitArrowDest();
    printAnnotation(OS, Annot);
    return;
  }

  // Pretty printer for the common "SrcL, SrcR<type><<shamt>" operand form.
  if (findField("SrcL") && findField("SrcR") && findField("SrcRType") &&
      !findField("SrcP") && !findField("SrcD") && !findField("SrcA") &&
      !AsmFmt.contains('[')) {
    OS << mnemonicTok("<op>");
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
    OS << mnemonicTok("<invalid>");
  else if (Form.mnemonic && Form.mnemonic[0])
    OS << StringRef(Form.mnemonic).lower();
  else
    OS << "<invalid>";

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
        Name.equals_insensitive("BlockType") || Name.equals_insensitive("SSR_ID") ||
        Name.equals_insensitive("SSRID")) {
      sep();
      const MCOperand &Op = KV.second;
      if (Op.isImm()) {
        if (Name.equals_insensitive("SSR_ID") || Name.equals_insensitive("SSRID")) {
          if (const char *Sym = ssrIdSymbol(static_cast<uint64_t>(Op.getImm()))) {
            OS << Sym;
          } else {
            OS << "0x" << utohexstr(static_cast<uint64_t>(Op.getImm()) & 0xfffu);
          }
        } else {
          OS << Op.getImm();
        }
      }
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
        OS << "->u";
      else
        OS << "->" << reg5Name(Code);
    }
  } else if (!AsmFmt.empty()) {
    if (asmImpliesArrowDest(AsmFmt, "->t")) {
      dstSep();
      OS << "->t";
    } else if (asmImpliesArrowDest(AsmFmt, "->u")) {
      dstSep();
      OS << "->u";
    } else if (asmImpliesArrowDest(AsmFmt, "->ra")) {
      dstSep();
      OS << "->ra";
    }
  }

  printAnnotation(OS, Annot);
}
