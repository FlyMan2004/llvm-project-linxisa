//===--- LinxISA inline assembly arithmetic tests ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file tests inline assembly arithmetic operations for LinxISA target.
//
//===----------------------------------------------------------------------===//

// RUN: %clang_cc1 -triple linx64-unknown-elf -emit-llvm %s -o - | FileCheck %s

// Test basic arithmetic operations
long test_arithmetic_add(long a, long b) {
  long result;
  // CHECK: call i64 asm sideeffect "add $1, $2, ->$0", "=r,r,r"(i64 %[[A:[a-z0-9]+]], i64 %[[B:[a-z0-9]+]])
  __asm__ volatile ("add $1, $2, ->$0" : "=r"(result) : "r"(a), "r"(b));
  return result;
}

long test_arithmetic_sub(long a, long b) {
  long result;
  // CHECK: call i64 asm sideeffect "sub $1, $2, ->$0", "=r,r,r"
  __asm__ volatile ("sub $1, $2, ->$0" : "=r"(result) : "r"(a), "r"(b));
  return result;
}

long test_arithmetic_and(long a, long b) {
  long result;
  // CHECK: call i64 asm sideeffect "and $1, $2, ->$0", "=r,r,r"
  __asm__ volatile ("and $1, $2, ->$0" : "=r"(result) : "r"(a), "r"(b));
  return result;
}

long test_arithmetic_or(long a, long b) {
  long result;
  // CHECK: call i64 asm sideeffect "or $1, $2, ->$0", "=r,r,r"
  __asm__ volatile ("or $1, $2, ->$0" : "=r"(result) : "r"(a), "r"(b));
  return result;
}

long test_arithmetic_xor(long a, long b) {
  long result;
  // CHECK: call i64 asm sideeffect "xor $1, $2, ->$0", "=r,r,r"
  __asm__ volatile ("xor $1, $2, ->$0" : "=r"(result) : "r"(a), "r"(b));
  return result;
}

// Test shift operations
long test_shift_left(long a, unsigned long b) {
  long result;
  // CHECK: call i64 asm sideeffect "sll $1, $2, ->$0", "=r,r,r"
  __asm__ volatile ("sll $1, $2, ->$0" : "=r"(result) : "r"(a), "r"(b));
  return result;
}

long test_shift_right_logical(long a, unsigned long b) {
  long result;
  // CHECK: call i64 asm sideeffect "srl $1, $2, ->$0", "=r,r,r"
  __asm__ volatile ("srl $1, $2, ->$0" : "=r"(result) : "r"(a), "r"(b));
  return result;
}

long test_shift_right_arithmetic(long a, unsigned long b) {
  long result;
  // CHECK: call i64 asm sideeffect "sra $1, $2, ->$0", "=r,r,r"
  __asm__ volatile ("sra $1, $2, ->$0" : "=r"(result) : "r"(a), "r"(b));
  return result;
}

// Test immediate arithmetic operations
long test_addi(long a) {
  long result;
  // CHECK: call i64 asm sideeffect "addi $1, 10, ->$0", "=r,I"
  __asm__ volatile ("addi $1, 10, ->$0" : "=r"(result) : "I"(a));
  return result;
}

long test_subi(long a) {
  long result;
  // CHECK: call i64 asm sideeffect "subi $1, 5, ->$0", "=r,I"
  __asm__ volatile ("subi $1, 5, ->$0" : "=r"(result) : "I"(a));
  return result;
}

// Test shift with immediate
long test_slli_imm(long a) {
  long result;
  // CHECK: call i64 asm sideeffect "slli $1, 3, ->$0", "=r,K"
  __asm__ volatile ("slli $1, 3, ->$0" : "=r"(result) : "K"(a));
  return result;
}

long test_srli_imm(long a) {
  long result;
  // CHECK: call i64 asm sideeffect "srli $1, 2, ->$0", "=r,K"
  __asm__ volatile ("srli $1, 2, ->$0" : "=r"(result) : "K"(a));
  return result;
}

// Test multiplication and division
long test_mul(long a, long b) {
  long result;
  // CHECK: call i64 asm sideeffect "mul $1, $2, ->$0", "=r,r,r"
  __asm__ volatile ("mul $1, $2, ->$0" : "=r"(result) : "r"(a), "r"(b));
  return result;
}

long test_div(long a, long b) {
  long result;
  // CHECK: call i64 asm sideeffect "div $1, $2, ->$0", "=r,r,r"
  __asm__ volatile ("div $1, $2, ->$0" : "=r"(result) : "r"(a), "r"(b));
  return result;
}

long test_divu(long a, long b) {
  long result;
  // CHECK: call i64 asm sideeffect "divu $1, $2, ->$0", "=r,r,r"
  __asm__ volatile ("divu $1, $2, ->$0" : "=r"(result) : "r"(a), "r"(b));
  return result;
}

long test_rem(long a, long b) {
  long result;
  // CHECK: call i64 asm sideeffect "rem $1, $2, ->$0", "=r,r,r"
  __asm__ volatile ("rem $1, $2, ->$0" : "=r"(result) : "r"(a), "r"(b));
  return result;
}

// Test comparison and conditional select
long test_cmpeq(long a, long b) {
  long result;
  // CHECK: call i64 asm sideeffect "cmpeq $1, $2, ->$0", "=r,r,r"
  __asm__ volatile ("cmpeq $1, $2, ->$0" : "=r"(result) : "r"(a), "r"(b));
  return result;
}

long test_cmpne(long a, long b) {
  long result;
  // CHECK: call i64 asm sideeffect "cmpne $1, $2, ->$0", "=r,r,r"
  __asm__ volatile ("cmpne $1, $2, ->$0" : "=r"(result) : "r"(a), "r"(b));
  return result;
}

long test_csel(long pred, long true_val, long false_val) {
  long result;
  // CHECK: call i64 asm sideeffect "csel $1, $2, $3, ->$0", "=r,r,r,r"
  __asm__ volatile ("csel $1, $2, $3, ->$0" : "=r"(result) : "r"(pred), "r"(true_val), "r"(false_val));
  return result;
}
