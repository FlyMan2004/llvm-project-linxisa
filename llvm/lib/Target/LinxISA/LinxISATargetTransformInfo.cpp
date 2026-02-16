//===-- LinxISATargetTransformInfo.cpp - LinxISA specific TTI -------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LinxISATargetTransformInfo.h"
#include "llvm/Analysis/OptimizationRemarkEmitter.h"

using namespace llvm;

void LinxISATTIImpl::getUnrollingPreferences(
    Loop *L, ScalarEvolution &SE, TTI::UnrollingPreferences &UP,
    OptimizationRemarkEmitter *ORE) const {
  BaseT::getUnrollingPreferences(L, SE, UP, ORE);

  /*
   * Preserve canonical loops for the Linx SIMT lowering pass.
   *
   * TSVC kernels are loop-heavy and many candidates disappear before
   * LinxISASIMTAutoVectorize runs due target-independent unrolling. Keep loops
   * structurally intact and let the Linx backend decide block lowering.
   */
  UP.Partial = false;
  UP.Runtime = false;
  UP.AllowRemainder = false;
  UP.UpperBound = false;
  UP.UnrollRemainder = false;
  UP.Force = false;

  UP.Count = 1;
  UP.MaxCount = 1;
  UP.FullUnrollMaxCount = 1;
  UP.DefaultUnrollRuntimeCount = 1;

  UP.Threshold = 0;
  UP.PartialThreshold = 0;
  UP.OptSizeThreshold = 0;
  UP.PartialOptSizeThreshold = 0;
}
