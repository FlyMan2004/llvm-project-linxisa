//===--- LinxISA inline assembly basic tests -----------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file tests inline assembly support for LinxISA target.
//
//===----------------------------------------------------------------------===//

// RUN: %clang_cc1 -triple linx64-unknown-elf -emit-llvm %s -o - | FileCheck %s

// Test basic register constraints
void test_basic_register_constraints() {
  long a = 0, b = 0, c = 0;

  // Test 'r' constraint - any general purpose register
  // CHECK: call i64 asm sideeffect "add $1, $2, ->$0", "=r,r,r"(i64 %[[A:[a-z0-9]+]], i64 %[[B:[a-z0-9]+]])
  __asm__ volatile ("add $1, $2, ->$0" : "=r"(c) : "r"(a), "r"(b));

  // Test explicit register naming
  // CHECK: call i64 asm sideeffect "add a0, a1, ->a0", "=r,r,r"
  __asm__ volatile ("add a0, a1, ->a0" : "=r"(c) : "r"(a), "r"(b));
}

// Test immediate value constraints
void test_immediate_constraints() {
  long a = 0;

  // Test 12-bit signed immediate constraint 'I'
  // CHECK: call i64 asm sideeffect "addi a0, $1, ->a0", "=r,I"(i64 100)
  __asm__ volatile ("addi a0, $1, ->a0" : "=r"(a) : "I"(100));

  // Test 20-bit immediate constraint 'J' (for LUI)
  // CHECK: call i64 asm sideeffect "lui a0, $1", "=r,J"(i64 12345)
  __asm__ volatile ("lui a0, $1" : "=r"(a) : "J"(12345));

  // Test 5-bit unsigned immediate constraint 'K'
  // CHECK: call i64 asm sideeffect "slli a0, a0, $1", "=r,r,K"(i64 5)
  __asm__ volatile ("slli a0, a0, $1" : "=r"(a) : "r"(a), "K"(5));

  // Test 32-bit immediate constraint 'n'
  // CHECK: call i64 asm sideeffect "addi a0, $1, ->a0", "=r,n"(i64 -12345)
  __asm__ volatile ("addi a0, $1, ->a0" : "=r"(a) : "n"(-12345));
}

// Test memory operands
void test_memory_operands(long *ptr) {
  long a = 0;

  // Test 'm' constraint - any memory operand (load)
  // CHECK: call i64 asm sideeffect "lwi a0, [$1], 0, ->a0", "=r,*m"(i64* %[[PTR:[a-z0-9]+]])
  __asm__ volatile ("lwi a0, [$1], 0, ->a0" : "=r"(a) : "m"(*ptr));

  // Test memory operand with base register (store)
  // Store uses register for value and base
  // CHECK: call void asm sideeffect "swi a0, [a1], 0", "r,r"
  __asm__ volatile ("swi a0, [a1], 0" : : "r"(a), "r"(ptr));
}

// Test block structure (BSTART/BSTOP)
void test_block_structure() {
  long a = 0, b = 0;

  // Test block start
  // CHECK: call void asm sideeffect "BSTART.STD", ""
  __asm__ volatile ("BSTART.STD" ::: "memory");

  // Test arithmetic inside block
  // CHECK: call i64 asm sideeffect "add a0, a1, ->a0", "=r,r,r"
  __asm__ volatile ("add a0, a1, ->a0" : "=r"(a) : "r"(a), "r"(b));

  // Test block stop
  // CHECK: call void asm sideeffect "BSTOP", ""
  __asm__ volatile ("BSTOP" ::: "memory");
}

// Test condition code setting
void test_condition_codes() {
  long a = 0, b = 0;

  // Test SETC instruction for condition setting
  // CHECK: call void asm sideeffect "setc.eq a0, a1", "r,r"(i64 %[[A:[a-z0-9]+]], i64 %[[B:[a-z0-9]+]])
  __asm__ volatile ("setc.eq a0, a1" : : "r"(a), "r"(b));
}

// Test compressed instructions
void test_compressed_instructions() {
  long a = 0, b = 0;

  // Test compressed add
  // CHECK: call i64 asm sideeffect "c.add a0, a1, ->t", "=r,r,r"
  __asm__ volatile ("c.add a0, a1, ->t" : "=r"(a) : "r"(a), "r"(b));

  // Test compressed load word immediate
  // CHECK: call i64 asm sideeffect "c.lwi [a0, 16], ->t", "=r,r"(i64 16)
  __asm__ volatile ("c.lwi [a0, $1], ->t" : "=r"(a) : "I"(4));

  // Test compressed store word immediate
  // CHECK: call void asm sideeffect "c.swi t#1, [a0, 16]", "r,I"
  __asm__ volatile ("c.swi t#1, [a0, $1]" : : "r"(a), "I"(4));
}

// Test T/U register references (block-private registers)
void test_block_private_registers() {
  long a = 0;

  // Test T register destination
  // CHECK: call i64 asm sideeffect "add a0, a1, ->t", "=r,r"
  __asm__ volatile ("add a0, a1, ->t" : "=r"(a) : "r"(a));

  // Test T register reference (t#1-t#4)
  // CHECK: call i64 asm sideeffect "add t#1, a0, ->t", "=r,r"
  __asm__ volatile ("add t#1, a0, ->t" : "=r"(a) : "r"(a));

  // Test U register destination
  // CHECK: call i64 asm sideeffect "add a0, a1, ->u", "=r,r"
  __asm__ volatile ("add a0, a1, ->u" : "=r"(a) : "r"(a));
}

// Test multiple constraints and outputs
void test_multiple_outputs() {
  long a = 0, b = 0, c = 0, d = 0;

  // Multiple inline asm statements with different constraints
  // CHECK: call i64 asm sideeffect "add $1, $2, ->$0", "=r,r,r"
  __asm__ volatile ("add $1, $2, ->$0" : "=r"(a) : "r"(c), "r"(d));
  
  // CHECK: call i64 asm sideeffect "add $1, $2, ->$0", "=r,r,r"
  __asm__ volatile ("add $1, $2, ->$0" : "=r"(b) : "r"(c), "r"(d));
}

// Test clobber list
void test_clobber_list() {
  long a = 0, b = 0;
  // CHECK: call void asm sideeffect "add a0, a1, ->a0", "=r,r,r" nounwind
  __asm__ volatile ("add a0, a1, ->a0"
                    : "=r"(a)
                    : "r"(a), "r"(b)
                    : "memory", "cc");
}

// Test volatile inline assembly
void test_volatile_asm() {
  long a = 0;

  // CHECK: call i64 asm sideeffect "addi a0, 1, ->a0", "=r,I"(i64 1)
  __asm__ volatile ("addi a0, 1, ->a0" : "=r"(a));
}

// Test special register constraints
void test_special_registers() {
  long a = 0;

  // Test zero register
  // CHECK: call i64 asm sideeffect "add zero, a0, ->a0", "=r,r"
  __asm__ volatile ("add zero, a0, ->a0" : "=r"(a) : "r"(a));

  // Test stack pointer
  // CHECK: call i64 asm sideeffect "add sp, a0, ->a0", "=r,r"
  __asm__ volatile ("add sp, a0, ->a0" : "=r"(a) : "r"(a));

  // Test return address register
  // CHECK: call i64 asm sideeffect "add ra, a0, ->a0", "=r,r"
  __asm__ volatile ("add ra, a0, ->a0" : "=r"(a) : "r"(a));
}
