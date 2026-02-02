//===--- LinxISA inline assembly block tests -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file tests inline assembly block structure for LinxISA target.
//
//===----------------------------------------------------------------------===//

// RUN: %clang_cc1 -triple linx64-unknown-elf -emit-llvm %s -o - | FileCheck %s

// Test basic block structure (BSTART/BSTOP)
void test_basic_block(void) {
  long a = 0, b = 0, c = 0;

  // Start a standard block
  // CHECK: call void asm sideeffect "BSTART.STD", ""
  __asm__ volatile ("BSTART.STD" ::: "memory");

  // Execute some operations inside the block
  // CHECK: call i64 asm sideeffect "add $1, $2, ->$0", "=r,r,r"
  __asm__ volatile ("add $1, $2, ->$0" : "=r"(c) : "r"(a), "r"(b));

  // End the block
  // CHECK: call void asm sideeffect "BSTOP", ""
  __asm__ volatile ("BSTOP" ::: "memory");
}

// Test block with condition setting
void test_conditional_block(long a, long b) {
  // Start block
  // CHECK: call void asm sideeffect "BSTART.STD", ""
  __asm__ volatile ("BSTART.STD" ::: "memory");

  // Set condition based on comparison
  // CHECK: call void asm sideeffect "setc.eq a0, a1", "r,r"
  __asm__ volatile ("setc.eq a0, a1" : : "r"(a), "r"(b));

  // Conditional block start (depends on condition set by setc)
  // CHECK: call void asm sideeffect "BSTART.STD COND, 0x0", ""
  __asm__ volatile ("BSTART.STD COND, 0x0" ::: "memory");

  // Operations in conditional block
  __asm__ volatile ("add a0, a1, ->a0" ::: "memory");

  // End conditional block
  // CHECK: call void asm sideeffect "BSTOP", ""
  __asm__ volatile ("BSTOP" ::: "memory");
}

// Test direct block jump
void test_direct_block(void (*target)(void)) {
  // Start block with direct jump
  // CHECK: call void asm sideeffect "BSTART.STD DIRECT, $0", "r"
  __asm__ volatile ("BSTART.STD DIRECT, $0" : : "r"(target));
}

// Test call block
void test_call_block(void (*func)(void)) {
  // Start call block
  // CHECK: call void asm sideeffect "BSTART.STD CALL, $0", "r"
  __asm__ volatile ("BSTART.STD CALL, $0" : : "r"(func));

  // SETRET to set return address
  // CHECK: call void asm sideeffect "setret 0x0, ->ra", ""
  __asm__ volatile ("setret 0x0, ->ra" ::: "memory");

  // End block
  // CHECK: call void asm sideeffect "BSTOP", ""
  __asm__ volatile ("BSTOP" ::: "memory");
}

// Test block private T registers
void test_t_register_usage(void) {
  long a = 0, b = 0, c = 0;

  // Use T register for temporary results
  // CHECK: call i64 asm sideeffect "add a0, a1, ->t", "=r,r"
  __asm__ volatile ("add a0, a1, ->t" : "=r"(a) : "r"(a));

  // Reference t#1 (first T register result)
  // CHECK: call i64 asm sideeffect "add t#1, a0, ->t", "=r,r"
  __asm__ volatile ("add t#1, a0, ->t" : "=r"(b) : "r"(a));

  // Use t#2
  // CHECK: call i64 asm sideeffect "add t#2, a0, ->t", "=r,r"
  __asm__ volatile ("add t#2, a0, ->t" : "=r"(c) : "r"(a));
}

// Test block private U registers
void test_u_register_usage(void) {
  long a = 0, b = 0;

  // Use U register for results
  // CHECK: call i64 asm sideeffect "add a0, a1, ->u", "=r,r"
  __asm__ volatile ("add a0, a1, ->u" : "=r"(a) : "r"(a));

  // Reference u#1
  // CHECK: call i64 asm sideeffect "add u#1, a0, ->t", "=r,r"
  __asm__ volatile ("add u#1, a0, ->t" : "=r"(b) : "r"(a));
}

// Test compressed block start
void test_compressed_block(void) {
  long a = 0, b = 0;

  // Compressed block start
  // CHECK: call void asm sideeffect "C.BSTART", ""
  __asm__ volatile ("C.BSTART" ::: "memory");

  // Compressed operations
  // CHECK: call i64 asm sideeffect "c.add a0, a1, ->t", "=r,r"
  __asm__ volatile ("c.add a0, a1, ->t" : "=r"(a) : "r"(b));

  // Compressed block stop
  // CHECK: call void asm sideeffect "C.BSTOP", ""
  __asm__ volatile ("C.BSTOP" ::: "memory");
}

// Test floating point block
void test_fp_block(void) {
  // Floating point block start
  // CHECK: call void asm sideeffect "BSTART.FP", ""
  __asm__ volatile ("BSTART.FP" ::: "memory");

  // FP operations would go here
  __asm__ volatile ("fadd a0, a1, a2" ::: "memory");

  // End FP block
  // CHECK: call void asm sideeffect "BSTOP", ""
  __asm__ volatile ("BSTOP" ::: "memory");
}

// Test system block
void test_sys_block(void) {
  // System block start
  // CHECK: call void asm sideeffect "BSTART.SYS", ""
  __asm__ volatile ("BSTART.SYS" ::: "memory");

  // System operations would go here
  __asm__ volatile ("syscall" ::: "memory");

  // End system block
  // CHECK: call void asm sideeffect "BSTOP", ""
  __asm__ volatile ("BSTOP" ::: "memory");
}

// Test parallel memory block
void test_mpar_block(long *src, long *dst, long n) {
  // Start parallel memory block
  // CHECK: call void asm sideeffect "BSTART.MPAR", ""
  __asm__ volatile ("BSTART.MPAR" ::: "memory");

  // Load
  // CHECK: call i64 asm sideeffect "lwi t#1, [a0], 0, ->t", "=r,r,r"
  __asm__ volatile ("lwi t#1, [a0], 0, ->t" : "=r"(n) : "r"(src));

  // Store
  // CHECK: call void asm sideeffect "swi t#1, [a1], 0", "r,r"
  __asm__ volatile ("swi t#1, [a1], 0" : : "r"(n), "r"(dst));

  // End parallel block
  // CHECK: call void asm sideeffect "BSTOP", ""
  __asm__ volatile ("BSTOP" ::: "memory");
}
