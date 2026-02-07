//===-- LinxISABaseInfo.h - LinxISA MC level definitions --------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LINXISA_LINXISABASEINFO_H
#define LLVM_LIB_TARGET_LINXISA_LINXISABASEINFO_H

namespace llvm {

namespace LinxII {

// Target flags for GlobalAddress/ExternalSymbol operands.
enum MOFlags : unsigned {
  MO_NO_FLAG = 0,

  // Use a PLT relocation when referencing a function symbol (e.g. for PIC
  // calls into shared libraries).
  MO_PLT = 1u << 0,

  // Apply a SrcR conversion modifier for instructions that support it.
  // Today this is used for SETC.* and CMP.* forms where only the right operand
  // can be tagged with a width/sign conversion (e.g. `.sw` / `.uw`).
  MO_SRCR_UW = 1u << 1,
  MO_SRCR_SW = 1u << 2,
};

} // namespace LinxII

} // namespace llvm

#endif // LLVM_LIB_TARGET_LINXISA_LINXISABASEINFO_H
