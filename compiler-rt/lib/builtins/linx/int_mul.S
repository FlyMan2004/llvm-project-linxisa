//===-- linx/mulsi3.S - 32-bit integer multiplication ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// 32-bit integer multiplication for Linx ISA.
//
// For targets without hardware multiplication, this provides a
// shift-and-add multiplication algorithm.
//
//===----------------------------------------------------------------------===//

	.text
	.align	2

	.globl	__mulsi3
	.type	__mulsi3, @function
__mulsi3:
	// a0 = multiplicand, a1 = multiplier, a0 = result
	// Based on shift-and-add algorithm

	mv	a2, a0		// a2 = multiplicand
	mv	a0, zero	// a0 = 0 (result)

.Lmulsi3_loop:
	andi	a3, a1, 1	// a3 = multiplier & 1 (check LSB)
	beqz	a3, .Lmulsi3_skip	// if LSB is 0, skip addition
	add	a0, a0, a2	// result += multiplicand

.Lmulsi3_skip:
	srli	a1, a1, 1	// multiplier >>= 1
	slli	a2, a2, 1	// multiplicand <<= 1
	bnez	a1, .Lmulsi3_loop	// while multiplier != 0

	ret

	.size	__mulsi3, .-__mulsi3


//===-- linx/muldi3.S - 64-bit integer multiplication ------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// 64-bit integer multiplication for Linx ISA.
//
// For targets without hardware multiplication, this provides a
// shift-and-add multiplication algorithm.
//
//===----------------------------------------------------------------------===//

	.text
	.align	2

	.globl	__muldi3
	.type	__muldi3, @function
__muldi3:
	// a0 = multiplicand, a1 = multiplier, a0 = result
	// Based on shift-and-add algorithm

	mv	a2, a0		// a2 = multiplicand (low)
	mv	a3, zero	// a3 = multiplicand (high, for 64-bit extension)
	mv	a0, zero	// a0 = 0 (result low)
	mv	a4, zero	// a4 = 0 (result high)

.Lmuldi3_loop:
	andi	a5, a1, 1	// a5 = multiplier & 1 (check LSB)
	beqz	a5, .Lmuldi3_skip	// if LSB is 0, skip addition

	// Add multiplicand to result (128-bit add)
	add	a0, a0, a2	// result_low += multiplicand_low
	adc	a4, a4, a3	// result_high += multiplicand_high + carry

.Lmuldi3_skip:
	srli	a1, a1, 1	// multiplier >>= 1

	// Shift multiplicand left (128-bit shift)
	slli	a2, a2, 1	// multiplicand_low <<= 1
	adc	a3, a3, zero	// multiplicand_high <<= 1 (with carry from low)

	// Check if multiplier is non-zero (need to check both parts)
	or	a5, a1, a4
	bnez	a5, .Lmuldi3_loop	// while any part of multiplier != 0

	ret

	.size	__muldi3, .-__muldi3
