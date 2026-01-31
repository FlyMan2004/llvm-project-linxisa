//===-- LinxISA.h - Top-level interface for LinxISA ----------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LINXISA_LINXISA_H
#define LLVM_LIB_TARGET_LINXISA_LINXISA_H

#include "llvm/CodeGen/SelectionDAGNodes.h"
#include "llvm/Pass.h"

namespace llvm {

class LinxISATargetMachine;
class PassRegistry;

FunctionPass *createLinxISAISelDag(LinxISATargetMachine &TM);
FunctionPass *createLinxISABlockifyPass();

void initializeLinxISAAsmPrinterPass(PassRegistry &);
void initializeLinxISADAGToDAGISelLegacyPass(PassRegistry &);
void initializeLinxISABlockifyPass(PassRegistry &);

namespace LinxISD {

enum NodeType : unsigned {
  FIRST_NUMBER = ISD::BUILTIN_OP_END,

  CALL,
  RET_GLUE,

  BR_CC,
  SETCC,
};

} // namespace LinxISD

} // namespace llvm

#endif // LLVM_LIB_TARGET_LINXISA_LINXISA_H
