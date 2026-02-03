#include "MCTargetDesc/LinxISAOpcodeTables.h"
#include "TargetInfo/LinxISATargetInfo.h"
#include "llvm/ADT/ArrayRef.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/MC/MCContext.h"
#include "llvm/MC/MCDisassembler/MCDisassembler.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/Support/MathExtras.h"
#include <cstdint>

using namespace llvm;

namespace {

class LinxISADisassembler : public MCDisassembler {
public:
  LinxISADisassembler(const MCSubtargetInfo &STI, MCContext &Ctx)
      : MCDisassembler(STI, Ctx) {}

  DecodeStatus getInstruction(MCInst &Instr, uint64_t &Size,
                              ArrayRef<uint8_t> Bytes, uint64_t Address,
                              raw_ostream &CStream) const override;
};

} // namespace

static MCDisassembler *createLinxISADisassembler(const Target & /*T*/,
                                                 const MCSubtargetInfo &STI,
                                                 MCContext &Ctx) {
  return new LinxISADisassembler(STI, Ctx);
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeLinxISADisassembler() {
  TargetRegistry::RegisterMCDisassembler(getTheLinx32Target(),
                                         createLinxISADisassembler);
  TargetRegistry::RegisterMCDisassembler(getTheLinx64Target(),
                                         createLinxISADisassembler);
}

static uint64_t readLE(ArrayRef<uint8_t> Bytes, unsigned SizeBytes) {
  uint64_t V = 0;
  for (unsigned i = 0; i < SizeBytes; ++i)
    V |= uint64_t(Bytes[i]) << (8 * i);
  return V;
}

static int64_t signExtend(uint64_t V, unsigned Bits) {
  if (Bits == 0 || Bits >= 64)
    return static_cast<int64_t>(V);
  uint64_t SignBit = 1ULL << (Bits - 1);
  if (V & SignBit)
    V |= (~0ULL) << Bits;
  return static_cast<int64_t>(V);
}

static bool isSupportedLength(unsigned Bits) {
  return Bits == 16 || Bits == 32 || Bits == 48 || Bits == 64;
}

static const linxisa_inst_form *findMatch(uint64_t Insn, unsigned Bits,
                                          unsigned &OutOpcode) {
  // Best-effort: pick the matching form with the most fixed bits.
  unsigned Best = ~0U;
  unsigned BestFixed = 0;

  for (unsigned i = 0; i < linxisa_inst_forms_count; ++i) {
    const linxisa_inst_form &F = linxisa_inst_forms[i];
    if (unsigned(F.length_bits) != Bits)
      continue;
    if ((Insn & F.mask) != F.match)
      continue;
    unsigned Fixed = llvm::popcount(static_cast<uint64_t>(F.mask));
    if (Best == ~0U || Fixed > BestFixed) {
      Best = i;
      BestFixed = Fixed;
    }
  }

  if (Best == ~0U)
    return nullptr;
  OutOpcode = Best;
  return &linxisa_inst_forms[Best];
}

static void extractFields(const linxisa_inst_form &Form, uint64_t Insn,
                          SmallVectorImpl<int64_t> &Out) {
  Out.clear();
  for (unsigned i = 0; i < Form.field_count; ++i) {
    const linxisa_field &F = linxisa_fields[Form.field_start + i];
    uint64_t Val = 0;
    for (unsigned j = 0; j < F.piece_count; ++j) {
      const linxisa_field_piece &P = linxisa_field_pieces[F.piece_start + j];
      uint64_t Bits = (Insn >> P.insn_lsb) & ((1ULL << P.width) - 1);
      Val |= Bits << P.value_lsb;
    }

    int64_t SignedVal = static_cast<int64_t>(Val);
    if (F.signed_hint == 1)
      SignedVal = signExtend(Val, F.bit_width);
    Out.push_back(SignedVal);
  }
}

static bool isBStartCall(const linxisa_inst_form &Form) {
  StringRef AsmFmt(Form.asm_fmt ? Form.asm_fmt : "");
  return (AsmFmt.starts_with_insensitive("bstart") ||
          AsmFmt.starts_with_insensitive("hl.bstart")) &&
         AsmFmt.contains_insensitive(" call,");
}

static bool isSetRet(const linxisa_inst_form &Form) {
  StringRef AsmFmt(Form.asm_fmt ? Form.asm_fmt : "");
  return AsmFmt.starts_with_insensitive("setret") ||
         AsmFmt.starts_with_insensitive("c.setret") ||
         AsmFmt.starts_with_insensitive("hl.setret");
}

static bool isSignedSetRet(const linxisa_inst_form &Form) {
  StringRef AsmFmt(Form.asm_fmt ? Form.asm_fmt : "");
  return AsmFmt.starts_with_insensitive("hl.setret");
}

MCDisassembler::DecodeStatus LinxISADisassembler::getInstruction(
    MCInst &Instr, uint64_t &Size, ArrayRef<uint8_t> Bytes, uint64_t Address,
    raw_ostream & /*CStream*/) const {
  // LinxISA has overlapping encodings across lengths (e.g. templates/prefixes).
  // Prefer the longest matching encoding to keep the disassembler in sync.
  static constexpr unsigned CandidateBits[] = {64, 48, 32, 16};

  unsigned MatchedOpcode = 0;
  const linxisa_inst_form *Matched = nullptr;
  unsigned MatchedBits = 0;

  for (unsigned Bits : CandidateBits) {
    if (!isSupportedLength(Bits))
      continue;
    unsigned SizeBytes = Bits / 8;
    if (Bytes.size() < SizeBytes)
      continue;
    uint64_t Insn = readLE(Bytes, SizeBytes);
    unsigned Opcode = 0;
    const linxisa_inst_form *Form = findMatch(Insn, Bits, Opcode);
    if (!Form)
      continue;
    Matched = Form;
    MatchedOpcode = Opcode;
    MatchedBits = Bits;
    break;
  }

  if (!Matched) {
    // Fallback size: advance by 2 bytes if possible, otherwise fail.
    Size = Bytes.size() >= 2 ? 2 : 0;
    return Fail;
  }

  Size = MatchedBits / 8;
  uint64_t Insn = readLE(Bytes, Size);

  Instr.clear();
  Instr.setOpcode(MatchedOpcode);

  SmallVector<int64_t, 16> FieldVals;
  extractFields(*Matched, Insn, FieldVals);
  for (int64_t V : FieldVals)
    Instr.addOperand(MCOperand::createImm(V));

  // Disassembler sugar: fuse `BSTART ... CALL` + `SETRET` into a single
  // printed instruction, while still consuming both encodings.
  if (isBStartCall(*Matched)) {
    const uint64_t BStartSize = Size;
    ArrayRef<uint8_t> Tail = Bytes.drop_front(BStartSize);

    const linxisa_inst_form *NextForm = nullptr;
    unsigned NextBits = 0;

    for (unsigned Bits : CandidateBits) {
      if (!isSupportedLength(Bits))
        continue;
      unsigned SizeBytes = Bits / 8;
      if (Tail.size() < SizeBytes)
        continue;
      uint64_t NextInsn = readLE(Tail, SizeBytes);
      unsigned TmpOpcode = 0;
      const linxisa_inst_form *Form = findMatch(NextInsn, Bits, TmpOpcode);
      if (!Form)
        continue;
      NextForm = Form;
      NextBits = Bits;
      break;
    }

    if (NextForm && isSetRet(*NextForm)) {
      const uint64_t NextSize = NextBits / 8;
      uint64_t NextInsn = readLE(Tail, NextSize);
      SmallVector<int64_t, 16> NextFieldVals;
      extractFields(*NextForm, NextInsn, NextFieldVals);
      if (!NextFieldVals.empty()) {
        const int64_t Enc = NextFieldVals[0];
        const uint64_t SetRetAddr = Address + BStartSize;

        uint64_t Target = 0;
        if (isSignedSetRet(*NextForm)) {
          int64_t Delta = Enc;
          Delta <<= 1;
          Target = static_cast<uint64_t>(static_cast<int64_t>(SetRetAddr) + Delta);
        } else {
          uint64_t Delta = static_cast<uint64_t>(Enc) << 1;
          Target = SetRetAddr + Delta;
        }

        Instr.addOperand(MCOperand::createImm(static_cast<int64_t>(Target)));
        Size = BStartSize + NextSize;
      }
    }
  }

  return Success;
}
