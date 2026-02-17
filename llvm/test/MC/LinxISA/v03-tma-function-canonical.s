# RUN: llvm-mc -triple=linx64 -filetype=obj %s -o - | llvm-objdump -d --triple=linx64 - | FileCheck %s

	.text
	.globl	test_v03_tma_function_canonical
	.type	test_v03_tma_function_canonical,@function
test_v03_tma_function_canonical:
	BSTART.TLOAD FP16
	BSTART.TSTORE FP16
	BSTART.TMOV FP16
	BSTART.TMA 2, 4
	C.BSTOP
	.size	test_v03_tma_function_canonical, .-test_v03_tma_function_canonical

# CHECK-LABEL: <test_v03_tma_function_canonical>:
# CHECK: BSTART.TLOAD{{[[:space:]]+}}FP16
# CHECK: BSTART.TSTORE{{[[:space:]]+}}FP16
# CHECK: BSTART.TMOV{{[[:space:]]+}}FP16
# CHECK: BSTART.TMOV{{[[:space:]]+}}FP16
# CHECK: C.BSTOP
