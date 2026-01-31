//===- LinxISAInstrFormats.td - LinxISA instruction formats -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

class LinxISAInst<dag outs, dag ins, string asmstr, list<dag> pattern,
                  int size = 4>
    : Instruction {
  let Size = size;

  let Namespace = "Linx";
  let DecoderNamespace = "Linx";

  dag OutOperandList = outs;
  dag InOperandList = ins;
  let AsmString = asmstr;
  let Pattern = pattern;
}

// Pseudo instructions (lowered/expanded in various backend passes).
class Pseudo<dag outs, dag ins, string asmstr, list<dag> pattern>
    : LinxISAInst<outs, ins, asmstr, pattern> {
  let isCodeGenOnly = 1;
  let isPseudo = 1;
}
