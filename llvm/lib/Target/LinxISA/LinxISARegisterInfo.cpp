//===-- LinxISARegisterInfo.cpp - LinxISA Register Information ------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LinxISARegisterInfo.h"
#include "LinxISAFrameLowering.h"
#include "LinxISAInstrInfo.h"
#include "MCTargetDesc/LinxISAMCTargetDesc.h"
#include "llvm/CodeGen/MachineFrameInfo.h"
#include "llvm/CodeGen/MachineFunction.h"
#include "llvm/CodeGen/MachineInstr.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/RegisterScavenging.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/MathExtras.h"

#define GET_REGINFO_TARGET_DESC
#include "LinxISAGenRegisterInfo.inc"

using namespace llvm;

LinxISARegisterInfo::LinxISARegisterInfo()
    : LinxISAGenRegisterInfo(LinxISA::R10) {}

const MCPhysReg *
LinxISARegisterInfo::getCalleeSavedRegs(const MachineFunction *MF) const {
  return CSR_Linx_SaveList;
}

const uint32_t *
LinxISARegisterInfo::getCallPreservedMask(const MachineFunction &MF,
                                          CallingConv::ID CC) const {
  return CSR_Linx_RegMask;
}

BitVector LinxISARegisterInfo::getReservedRegs(const MachineFunction &MF) const {
  BitVector Reserved(getNumRegs());
  Reserved.set(LinxISA::R0);
  Reserved.set(LinxISA::R1); // sp
  Reserved.set(LinxISA::R10); // ra
  Reserved.set(LinxISA::T1);
  Reserved.set(LinxISA::T2);
  Reserved.set(LinxISA::T3);
  Reserved.set(LinxISA::T4);
  Reserved.set(LinxISA::U1);
  Reserved.set(LinxISA::U2);
  Reserved.set(LinxISA::U3);
  Reserved.set(LinxISA::U4);
  return Reserved;
}

Register LinxISARegisterInfo::getFrameRegister(const MachineFunction &MF) const {
  return LinxISA::R1; // sp
}

static int64_t getMemScale(unsigned Opcode) {
  switch (Opcode) {
  case LinxISA::ADDIri:
  case LinxISA::SUBIri:
  case LinxISA::ADDIWri:
  case LinxISA::SUBIWri:
    return 1;
  case LinxISA::LBI:
  case LinxISA::LBUI:
  case LinxISA::SBI:
    return 1;
  case LinxISA::LHI:
  case LinxISA::LHUI:
  case LinxISA::SHI:
    return 2;
  case LinxISA::LWI:
  case LinxISA::LWUI:
  case LinxISA::SWI:
    return 4;
  case LinxISA::LDI:
  case LinxISA::SDI:
    return 8;
  default:
    return 1;
  }
}

bool LinxISARegisterInfo::eliminateFrameIndex(MachineBasicBlock::iterator II,
                                              int SPAdj,
                                              unsigned FIOperandNum,
                                              RegScavenger *RS) const {
  MachineInstr &MI = *II;
  MachineFunction &MF = *MI.getParent()->getParent();
  const auto &TII = *MF.getSubtarget().getInstrInfo();
  const MachineFrameInfo &MFI = MF.getFrameInfo();

  const int FrameIndex = MI.getOperand(FIOperandNum).getIndex();

  // The frame index offset is computed relative to the stack pointer value at
  // the end of the prologue. If this instruction is inside a call sequence,
  // SP has been adjusted by SPAdj (tracked by PEI); incorporate it here.
  int64_t OffsetBytes =
      MFI.getObjectOffset(FrameIndex) + MFI.getStackSize() + SPAdj;

  // Reg-offset addressing forms do not have an immediate displacement field;
  // materialize the stack-object base address into a temporary register.
  switch (MI.getOpcode()) {
  case LinxISA::LB:
  case LinxISA::LBU:
  case LinxISA::LH:
  case LinxISA::LHU:
  case LinxISA::LW:
  case LinxISA::LWU:
  case LinxISA::LD:
  case LinxISA::SB:
  case LinxISA::SH:
  case LinxISA::SW:
  case LinxISA::SD: {
    if (OffsetBytes == 0) {
      MI.getOperand(FIOperandNum)
          .ChangeToRegister(LinxISA::R1, /*isDef=*/false);
      return false;
    }

    int64_t AbsOff = OffsetBytes < 0 ? -OffsetBytes : OffsetBytes;
    if (!isUInt<12>(AbsOff))
      report_fatal_error("Linx: stack frame offset out of range");

    if (!RS)
      report_fatal_error("Linx: frame index elimination requires RegScavenger");

    // PEI runs after register allocation. Use the register scavenger instead
    // of creating new virtual registers.
    Register BaseReg =
        RS->scavengeRegisterBackwards(LinxISA::GPRRegClass, II,
                                      /*RestoreAfter=*/true, SPAdj,
                                      /*AllowSpill=*/true);
    if (!BaseReg)
      report_fatal_error("Linx: failed to scavenge a scratch register");

    DebugLoc DL = MI.getDebugLoc();
    if (OffsetBytes > 0) {
      BuildMI(*MI.getParent(), II, DL, TII.get(LinxISA::ADDIri), BaseReg)
          .addReg(LinxISA::R1)
          .addImm(OffsetBytes);
    } else {
      BuildMI(*MI.getParent(), II, DL, TII.get(LinxISA::SUBIri), BaseReg)
          .addReg(LinxISA::R1)
          .addImm(AbsOff);
    }

    MI.getOperand(FIOperandNum).ChangeToRegister(BaseReg, /*isDef=*/false);
    return false;
  }
  default:
    break;
  }

  // Tile pseudo stack accesses carry a FrameIndex in operand #0. Materialize
  // a base register and rewrite the FrameIndex operand into that register.
  switch (MI.getOpcode()) {
  case LinxISA::PSEUDO_TMA_TLOAD:
  case LinxISA::PSEUDO_TMA_TLOAD_ANY:
  case LinxISA::PSEUDO_TMA_TSTORE: {
    if (OffsetBytes == 0) {
      MI.getOperand(FIOperandNum).ChangeToRegister(LinxISA::R1, /*isDef=*/false);
      return false;
    }

    if (!RS) {
      report_fatal_error("Linx: tile frame index elimination requires RegScavenger");
    }

    Register BaseReg =
        RS->scavengeRegisterBackwards(LinxISA::GPRRegClass, II,
                                      /*RestoreAfter=*/true, SPAdj,
                                      /*AllowSpill=*/true);
    if (!BaseReg) {
      report_fatal_error("Linx: failed to scavenge scratch register for tile spill");
    }

    DebugLoc DL = MI.getDebugLoc();
    const int64_t AbsOff = OffsetBytes < 0 ? -OffsetBytes : OffsetBytes;
    const bool IsPos = OffsetBytes > 0;
    if (isUInt<12>(AbsOff)) {
      BuildMI(*MI.getParent(), II, DL,
              TII.get(IsPos ? LinxISA::ADDIri : LinxISA::SUBIri), BaseReg)
          .addReg(LinxISA::R1)
          .addImm(AbsOff);
    } else if (isUInt<24>(AbsOff)) {
      BuildMI(*MI.getParent(), II, DL,
              TII.get(IsPos ? LinxISA::HLADDIri : LinxISA::HLSUBIri), BaseReg)
          .addReg(LinxISA::R1)
          .addImm(AbsOff);
    } else {
      report_fatal_error("Linx: tile spill stack frame offset out of range");
    }

    MI.getOperand(FIOperandNum).ChangeToRegister(BaseReg, /*isDef=*/false);
    return false;
  }
  default:
    break;
  }

  const int64_t Scale = getMemScale(MI.getOpcode());
  const bool UnsignedImm =
      MI.getOpcode() == LinxISA::ADDIri || MI.getOpcode() == LinxISA::SUBIri ||
      MI.getOpcode() == LinxISA::ADDIWri || MI.getOpcode() == LinxISA::SUBIWri;
  if (OffsetBytes % Scale != 0)
    report_fatal_error("Linx: stack object offset is not naturally aligned");

  int64_t ScaledOff = OffsetBytes / Scale;
  if (MI.getOperand(FIOperandNum + 1).isImm())
    ScaledOff += MI.getOperand(FIOperandNum + 1).getImm();

  if (UnsignedImm ? !isUInt<12>(ScaledOff) : !isInt<12>(ScaledOff))
    report_fatal_error("Linx: stack frame offset out of range");

  MI.getOperand(FIOperandNum).ChangeToRegister(LinxISA::R1, /*isDef=*/false);
  MI.getOperand(FIOperandNum + 1).ChangeToImmediate(ScaledOff);
  return false;
}
