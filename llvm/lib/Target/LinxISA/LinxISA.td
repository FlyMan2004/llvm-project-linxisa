// Minimal TableGen definitions for the LinxISA target (MC-layer bring-up).

include "llvm/Target/Target.td"

include "LinxISARegisterInfo.td"
include "LinxISACallingConv.td"
include "LinxISAInstrInfo.td"

defm : RemapAllTargetPseudoPointerOperands<GPR>;

def LinxISAInstrInfo : InstrInfo;

def : ProcessorModel<"generic", NoSchedModel, []>;

def LinxISA : Target {
  let InstructionSet = LinxISAInstrInfo;
}
