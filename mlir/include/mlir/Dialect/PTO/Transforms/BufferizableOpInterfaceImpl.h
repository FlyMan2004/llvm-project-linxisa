//===- BufferizableOpInterfaceImpl.h - Impl. of BufferizableOpInterface ---===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#ifndef PTO_BUFFERIZABLEOPINTERFACEIMPL_H
#define PTO_BUFFERIZABLEOPINTERFACEIMPL_H

namespace mlir {
class DialectRegistry;

namespace pto {
void registerBufferizableOpInterfaceExternalModels(DialectRegistry &registry);
} // namespace pto
} // namespace mlir

#endif // PTO_BUFFERIZABLEOPINTERFACEIMPL_H
