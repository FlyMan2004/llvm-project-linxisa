//===-- LinxISASubtarget.cpp - LinxISA Subtarget Information --------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LinxISASubtarget.h"
#include "LinxISAFrameLowering.h"
#include "LinxISAISelLowering.h"
#include "LinxISAInstrInfo.h"
#include "llvm/Target/TargetMachine.h"

#define DEBUG_TYPE "linx-subtarget"

#define GET_SUBTARGETINFO_TARGET_DESC
#define GET_SUBTARGETINFO_CTOR
#include "LinxISAGenSubtargetInfo.inc"

using namespace llvm;

LinxISASubtarget::LinxISASubtarget(const Triple &TT, StringRef CPU, StringRef FS,
                                   const TargetMachine &TM)
    : LinxISAGenSubtargetInfo(TT, CPU, /*TuneCPU=*/CPU, FS) {
  ParseSubtargetFeatures(CPU, /*TuneCPU=*/CPU, FS);
  InstrInfo = std::make_unique<LinxISAInstrInfo>(*this);
  FrameLowering = std::make_unique<LinxISAFrameLowering>();
  TLInfo = std::make_unique<LinxISATargetLowering>(TM, *this);
}

const LinxISAInstrInfo *LinxISASubtarget::getInstrInfo() const {
  return InstrInfo.get();
}

const LinxISARegisterInfo *LinxISASubtarget::getRegisterInfo() const {
  return &InstrInfo->getRegisterInfo();
}

const LinxISAFrameLowering *LinxISASubtarget::getFrameLowering() const {
  return FrameLowering.get();
}

const LinxISATargetLowering *LinxISASubtarget::getTargetLowering() const {
  return TLInfo.get();
}
