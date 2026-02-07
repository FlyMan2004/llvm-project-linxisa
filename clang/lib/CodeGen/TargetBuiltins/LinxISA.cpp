//===--- LinxISA.cpp - Emit LLVM Code for LinxISA builtins ----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "CGBuiltin.h"
#include "clang/Basic/TargetBuiltins.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/IntrinsicsLinx.h"

using namespace clang;
using namespace clang::CodeGen;

llvm::Value *CodeGenFunction::EmitLinxISABuiltinExpr(unsigned BuiltinID,
                                                     const CallExpr *E,
                                                     ReturnValueSlot) {
  switch (BuiltinID) {
  case LinxISA::BI__builtin_linx_tma_tload: {
    llvm::Value *Base = EmitScalarExpr(E->getArg(0));
    llvm::Value *Size = EmitScalarExpr(E->getArg(1));
    Size = Builder.CreateIntCast(Size, Builder.getInt32Ty(), /*isSigned=*/false);

    llvm::Type *TileTy = ConvertType(E->getType());
    llvm::Function *F =
        CGM.getIntrinsic(llvm::Intrinsic::linx_tma_tload, {TileTy});
    return Builder.CreateCall(F, {Base, Size}, "linx.tload");
  }
  case LinxISA::BI__builtin_linx_tma_tstore: {
    llvm::Value *Base = EmitScalarExpr(E->getArg(0));
    llvm::Value *Tile = EmitScalarExpr(E->getArg(1));
    llvm::Value *Size = EmitScalarExpr(E->getArg(2));
    Size = Builder.CreateIntCast(Size, Builder.getInt32Ty(), /*isSigned=*/false);

    llvm::Function *F =
        CGM.getIntrinsic(llvm::Intrinsic::linx_tma_tstore, {Tile->getType()});
    return Builder.CreateCall(F, {Base, Tile, Size});
  }
  case LinxISA::BI__builtin_linx_cube_mamulb: {
    llvm::Value *A = EmitScalarExpr(E->getArg(0));
    llvm::Value *B = EmitScalarExpr(E->getArg(1));
    llvm::Value *M = EmitScalarExpr(E->getArg(2));
    llvm::Value *N = EmitScalarExpr(E->getArg(3));
    llvm::Value *K = EmitScalarExpr(E->getArg(4));

    llvm::Type *I32 = Builder.getInt32Ty();
    M = Builder.CreateIntCast(M, I32, /*isSigned=*/false);
    N = Builder.CreateIntCast(N, I32, /*isSigned=*/false);
    K = Builder.CreateIntCast(K, I32, /*isSigned=*/false);

    llvm::Function *F =
        CGM.getIntrinsic(llvm::Intrinsic::linx_cube_mamulb, {A->getType()});
    return Builder.CreateCall(F, {A, B, M, N, K}, "linx.mamulb");
  }
  default:
    break;
  }

  return nullptr;
}
