//===-- LinxISAFrameLowering.cpp - Frame lowering for LinxISA -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file contains the LinxISA implementation of TargetFrameLowering.
// 
// LinxISA uses FENTRY and FRET.STK instructions for function prologue/epilogue.
// These are hardware macro instructions that expand to save/restore register
// sequences. The register range [ra ~ sN] specifies which registers to save.
//
//===----------------------------------------------------------------------===//

#include "LinxISAFrameLowering.h"
#include "LinxISAInstrInfo.h"
#include "MCTargetDesc/LinxISAMCTargetDesc.h"
#include "llvm/ADT/BitVector.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/CodeGen/TargetRegisterInfo.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

static bool shouldEmitFrameMacros(const MachineFunction &MF) {
  if (MF.getFunction().hasFnAttribute(Attribute::Naked))
    return false;

  const MachineFrameInfo &MFI = MF.getFrameInfo();
  return MFI.getStackSize() != 0 || !MFI.getCalleeSavedInfo().empty();
}

static std::pair<unsigned, unsigned>
getFentryRangeEnc(const MachineFunction &MF) {
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  // FENTRY/FRET save/restore a contiguous range starting at `ra`.
  const unsigned RegBeginEnc = TRI.getEncodingValue(LinxISA::R10);
  unsigned RegEndEnc = RegBeginEnc;

  for (const CalleeSavedInfo &CS : MFI.getCalleeSavedInfo())
    RegEndEnc = std::max<unsigned>(RegEndEnc, TRI.getEncodingValue(CS.getReg()));

  return {RegBeginEnc, RegEndEnc};
}

void LinxISAFrameLowering::determineCalleeSaves(MachineFunction &MF,
                                               BitVector &SavedRegs,
                                               RegScavenger *RS) const {
  TargetFrameLowering::determineCalleeSaves(MF, SavedRegs, RS);

  // If we're using the FENTRY/FRET macros, we must reserve stack space for the
  // full contiguous register range they will save/restore. Expand the
  // SavedRegs set so that if any callee-saved register is saved, then all
  // callee-saved registers from `ra` through the highest saved one are saved.
  //
  // This guarantees PEI allocates spill slots for the whole range, preventing
  // the macro save/restore microcode from clobbering local stack objects.
  if (MF.getFunction().hasFnAttribute(Attribute::Naked))
    return;

  // If the function has a stack frame but PEI didn't decide to save anything,
  // force saving `ra` so that FRET.STK has a valid restore slot.
  const MachineFrameInfo &MFI = MF.getFrameInfo();
  const bool HasFrame = MFI.hasStackObjects();

  const MCPhysReg *CSRs =
      MF.getSubtarget().getRegisterInfo()->getCalleeSavedRegs(&MF);
  unsigned LastIdx = 0;
  bool AnySaved = false;
  for (unsigned I = 0; CSRs[I] != 0; ++I) {
    if (!SavedRegs.test(CSRs[I]))
      continue;
    AnySaved = true;
    LastIdx = I;
  }

  if (!AnySaved) {
    if (!HasFrame)
      return;
    // Ensure `ra` is saved at least.
    SavedRegs.set(LinxISA::R10);
    AnySaved = true;
    LastIdx = 0;
  }

  for (unsigned I = 0; I <= LastIdx; ++I)
    SavedRegs.set(CSRs[I]);
}

void LinxISAFrameLowering::processFunctionBeforeFrameFinalized(
    MachineFunction &MF, RegScavenger *RS) const {
  if (!RS)
    return;

  // LinxISA frequently needs a post-RA scratch register when eliminating frame
  // indices for reg-offset loads/stores (e.g. stack arrays indexed by a
  // runtime value). Provide an emergency spill slot for the register scavenger.
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const TargetRegisterInfo &TRI = *MF.getSubtarget().getRegisterInfo();
  const TargetRegisterClass *RC = &LinxISA::GPRRegClass;

  int FI = MFI.CreateSpillStackObject(TRI.getSpillSize(*RC),
                                      TRI.getSpillAlign(*RC));
  RS->addScavengingFrameIndex(FI);
}

void LinxISAFrameLowering::emitPrologue(MachineFunction &MF,
                                       MachineBasicBlock &MBB) const {
  if (!shouldEmitFrameMacros(MF))
    return;

  MachineFrameInfo &MFI = MF.getFrameInfo();
  const uint64_t StackSize = MFI.getStackSize();
  if (!isUInt<15>(StackSize) || (StackSize & 7) != 0)
    report_fatal_error("Linx: invalid stack size for FENTRY/FRET (must be 15-bit, 8-byte aligned)");

  const LinxISAInstrInfo &TII =
      *static_cast<const LinxISAInstrInfo *>(MF.getSubtarget().getInstrInfo());

  auto [RegBeginEnc, RegEndEnc] = getFentryRangeEnc(MF);

  // The FENTRY macro is a standalone block instruction. Emit it in a dedicated
  // prologue block that falls through to the original entry block.
  MachineBasicBlock *PrologueBB = MF.CreateMachineBasicBlock(MBB.getBasicBlock());
  MF.insert(MBB.getIterator(), PrologueBB);
  PrologueBB->addSuccessor(&MBB);

  for (const MachineBasicBlock::RegisterMaskPair &LI : MBB.liveins())
    PrologueBB->addLiveIn(LI);
  PrologueBB->sortUniqueLiveIns();

  BuildMI(*PrologueBB, PrologueBB->end(), DebugLoc(), TII.get(LinxISA::FENTRY))
      .addImm(RegBeginEnc)
      .addImm(RegEndEnc)
      .addImm(StackSize);
}

void LinxISAFrameLowering::emitEpilogue(MachineFunction &MF,
                                       MachineBasicBlock &MBB) const {
  if (!shouldEmitFrameMacros(MF))
    return;

  MachineFrameInfo &MFI = MF.getFrameInfo();
  const uint64_t StackSize = MFI.getStackSize();
  if (!isUInt<15>(StackSize) || (StackSize & 7) != 0)
    report_fatal_error("Linx: invalid stack size for FENTRY/FRET (must be 15-bit, 8-byte aligned)");

  const LinxISAInstrInfo &TII =
      *static_cast<const LinxISAInstrInfo *>(MF.getSubtarget().getInstrInfo());

  auto [RegBeginEnc, RegEndEnc] = getFentryRangeEnc(MF);

  MachineInstr *RetMI = nullptr;
  for (MachineInstr &MI : llvm::reverse(MBB)) {
    if (MI.isDebugInstr())
      continue;
    if (MI.isReturn()) {
      RetMI = &MI;
      break;
    }
    // No return terminator found.
    break;
  }

  if (!RetMI)
    return;

  SmallVector<Register, 8> RetValRegs;
  for (const MachineOperand &MO : RetMI->operands()) {
    if (!MO.isReg() || MO.isDef() || MO.isImplicit())
      continue;
    if (Register Reg = MO.getReg())
      RetValRegs.push_back(Reg);
  }
  RetMI->eraseFromParent();

  // The FRET macro is a standalone block instruction. Emit it in a dedicated
  // epilogue block and fall through to it from the original return block.
  MachineBasicBlock *EpilogueBB = MF.CreateMachineBasicBlock(MBB.getBasicBlock());
  MF.insert(std::next(MBB.getIterator()), EpilogueBB);
  MBB.addSuccessor(EpilogueBB);

  MachineInstrBuilder MIB =
      BuildMI(*EpilogueBB, EpilogueBB->end(), DebugLoc(),
              TII.get(LinxISA::FRET_STK))
          .addImm(RegBeginEnc)
          .addImm(RegEndEnc)
          .addImm(StackSize);
  for (Register Reg : RetValRegs)
    MIB.addReg(Reg, RegState::Implicit);
}

bool LinxISAFrameLowering::spillCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    ArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  // Callee-saved register saves are performed by the FENTRY macro.
  (void)MBB;
  (void)MI;
  (void)TRI;
  return !CSI.empty();
}

bool LinxISAFrameLowering::restoreCalleeSavedRegisters(
    MachineBasicBlock &MBB, MachineBasicBlock::iterator MI,
    MutableArrayRef<CalleeSavedInfo> CSI, const TargetRegisterInfo *TRI) const {
  // Callee-saved register restores are performed by the FRET macro.
  (void)MBB;
  (void)MI;
  (void)TRI;
  if (CSI.empty())
    return false;
  for (CalleeSavedInfo &CS : CSI)
    CS.setRestored(true);
  return true;
}

MachineBasicBlock::iterator LinxISAFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MI) const {
  // With FENTRY/FRET.STK, we don't need dynamic call frame adjustments
  // The stack is fixed at function entry
  // Just remove the pseudo instructions
  return MBB.erase(MI);
}
