#ifndef LLVM_LIB_TARGET_LINXISA_MCTARGETDESC_LINXISAMCTARGETDESC_H
#define LLVM_LIB_TARGET_LINXISA_MCTARGETDESC_LINXISAMCTARGETDESC_H

#include "llvm/MC/MCInstrInfo.h"
#include "llvm/Support/CodeGen.h"
#include "llvm/Support/Compiler.h"
#include <memory>

namespace llvm {

class MCAsmBackend;
class MCCodeEmitter;
class MCContext;
class MCObjectTargetWriter;
class MCRegisterInfo;
class MCSubtargetInfo;
class MCTargetOptions;
class Target;

Target &getTheLinx32Target();
Target &getTheLinx64Target();

MCCodeEmitter *createLinxISAMCCodeEmitter(const MCInstrInfo &MII,
                                          MCContext &Ctx);

MCAsmBackend *createLinxISAAsmBackend(const Target &T,
                                      const MCSubtargetInfo &STI,
                                      const MCRegisterInfo &MRI,
                                      const MCTargetOptions &Options);

std::unique_ptr<MCObjectTargetWriter>
createLinxISAELFObjectWriter(uint8_t OSABI, bool Is64Bit);

} // namespace llvm

// Defines symbolic names for LinxISA registers.
#define GET_REGINFO_ENUM
#include "LinxISAGenRegisterInfo.inc"

// Defines symbolic names for LinxISA instructions.
#define GET_INSTRINFO_ENUM
#define GET_INSTRINFO_MC_HELPER_DECLS
#include "LinxISAGenInstrInfo.inc"

// Defines symbolic names for LinxISA subtarget features.
#define GET_SUBTARGETINFO_ENUM
#include "LinxISAGenSubtargetInfo.inc"

namespace llvm {
namespace LinxISA {
// Compatibility: older code references LinxISA:: for registers/opcodes, while
// TableGen now places them under the Linx:: namespace.
using namespace Linx;
} // namespace LinxISA
} // namespace llvm

#endif // LLVM_LIB_TARGET_LINXISA_MCTARGETDESC_LINXISAMCTARGETDESC_H
