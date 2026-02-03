//===-- LinxISAMachineFunctionInfo.h - LinxISA machine function info ------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file declares LinxISA-specific per-machine-function information.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LINXISA_LINXISAMACHINEFUNCTIONINFO_H
#define LLVM_LIB_TARGET_LINXISA_LINXISAMACHINEFUNCTIONINFO_H

#include "LinxISASubtarget.h"
#include "llvm/CodeGen/MachineFunction.h"

namespace llvm {

class LinxISAMachineFunctionInfo : public MachineFunctionInfo {
  int VarArgsFrameIndex = 0;
  int VarArgsSaveSize = 0;

public:
  LinxISAMachineFunctionInfo(const Function &F, const LinxISASubtarget *STI) {
    (void)F;
    (void)STI;
  }

  MachineFunctionInfo *
  clone(BumpPtrAllocator &Allocator, MachineFunction &DestMF,
        const DenseMap<MachineBasicBlock *, MachineBasicBlock *> &Src2DstMBB)
      const override {
    (void)DestMF;
    (void)Src2DstMBB;
    return new (Allocator.Allocate<LinxISAMachineFunctionInfo>())
        LinxISAMachineFunctionInfo(*this);
  }

  int getVarArgsFrameIndex() const { return VarArgsFrameIndex; }
  void setVarArgsFrameIndex(int Index) { VarArgsFrameIndex = Index; }

  int getVarArgsSaveSize() const { return VarArgsSaveSize; }
  void setVarArgsSaveSize(int Size) { VarArgsSaveSize = Size; }
};

} // namespace llvm

#endif // LLVM_LIB_TARGET_LINXISA_LINXISAMACHINEFUNCTIONINFO_H

