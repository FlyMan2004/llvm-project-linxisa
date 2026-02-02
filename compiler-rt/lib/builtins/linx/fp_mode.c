//===-- linx/fp_mode.c - Floating-point mode utilities --------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements floating-point mode utilities for the Linx ISA.
// For targets without FPU, software floating point is used via libgcc.
//
//===----------------------------------------------------------------------===//

#include "../fp_mode.h"

// For Linx ISA, we use software floating point, so the rounding mode
// is always round-to-nearest (the default for soft-fp)

CRT_FE_ROUND_MODE __fe_getround(void) {
  // Software FP always uses round-to-nearest
  return CRT_FE_TONEAREST;
}

int __fe_raise_inexact(void) {
  // Software FP doesn't raise floating-point exceptions
  return 0;
}
