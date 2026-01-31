//===--- LinxISA.cpp - Implement LinxISA target feature support -----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file implements LinxISA TargetInfo objects.
//
//===----------------------------------------------------------------------===//

#include "LinxISA.h"
#include "clang/Basic/MacroBuilder.h"

using namespace clang;
using namespace clang::targets;

ArrayRef<const char *> LinxISATargetInfo::getGCCRegNames() const {
  static const char *const GCCRegNames[] = {
      "zero", "sp",  "a0",  "a1",  "a2",  "a3",  "a4",  "a5",
      "a6",   "a7",  "ra",  "s0",  "s1",  "s2",  "s3",  "s4",
      "s5",   "s6",  "s7",  "s8",  "x0",  "x1",  "x2",  "x3",
      "t#1",  "t#2", "t#3", "t#4", "u#1", "u#2", "u",   "t",
  };
  return llvm::ArrayRef(GCCRegNames);
}

ArrayRef<TargetInfo::GCCRegAlias> LinxISATargetInfo::getGCCRegAliases() const {
  static const TargetInfo::GCCRegAlias GCCRegAliases[] = {
      {{"r0"}, "zero"}, {{"r1"}, "sp"},  {{"r2"}, "a0"},  {{"r3"}, "a1"},
      {{"r4"}, "a2"},   {{"r5"}, "a3"},  {{"r6"}, "a4"},  {{"r7"}, "a5"},
      {{"r8"}, "a6"},   {{"r9"}, "a7"},  {{"r10"}, "ra"}, {{"r11"}, "s0"},
      {{"r12"}, "s1"},  {{"r13"}, "s2"}, {{"r14"}, "s3"}, {{"r15"}, "s4"},
      {{"r16"}, "s5"},  {{"r17"}, "s6"}, {{"r18"}, "s7"}, {{"r19"}, "s8"},
      {{"r20"}, "x0"},  {{"r21"}, "x1"}, {{"r22"}, "x2"}, {{"r23"}, "x3"},
  };
  return llvm::ArrayRef(GCCRegAliases);
}

void LinxISATargetInfo::getTargetDefines(const LangOptions &Opts,
                                        MacroBuilder &Builder) const {
  Builder.defineMacro("__LINX__");
  Builder.defineMacro("__linx__");
  Builder.defineMacro("__LINXISA__");
  Builder.defineMacro("__linxisa__");
  if (getTriple().isArch64Bit()) {
    Builder.defineMacro("__LINX64__");
    Builder.defineMacro("__linx64__");
  } else {
    Builder.defineMacro("__LINX32__");
    Builder.defineMacro("__linx32__");
  }

  Builder.defineMacro("__ELF__");
}
