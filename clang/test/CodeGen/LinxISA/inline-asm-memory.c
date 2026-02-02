//===--- LinxISA inline assembly memory tests ----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file tests inline assembly memory operations for LinxISA target.
//
//===----------------------------------------------------------------------===//

// RUN: %clang_cc1 -triple linx64-unknown-elf -emit-llvm %s -o - | FileCheck %s

// Test load operations
long test_lwi(long *ptr) {
  long result;
  // CHECK: call i64 asm sideeffect "lwi $1, [$2], 0, ->$0", "=r,r"(i64* %[[PTR:[a-z0-9]+]])
  __asm__ volatile ("lwi $1, [$2], 0, ->$0" : "=r"(result) : "r"(ptr));
  return result;
}

long test_lwi_offset(long *ptr) {
  long result;
  // CHECK: call i64 asm sideeffect "lwi $1, [$2], 16, ->$0", "=r,r"
  __asm__ volatile ("lwi $1, [$2], 16, ->$0" : "=r"(result) : "r"(ptr));
  return result;
}

long test_ldi(long *ptr) {
  long result;
  // CHECK: call i64 asm sideeffect "ldi $1, [$2], 0, ->$0", "=r,r"
  __asm__ volatile ("ldi $1, [$2], 0, ->$0" : "=r"(result) : "r"(ptr));
  return result;
}

short test_lhi(long *ptr) {
  short result;
  // CHECK: call i16 asm sideeffect "lhi $1, [$2], 0, ->$0", "=r,r"
  __asm__ volatile ("lhi $1, [$2], 0, ->$0" : "=r"(result) : "r"(ptr));
  return result;
}

char test_lbi(long *ptr) {
  char result;
  // CHECK: call i8 asm sideeffect "lbi $1, [$2], 0, ->$0", "=r,r"
  __asm__ volatile ("lbi $1, [$2], 0, ->$0" : "=r"(result) : "r"(ptr));
  return result;
}

// Test store operations
void test_swi(long *ptr, long value) {
  // CHECK: call void asm sideeffect "swi $1, [$2], 0", "r,r"
  __asm__ volatile ("swi $1, [$2], 0" : : "r"(value), "r"(ptr));
}

void test_swi_offset(long *ptr, long value) {
  // CHECK: call void asm sideeffect "swi $1, [$2], 16", "r,r"
  __asm__ volatile ("swi $1, [$2], 16" : : "r"(value), "r"(ptr));
}

void test_sdi(long *ptr, long value) {
  // CHECK: call void asm sideeffect "sdi $1, [$2], 0", "r,r"
  __asm__ volatile ("sdi $1, [$2], 0" : : "r"(value), "r"(ptr));
}

void test_shi(long *ptr, short value) {
  // CHECK: call void asm sideeffect "shi $1, [$2], 0", "r,r"
  __asm__ volatile ("shi $1, [$2], 0" : : "r"(value), "r"(ptr));
}

void test_sbi(long *ptr, char value) {
  // CHECK: call void asm sideeffect "sbi $1, [$2], 0", "r,r"
  __asm__ volatile ("sbi $1, [$2], 0" : : "r"(value), "r"(ptr));
}

// Test indexed loads
long test_lw_indexed(long *base, long index) {
  long result;
  // CHECK: call i64 asm sideeffect "lw $1, [$2, $3<<2], ->$0", "=r,r,r"
  __asm__ volatile ("lw $1, [$2, $3<<2], ->$0" : "=r"(result) : "r"(base), "r"(index));
  return result;
}

long test_ld_indexed(long *base, long index) {
  long result;
  // CHECK: call i64 asm sideeffect "ld $1, [$2, $3<<3], ->$0", "=r,r,r"
  __asm__ volatile ("ld $1, [$2, $3<<3], ->$0" : "=r"(result) : "r"(base), "r"(index));
  return result;
}

// Test indexed stores
void test_sw_indexed(long *base, long index, long value) {
  // CHECK: call void asm sideeffect "sw $1, [$2, $3<<2]", "r,r,r"
  __asm__ volatile ("sw $1, [$2, $3<<2]" : : "r"(value), "r"(base), "r"(index));
}

void test_sd_indexed(long *base, long index, long value) {
  // CHECK: call void asm sideeffect "sd $1, [$2, $3<<3]", "r,r,r"
  __asm__ volatile ("sd $1, [$2, $3<<3]" : : "r"(value), "r"(base), "r"(index));
}

// Test LUI (load upper immediate)
long test_lui(void) {
  long result;
  // CHECK: call i64 asm sideeffect "lui $1, 12345, ->$0", "=r,J"
  __asm__ volatile ("lui $1, 12345, ->$0" : "=r"(result));
  return result;
}

// Test PC-relative load
long test_ldpcr(void) {
  long result;
  // CHECK: call i64 asm sideeffect "ld.pcr $1, symbol, ->$0", "=r"
  __asm__ volatile ("ld.pcr $1, symbol, ->$0" : "=r"(result));
  return result;
}

// Test load immediate
long test_ldi_imm(void) {
  long result;
  // CHECK: call i64 asm sideeffect "ldi $1, 42, ->$0", "=r,I"
  __asm__ volatile ("ldi $1, 42, ->$0" : "=r"(result));
  return result;
}
