//===- LinxISA.cpp --------------------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "Symbols.h"
#include "Target.h"
#include "llvm/BinaryFormat/ELF.h"
#include "llvm/Support/Endian.h"

using namespace llvm;
using namespace llvm::object;
using namespace llvm::support::endian;
using namespace llvm::ELF;
using namespace lld;
using namespace lld::elf;

namespace {

static uint32_t encodeB12Pcrel(Ctx &ctx, uint8_t *loc, int64_t value,
                              const Relocation &rel) {
  if (value & 0x1)
    Err(ctx) << getErrorLoc(ctx, loc) << "unaligned branch target";

  int64_t imm = value >> 1;
  checkInt(ctx, loc, imm, 12, rel);
  uint32_t uimm = static_cast<uint32_t>(imm) & 0x0FFFu;
  return ((uimm & 0x07Fu) << 25) | (((uimm >> 7) & 0x01Fu) << 7);
}

static uint32_t encodeJ22Pcrel(Ctx &ctx, uint8_t *loc, int64_t value,
                              const Relocation &rel) {
  if (value & 0x1)
    Err(ctx) << getErrorLoc(ctx, loc) << "unaligned jump target";

  int64_t imm = value >> 1;
  checkInt(ctx, loc, imm, 22, rel);
  uint32_t uimm = static_cast<uint32_t>(imm) & 0x003FFFFFu;
  return ((uimm & 0x1FFFFu) << 15) | (((uimm >> 17) & 0x01Fu) << 7);
}

static uint16_t encodeCBStart12Pcrel(Ctx &ctx, uint8_t *loc, int64_t value,
                                     const Relocation &rel) {
  if (value & 0x1)
    Err(ctx) << getErrorLoc(ctx, loc) << "unaligned C.BSTART target";

  int64_t imm = value >> 1;
  checkInt(ctx, loc, imm, 12, rel);
  uint16_t uimm = static_cast<uint16_t>(imm) & 0x0FFFu;
  return uimm << 4;
}

static uint32_t encodeB17Pcrel(Ctx &ctx, uint8_t *loc, int64_t value,
                              const Relocation &rel) {
  if (value & 0x1)
    Err(ctx) << getErrorLoc(ctx, loc) << "unaligned BSTART target";

  int64_t imm = value >> 1;
  checkInt(ctx, loc, imm, 17, rel);
  uint32_t uimm = static_cast<uint32_t>(imm) & 0x1FFFFu;
  return uimm << 15;
}

static uint64_t encodeHLBStart30Pcrel(Ctx &ctx, uint8_t *loc, int64_t value,
                                      const Relocation &rel) {
  if (value & 0x1)
    Err(ctx) << getErrorLoc(ctx, loc) << "unaligned HL.BSTART target";

  checkInt(ctx, loc, value, 30, rel);
  uint64_t uimm = static_cast<uint64_t>(value) & 0x3FFFFFFFull;
  uint64_t patch = 0;
  patch |= ((uimm >> 1) & 0x1FFFFull) << 31;   // simm[17:1] -> insn[47:31]
  patch |= ((uimm >> 18) & 0x0FFFull) << 4;    // simm[29:18] -> insn[15:4]
  return patch;
}

static uint16_t encodeCSetRet5Pcrel(Ctx &ctx, uint8_t *loc, int64_t value,
                                   const Relocation &rel) {
  if (value & 0x1)
    Err(ctx) << getErrorLoc(ctx, loc) << "unaligned C.SETRET target";

  uint64_t imm = static_cast<uint64_t>(value) >> 1;
  checkUInt(ctx, loc, imm, 5, rel);
  return (static_cast<uint16_t>(imm) & 0x001Fu) << 6;
}

static uint32_t encodeSetRet20Pcrel(Ctx &ctx, uint8_t *loc, int64_t value,
                                   const Relocation &rel) {
  if (value & 0x1)
    Err(ctx) << getErrorLoc(ctx, loc) << "unaligned SETRET target";

  uint64_t imm = static_cast<uint64_t>(value) >> 1;
  checkUInt(ctx, loc, imm, 20, rel);
  return (static_cast<uint32_t>(imm) & 0x000FFFFFu) << 12;
}

static uint64_t encodeHLSetRet32Pcrel(Ctx &ctx, uint8_t *loc, int64_t value,
                                      const Relocation &rel) {
  if (value & 0x1)
    Err(ctx) << getErrorLoc(ctx, loc) << "unaligned HL.SETRET target";

  int64_t imm = value >> 1;
  checkInt(ctx, loc, imm, 32, rel);
  uint64_t uimm = static_cast<uint64_t>(imm) & 0xFFFF'FFFFull;
  uint64_t patch = 0;
  patch |= (uimm & 0xFFFFFull) << 28;          // imm[19:0] -> insn[47:28]
  patch |= ((uimm >> 20) & 0x0FFFull) << 4;    // imm[31:20] -> insn[15:4]
  return patch;
}

class LinxISA final : public TargetInfo {
public:
  LinxISA(Ctx &ctx);
  RelType getDynRel(RelType type) const override { return type; }
  RelExpr getRelExpr(RelType type, const Symbol &s,
                     const uint8_t *loc) const override;
  void relocate(uint8_t *loc, const Relocation &rel,
                uint64_t val) const override;
};

} // namespace

LinxISA::LinxISA(Ctx &ctx) : TargetInfo(ctx) {
  copyRel = R_LINX_COPY;
  pltRel = R_LINX_JUMP_SLOT;
  relativeRel = R_LINX_RELATIVE;
  iRelativeRel = R_LINX_RELATIVE;
  symbolicRel = ctx.arg.is64 ? R_LINX_64 : R_LINX_32;
  gotRel = symbolicRel;
}

RelExpr LinxISA::getRelExpr(RelType type, const Symbol &s,
                            const uint8_t *loc) const {
  switch (type) {
  case R_LINX_B12_PCREL:
  case R_LINX_J22_PCREL:
  case R_LINX_CBSTART12_PCREL:
  case R_LINX_B17_PCREL:
  case R_LINX_HL_BSTART30_PCREL:
  case R_LINX_CSETRET5_PCREL:
  case R_LINX_SETRET20_PCREL:
  case R_LINX_HL_SETRET32_PCREL:
    return R_PC;
  default:
    return R_ABS;
  }
}

void LinxISA::relocate(uint8_t *loc, const Relocation &rel,
                       uint64_t val) const {
  int64_t sval = static_cast<int64_t>(val);
  switch (rel.type) {
  case R_LINX_NONE:
    return;
  case R_LINX_32:
    checkIntUInt(ctx, loc, val, 32, rel);
    write32le(loc, val);
    return;
  case R_LINX_64:
    write64le(loc, val);
    return;

  case R_LINX_B12_PCREL: {
    uint32_t cur = read32le(loc);
    cur |= encodeB12Pcrel(ctx, loc, sval, rel);
    write32le(loc, cur);
    return;
  }
  case R_LINX_J22_PCREL: {
    uint32_t cur = read32le(loc);
    cur |= encodeJ22Pcrel(ctx, loc, sval, rel);
    write32le(loc, cur);
    return;
  }
  case R_LINX_CBSTART12_PCREL: {
    uint16_t cur = read16le(loc);
    cur |= encodeCBStart12Pcrel(ctx, loc, sval, rel);
    write16le(loc, cur);
    return;
  }
  case R_LINX_B17_PCREL: {
    uint32_t cur = read32le(loc);
    cur |= encodeB17Pcrel(ctx, loc, sval, rel);
    write32le(loc, cur);
    return;
  }
  case R_LINX_HL_BSTART30_PCREL: {
    uint64_t cur = 0;
    for (unsigned i = 0; i < 6; ++i)
      cur |= static_cast<uint64_t>(loc[i]) << (i * 8);
    cur |= encodeHLBStart30Pcrel(ctx, loc, sval, rel);
    for (unsigned i = 0; i < 6; ++i)
      loc[i] = static_cast<uint8_t>((cur >> (i * 8)) & 0xFF);
    return;
  }
  case R_LINX_CSETRET5_PCREL: {
    uint16_t cur = read16le(loc);
    cur |= encodeCSetRet5Pcrel(ctx, loc, sval, rel);
    write16le(loc, cur);
    return;
  }
  case R_LINX_SETRET20_PCREL: {
    uint32_t cur = read32le(loc);
    cur |= encodeSetRet20Pcrel(ctx, loc, sval, rel);
    write32le(loc, cur);
    return;
  }
  case R_LINX_HL_SETRET32_PCREL: {
    uint64_t cur = 0;
    for (unsigned i = 0; i < 6; ++i)
      cur |= static_cast<uint64_t>(loc[i]) << (i * 8);
    cur |= encodeHLSetRet32Pcrel(ctx, loc, sval, rel);
    for (unsigned i = 0; i < 6; ++i)
      loc[i] = static_cast<uint8_t>((cur >> (i * 8)) & 0xFF);
    return;
  }

  default:
    Err(ctx) << getErrorLoc(ctx, loc) << "unrecognized relocation " << rel.type;
    return;
  }
}

void elf::setLinxTargetInfo(Ctx &ctx) { ctx.target.reset(new LinxISA(ctx)); }
