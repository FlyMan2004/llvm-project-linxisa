#ifndef LLVM_LIB_TARGET_LINXISA_MCTARGETDESC_LINXISAINSTPRINTER_H
#define LLVM_LIB_TARGET_LINXISA_MCTARGETDESC_LINXISAINSTPRINTER_H

#include "llvm/MC/MCInstPrinter.h"

namespace llvm {

class LinxISAInstPrinter : public MCInstPrinter {
public:
  LinxISAInstPrinter(const MCAsmInfo &MAI, const MCInstrInfo &MII,
                     const MCRegisterInfo &MRI)
      : MCInstPrinter(MAI, MII, MRI) {}

  std::pair<const char *, uint64_t> getMnemonic(const MCInst &MI) const override;

  void printRegName(raw_ostream &OS, MCRegister Reg) override;

  void printInst(const MCInst *MI, uint64_t Address, StringRef Annot,
                 const MCSubtargetInfo &STI,
                 raw_ostream &OS) override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_LINXISA_MCTARGETDESC_LINXISAINSTPRINTER_H
