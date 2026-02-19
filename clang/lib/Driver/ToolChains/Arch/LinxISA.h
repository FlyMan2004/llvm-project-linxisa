//===--- LinxISA.h - LinxISA-specific Tool Helpers -------------*- C++ -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_ARCH_LINXISA_H
#define LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_ARCH_LINXISA_H

#include "clang/Driver/Driver.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/Option/ArgList.h"
#include <string>
#include <vector>

namespace clang {
namespace driver {
namespace tools {
namespace linxisa {

std::string getLinxISATargetCPU(const Driver &D, const llvm::Triple &Triple,
                                const llvm::opt::ArgList &Args);

void getLinxISATargetFeatures(const Driver &D, const llvm::Triple &Triple,
                              const llvm::opt::ArgList &Args,
                              std::vector<llvm::StringRef> &Features);

} // namespace linxisa
} // namespace tools
} // namespace driver
} // namespace clang

#endif // LLVM_CLANG_LIB_DRIVER_TOOLCHAINS_ARCH_LINXISA_H
