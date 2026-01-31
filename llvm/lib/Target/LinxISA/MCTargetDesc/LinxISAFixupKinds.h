//===-- LinxISAFixupKinds.h - LinxISA Specific Fixup Entries ----*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_LINXISA_MCTARGETDESC_LINXISAFIXUPKINDS_H
#define LLVM_LIB_TARGET_LINXISA_MCTARGETDESC_LINXISAFIXUPKINDS_H

#include "llvm/MC/MCFixup.h"

namespace llvm {
namespace LinxISA {

enum Fixups {
  FIXUP_LINX_NONE = FirstTargetFixupKind,

  // PC-relative branch immediate (simm12, scaled by 2 bytes).
  FIXUP_LINX_B12_PCREL,

  // PC-relative jump immediate (simm22, scaled by 2 bytes).
  FIXUP_LINX_J22_PCREL,

  // PC-relative compressed block-start immediate (C.BSTART simm12, scaled by 2
  // bytes).
  FIXUP_LINX_CBSTART12_PCREL,

  // PC-relative block-start immediate (simm17, scaled by 2 bytes).
  FIXUP_LINX_B17_PCREL,

  // PC-relative long block-start immediate (HL.BSTART.* simm30, in bytes,
  // instruction-aligned).
  FIXUP_LINX_HL_BSTART30_PCREL,

  // PC-relative compressed setret immediate (C.SETRET uimm5, scaled by 2
  // bytes).
  FIXUP_LINX_CSETRET5_PCREL,

  // PC-relative setret immediate (imm20, scaled by 2 bytes).
  FIXUP_LINX_SETRET20_PCREL,

  // PC-relative long setret immediate (HL.SETRET imm32, scaled by 2 bytes).
  FIXUP_LINX_HL_SETRET32_PCREL,

  // Marker.
  LastTargetFixupKind,
  NumTargetFixupKinds = LastTargetFixupKind - FirstTargetFixupKind
};

} // namespace LinxISA
} // namespace llvm

#endif // LLVM_LIB_TARGET_LINXISA_MCTARGETDESC_LINXISAFIXUPKINDS_H
