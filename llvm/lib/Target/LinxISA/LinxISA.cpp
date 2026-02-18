//===-- LinxISA.cpp - Linx target-wide options and hooks -----------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "LinxISA.h"
#include "llvm/Support/CommandLine.h"

using namespace llvm;

static cl::opt<bool>
    LinxEnableNegImmCanon("linx-enable-neg-imm-canon", cl::Hidden,
                          cl::desc("Enable Linx negative-immediate "
                                   "canonicalization in DAG isel"),
                          cl::init(false));

static cl::opt<bool>
    LinxEnableMaskSetcFold("linx-enable-mask-setc-fold", cl::Hidden,
                           cl::desc("Enable Linx mask compare/setc folding"),
                           cl::init(false));

static cl::opt<bool> LinxEnableSetcSrcRTypeFlags(
    "linx-enable-setc-srcr-flags", cl::Hidden,
    cl::desc("Enable Linx setc/cmp srcR type modifiers (.sw/.uw) in blockify"),
    cl::init(false));

static cl::opt<bool> LinxEnableCShift16(
    "linx-enable-cshift16", cl::Hidden,
    cl::desc("Enable Linx compressed C.SLLI/C.SRLI emission"), cl::init(false));

static cl::opt<bool> LinxEnableT1Motion(
    "linx-enable-t1-motion", cl::Hidden,
    cl::desc("Enable Linx local motion/remap heuristics for t#1 compression"),
    cl::init(false));

static cl::opt<LinxCodeSizeBalanceMode> LinxCodeSizeMode(
    "linx-codesize-balance-mode", cl::Hidden,
    cl::desc("Linx code-size balancing mode"),
    cl::values(clEnumValN(LinxCodeSizeBalanceMode::Off, "off",
                          "Disable Linx code-size balance tuning"),
               clEnumValN(LinxCodeSizeBalanceMode::Balanced, "balanced",
                          "Balanced static/dynamic tuning"),
               clEnumValN(LinxCodeSizeBalanceMode::StaticFirst, "static-first",
                          "Prefer static code size"),
               clEnumValN(LinxCodeSizeBalanceMode::DynamicFirst, "dynamic-first",
                          "Prefer dynamic instruction count")),
    cl::init(LinxCodeSizeBalanceMode::Off));

bool llvm::linxEnableNegImmCanon() { return LinxEnableNegImmCanon; }
bool llvm::linxEnableMaskSetcFold() { return LinxEnableMaskSetcFold; }
bool llvm::linxEnableSetcSrcRTypeFlags() { return LinxEnableSetcSrcRTypeFlags; }
bool llvm::linxEnableCShift16() { return LinxEnableCShift16; }
bool llvm::linxEnableT1Motion() { return LinxEnableT1Motion; }
LinxCodeSizeBalanceMode llvm::linxCodeSizeBalanceMode() {
  return LinxCodeSizeMode;
}
