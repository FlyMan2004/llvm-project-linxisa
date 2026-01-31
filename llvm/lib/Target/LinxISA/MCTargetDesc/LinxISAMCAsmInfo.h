#ifndef LLVM_LIB_TARGET_LINXISA_MCTARGETDESC_LINXISAMCASMINFO_H
#define LLVM_LIB_TARGET_LINXISA_MCTARGETDESC_LINXISAMCASMINFO_H

#include "llvm/MC/MCAsmInfoELF.h"

namespace llvm {

class Triple;

class LinxISAMCAsmInfo : public MCAsmInfoELF {
public:
  LinxISAMCAsmInfo(const Triple &TT, bool Is64Bit);
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_LINXISA_MCTARGETDESC_LINXISAMCASMINFO_H
