//===--- LinxISA inline assembly comprehensive tests ---------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This file provides comprehensive inline assembly tests for LinxISA target,
// including real-world use cases.
//
//===----------------------------------------------------------------------===//

// RUN: %clang_cc1 -triple linx64-unknown-elf -emit-llvm %s -o - | FileCheck %s

// Test: Get current time counter
unsigned long get_cycle_count(void) {
  unsigned long result;
  // Read CYCLE register using SSRGET
  // CHECK: call i64 asm sideeffect "c.ssrget 0x11, ->$0", "=r"
  __asm__ volatile ("c.ssrget 0x11, ->$0" : "=r"(result));
  return result;
}

// Test: Memory barrier
void memory_barrier(void) {
  // Full memory barrier using fence
  // CHECK: call void asm sideeffect "fence", ""
  __asm__ volatile ("fence" ::: "memory");
}

// Test: Atomic exchange
long atomic_exchange(long *ptr, long val) {
  long old_val;
  // Use LL/SC pattern with block structure
  // CHECK: call void asm sideeffect "BSTART.STD", ""
  __asm__ volatile ("BSTART.STD" ::: "memory");
  // Load linked
  // CHECK: call i64 asm sideeffect "lwi $1, [$2], 0, ->$0", "=r,r"
  __asm__ volatile ("lwi $1, [$2], 0, ->$0" : "=r"(old_val) : "r"(ptr));
  // Store conditional (simplified)
  __asm__ volatile ("swi $1, [$2], 0" : : "r"(val), "r"(ptr) : "memory");
  // CHECK: call void asm sideeffect "BSTOP", ""
  __asm__ volatile ("BSTOP" ::: "memory");
  return old_val;
}

// Test: Inline constant materialization
long materialize_constant(void) {
  long result;
  // Materialize 0x12345678 using LUI + ADDI
  // CHECK: call i64 asm sideeffect "lui $1, 1193046, ->$0", "=r,J"
  __asm__ volatile ("lui $1, 1193046, ->$0" : "=r"(result));
  return result;
}

// Test: Bit manipulation - count trailing zeros
int count_trailing_zeros(unsigned long val) {
  int result = 0;
  // CHECK: call i64 asm sideeffect "ctz $1, $2, ->$0", "=r,r"
  __asm__ volatile ("ctz $1, $2, ->$0" : "=r"(result) : "r"(val));
  return result;
}

// Test: Bit manipulation - parity check
int check_parity(unsigned long val) {
  int result;
  // CHECK: call i64 asm sideeffect "parity $1, $2, ->$0", "=r,r"
  __asm__ volatile ("parity $1, $2, ->$0" : "=r"(result) : "r"(val));
  return result;
}

// Test: Byte swap
long bswap(long val) {
  long result;
  // CHECK: call i64 asm sideeffect "rev8 $1, $2, ->$0", "=r,r"
  __asm__ volatile ("rev8 $1, $2, ->$0" : "=r"(result) : "r"(val));
  return result;
}

// Test: Conditional move using csel
long conditional_move(long condition, long true_val, long false_val) {
  long result;
  // Convert condition to 0/1 if needed
  __asm__ volatile ("cmpeq a0, a1, a2" : : "r"(condition), "r"(0));
  // Use conditional select
  // CHECK: call i64 asm sideeffect "csel a0, a1, a2, ->a0", "=r,r,r,r"
  __asm__ volatile ("csel a0, a1, a2, ->a0" : "=r"(result) : "r"(true_val), "r"(false_val));
  return result;
}

// Test: Function prologue with FENTRY
void test_fentry(void) {
  // CHECK: call void asm sideeffect "FENTRY [ra ~ s2], sp!, 256", ""
  __asm__ volatile ("FENTRY [ra ~ s2], sp!, 256" ::: "memory");
  // Function body would go here
  __asm__ volatile ("add a0, a1, ->a0" ::: "memory");
}

// Test: Function epilogue with FRET
void test_fret(long a0) {
  long ra_val;
  // Get return address
  // CHECK: call void asm sideeffect "add ra, a0, ->ra", "=r,r"
  __asm__ volatile ("add ra, a0, ->ra" : "=r"(ra_val) : "r"(a0));
  // CHECK: call void asm sideeffect "FRET.STK [ra ~ s2], sp!, 256", ""
  __asm__ volatile ("FRET.STK [ra ~ s2], sp!, 256" ::: "memory");
}

// Test: Indexed memory access with shift
long indexed_array_access(long *array, long index) {
  long result;
  // array[index] where we compute offset as index << 3 for 64-bit elements
  // CHECK: call i64 asm sideeffect "ld $1, [$2, $3<<3], ->$0", "=r,r,r"
  __asm__ volatile ("ld $1, [$2, $3<<3], ->$0" : "=r"(result) : "r"(array), "r"(index));
  return result;
}

// Test: String operation - memset pattern
void *simple_memset(void *dst, int val, long count) {
  void *result = dst;
  long pattern = (unsigned char)val;
  // Fill pattern with replication
  // CHECK: call void asm sideeffect "BSTART.MSEQ", ""
  __asm__ volatile ("BSTART.MSEQ" ::: "memory");
  // Set byte
  // CHECK: call void asm sideeffect "sbi $1, [$2], 0", "r,r"
  __asm__ volatile ("sbi $1, [$2], 0" : : "r"(pattern), "r"(dst));
  // CHECK: call void asm sideeffect "BSTOP", ""
  __asm__ volatile ("BSTOP" ::: "memory");
  return result;
}

// Test: Coprocessor/extension register access
long read_extension_reg(long reg_id) {
  long result;
  // SSRGET for extension registers (0x20-0x2F range)
  // CHECK: call i64 asm sideeffect "ssrget $1, $2, ->$0", "=r,r"
  __asm__ volatile ("ssrget $1, $2, ->$0" : "=r"(result) : "r"(reg_id));
  return result;
}

// Test: Interrupt/exception control
void enable_interrupts(void) {
  // Set interrupt enable flag via system register
  // CHECK: call void asm sideeffect "ssrset 0x21, 1", ""
  __asm__ volatile ("ssrset 0x21, 1" ::: "memory");
}

void disable_interrupts(void) {
  // Clear interrupt enable flag
  // CHECK: call void asm sideeffect "ssrset 0x21, 0", ""
  __asm__ volatile ("ssrset 0x21, 0" ::: "memory");
}

// Test: Min/max operations without branches
long fast_min(long a, long b) {
  long result;
  // CHECK: call i64 asm sideeffect "min $1, $2, ->$0", "=r,r,r"
  __asm__ volatile ("min $1, $2, ->$0" : "=r"(result) : "r"(a), "r"(b));
  return result;
}

long fast_max(long a, long b) {
  long result;
  // CHECK: call i64 asm sideeffect "max $1, $2, ->$0", "=r,r,r"
  __asm__ volatile ("max $1, $2, ->$0" : "=r"(result) : "r"(a), "r"(b));
  return result;
}

// Test: Absolute value without branches
long fast_abs(long val) {
  long result;
  // CHECK: call i64 asm sideeffect "abs $1, $2, ->$0", "=r,r"
  __asm__ volatile ("abs $1, $2, ->$0" : "=r"(result) : "r"(val));
  return result;
}

// Test: Clamp value to range
long clamp(long val, long min_val, long max_val) {
  long result;
  // CHECK: call i64 asm sideeffect "clamp $1, $2, $3, ->$0", "=r,r,r,r"
  __asm__ volatile ("clamp $1, $2, $3, ->$0" : "=r"(result) : "r"(val), "r"(min_val), "r"(max_val));
  return result;
}
