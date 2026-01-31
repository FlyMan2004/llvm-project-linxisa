#include "TargetInfo/LinxISATargetInfo.h"
#include "llvm/MC/TargetRegistry.h"
#include "llvm/Support/Compiler.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

Target &llvm::getTheLinx32Target() {
  static Target TheLinx32Target;
  return TheLinx32Target;
}

Target &llvm::getTheLinx64Target() {
  static Target TheLinx64Target;
  return TheLinx64Target;
}

extern "C" LLVM_ABI LLVM_EXTERNAL_VISIBILITY void
LLVMInitializeLinxISATargetInfo() {
  RegisterTarget<Triple::linx32> X32(getTheLinx32Target(), "linx32",
                                     "Linx (Linx Instruction Set Architecture) 32-bit",
                                     "Linx");
  RegisterTarget<Triple::linx64> X64(getTheLinx64Target(), "linx64",
                                     "Linx (Linx Instruction Set Architecture) 64-bit",
                                     "Linx");
}
