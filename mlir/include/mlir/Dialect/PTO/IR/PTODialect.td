//===- PTODialect.td - PTO dialect definition --------------*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// Defines the MLIR PTO dialect.
//
//===----------------------------------------------------------------------===//

#ifndef MLIR_DIALECT_PTO_IR_PTODIALECT
#define MLIR_DIALECT_PTO_IR_PTODIALECT

include "mlir/IR/OpBase.td"

//===----------------------------------------------------------------------===//
// PTO Dialect
//===----------------------------------------------------------------------===//

def PTO_Dialect : Dialect {
  let summary = "...";
  let description = [{
    ...
  }];

  let name = "pto";
  let cppNamespace = "::mlir::pto";

  let useDefaultTypePrinterParser = 1;
  let useDefaultAttributePrinterParser = 1;
}

#endif // MLIR_DIALECT_PTO_IR_PTODIALECT
