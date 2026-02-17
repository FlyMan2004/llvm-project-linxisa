# RUN: llvm-mc -triple=linx64 -filetype=obj %s -o - | llvm-objdump -d --triple=linx64 - | FileCheck %s

	.text
	.globl	test_v03_canonical
	.type	test_v03_canonical,@function
test_v03_canonical:
	BSTART.TLOAD FP16
	BSTART.TMATMUL FP16
	V.ADD a0, a1, ->a2
	C.BSTOP
	.size	test_v03_canonical, .-test_v03_canonical

# CHECK-LABEL: <test_v03_canonical>:
# CHECK: BSTART.TLOAD{{[[:space:]]+}}FP16
# CHECK: BSTART.TMATMUL{{[[:space:]]+}}FP16
# CHECK: v.add
# CHECK: C.BSTOP
# CHECK-NOT: L{{.}}BSTOP
