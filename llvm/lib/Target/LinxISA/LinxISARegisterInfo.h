//===-- LinxISARegisterInfo.h - LinxISA Register Information ---*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LINXISA_LINXISAREGISTERINFO_H
#define LLVM_LIB_TARGET_LINXISA_LINXISAREGISTERINFO_H

#include "llvm/CodeGen/TargetRegisterInfo.h"

#define GET_REGINFO_HEADER
#include "LinxISAGenRegisterInfo.inc"

namespace llvm {

class LinxISARegisterInfo : public LinxISAGenRegisterInfo {
public:
  LinxISARegisterInfo();

  const MCPhysReg *getCalleeSavedRegs(const MachineFunction *MF) const override;

  const uint32_t *getCallPreservedMask(const MachineFunction &MF,
                                       CallingConv::ID CC) const override;

  BitVector getReservedRegs(const MachineFunction &MF) const override;

  Register getFrameRegister(const MachineFunction &MF) const override;

  bool eliminateFrameIndex(MachineBasicBlock::iterator II, int SPAdj,
                           unsigned FIOperandNum,
                           RegScavenger *RS) const override;
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_LINXISA_LINXISAREGISTERINFO_H

