//===- LinxISACallingConv.td - Calling conventions for LinxISA -*- tablegen -*-===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Argument/Return Value Calling Conventions
//===----------------------------------------------------------------------===//

//===----------------------------------------------------------------------===//
// Linx 32-bit (linx32): pointers are i32.
//
// Bring-up ABI notes:
// - i64 is still passed in a single GPR (implementation choice).
// - Stack arguments are 8-byte aligned for simplicity.
//===----------------------------------------------------------------------===//

def CC_Linx32 : CallingConv<[
  // Promote small integers to i32.
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,

  // i32/i64/pointer arguments go in a0-a7.
  CCIfType<[i32, i64], CCAssignToReg<[R2, R3, R4, R5, R6, R7, R8, R9]>>,

  // Spill remaining args to the stack (8-byte aligned).
  CCAssignToStack<8, 8>
]>;

def RetCC_Linx32 : CallingConv<[
  // Promote small integers to i32.
  CCIfType<[i1, i8, i16], CCPromoteToType<i32>>,

  // Return values in a0 (and a1 for multi-reg returns in the future).
  CCIfType<[i32, i64], CCAssignToReg<[R2]>>
]>;

//===----------------------------------------------------------------------===//
// Linx 64-bit (linx64): pointers are i64.
//===----------------------------------------------------------------------===//

def CC_Linx64 : CallingConv<[
  // Promote small integers to i64.
  CCIfType<[i1, i8, i16, i32], CCPromoteToType<i64>>,

  // i64/pointer arguments go in a0-a7.
  CCIfType<[i64], CCAssignToReg<[R2, R3, R4, R5, R6, R7, R8, R9]>>,

  // Spill remaining args to the stack (8-byte aligned).
  CCAssignToStack<8, 8>
]>;

def RetCC_Linx64 : CallingConv<[
  // Promote small integers to i64.
  CCIfType<[i1, i8, i16, i32], CCPromoteToType<i64>>,

  // Return values in a0 (and a1 for multi-reg returns in the future).
  CCIfType<[i64], CCAssignToReg<[R2]>>
]>;
