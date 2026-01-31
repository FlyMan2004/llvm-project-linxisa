//===-- LinxISAFrameLowering.cpp - Frame lowering for LinxISA -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LinxISAFrameLowering.h"
#include "LinxISAInstrInfo.h"
#include "MCTargetDesc/LinxISAMCTargetDesc.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"

using namespace llvm;

void LinxISAFrameLowering::emitPrologue(MachineFunction &MF,
                                       MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const uint64_t StackSize = MFI.getStackSize();
  if (!StackSize)
    return;

  if (!isUInt<12>(StackSize))
    report_fatal_error("Linx: stack size out of range for SUBI");

  const LinxISAInstrInfo &TII =
      *static_cast<const LinxISAInstrInfo *>(MF.getSubtarget().getInstrInfo());

  MachineBasicBlock::iterator InsertPt = MBB.begin();
  while (InsertPt != MBB.end() && InsertPt->isDebugInstr())
    ++InsertPt;

  BuildMI(MBB, InsertPt, DebugLoc(), TII.get(LinxISA::SUBIri), LinxISA::R1)
      .addReg(LinxISA::R1)
      .addImm(StackSize);
}

void LinxISAFrameLowering::emitEpilogue(MachineFunction &MF,
                                       MachineBasicBlock &MBB) const {
  MachineFrameInfo &MFI = MF.getFrameInfo();
  const uint64_t StackSize = MFI.getStackSize();
  if (!StackSize)
    return;

  if (!isUInt<12>(StackSize))
    report_fatal_error("Linx: stack size out of range for ADDI");

  const LinxISAInstrInfo &TII =
      *static_cast<const LinxISAInstrInfo *>(MF.getSubtarget().getInstrInfo());

  MachineBasicBlock::iterator InsertPt = MBB.end();
  while (InsertPt != MBB.begin()) {
    auto Prev = std::prev(InsertPt);
    if (!Prev->isDebugInstr()) {
      InsertPt = Prev;
      break;
    }
    InsertPt = Prev;
  }

  BuildMI(MBB, InsertPt, DebugLoc(), TII.get(LinxISA::ADDIri), LinxISA::R1)
      .addReg(LinxISA::R1)
      .addImm(StackSize);
}

MachineBasicBlock::iterator LinxISAFrameLowering::eliminateCallFramePseudoInstr(
    MachineFunction &MF, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator MI) const {
  const LinxISAInstrInfo &TII =
      *static_cast<const LinxISAInstrInfo *>(MF.getSubtarget().getInstrInfo());

  const DebugLoc &DL = MI->getDebugLoc();
  unsigned Opc = MI->getOpcode();
  uint64_t Amount = MI->getOperand(0).getImm();

  // The second immediate (amt2) is currently unused for LinxISA.
  if (Amount) {
    if (!isUInt<12>(Amount))
      report_fatal_error("Linx: call frame adjustment out of range");

    if (Opc == LinxISA::ADJCALLSTACKDOWN) {
      BuildMI(MBB, MI, DL, TII.get(LinxISA::SUBIri), LinxISA::R1)
          .addReg(LinxISA::R1)
          .addImm(Amount);
    } else if (Opc == LinxISA::ADJCALLSTACKUP) {
      BuildMI(MBB, MI, DL, TII.get(LinxISA::ADDIri), LinxISA::R1)
          .addReg(LinxISA::R1)
          .addImm(Amount);
    } else {
      llvm_unreachable("Unexpected call frame pseudo");
    }
  }

  return MBB.erase(MI);
}
