//===-- LinxISAELFObjectWriter.cpp - LinxISA ELF Writer -------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "MCTargetDesc/LinxISAFixupKinds.h"
#include "MCTargetDesc/LinxISAMCTargetDesc.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/MC/MCELFObjectWriter.h"
#include "llvm/MC/MCFixup.h"
#include "llvm/MC/MCObjectWriter.h"
#include "llvm/MC/MCValue.h"

using namespace llvm;

namespace {

class LinxISAELFObjectWriter : public MCELFObjectTargetWriter {
public:
  LinxISAELFObjectWriter(uint8_t OSABI, bool Is64Bit)
      : MCELFObjectTargetWriter(Is64Bit, OSABI, ELF::EM_NONE,
                                /*HasRelocationAddend=*/true) {}

  ~LinxISAELFObjectWriter() override = default;

  unsigned getRelocType(const MCFixup &, const MCValue &,
                        bool IsPCRel) const override {
    // For now, only support fully-resolved fixups (no dynamic relocations).
    // Any unresolved expression will result in an ELF relocation of type NONE,
    // which is acceptable for early bring-up but not a stable ABI.
    return 0;
  }
};

} // namespace

std::unique_ptr<MCObjectTargetWriter>
llvm::createLinxISAELFObjectWriter(uint8_t OSABI, bool Is64Bit) {
  return std::make_unique<LinxISAELFObjectWriter>(OSABI, Is64Bit);
}
