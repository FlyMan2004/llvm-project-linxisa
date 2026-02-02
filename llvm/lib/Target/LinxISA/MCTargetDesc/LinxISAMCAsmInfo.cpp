#include "MCTargetDesc/LinxISAMCAsmInfo.h"
#include "llvm/TargetParser/Triple.h"

using namespace llvm;

LinxISAMCAsmInfo::LinxISAMCAsmInfo(const Triple &TT, bool Is64Bit) {
  IsLittleEndian = true;
  CodePointerSize = Is64Bit ? 8 : 4;
  CalleeSaveStackSlotSize = Is64Bit ? 8 : 4;
  // Use "# " so tokens like `t#1` remain lexable (the lexer treats a single
  // '#' comment string as starting a comment anywhere in the line).
  CommentString = "# ";

  // TODO: fill in ELF/ABI details once the LinxISA ABI is defined.
}
