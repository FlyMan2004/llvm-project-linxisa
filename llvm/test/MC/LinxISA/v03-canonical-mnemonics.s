# RUN: llvm-mc -triple=linx64 -filetype=obj %s -o - | llvm-objdump -d --triple=linx64 - | FileCheck %s

	.text
	.globl	test_v03_canonical
	.type	test_v03_canonical,@function
test_v03_canonical:
	BSTART.TMA 0, 0
	BSTART.CUBE 8, 0
	V.ADD a0, a1, ->a2
	C.BSTOP
	.size	test_v03_canonical, .-test_v03_canonical

# CHECK-LABEL: <test_v03_canonical>:
# CHECK: BSTART.TMA{{[[:space:]]+}}TLOAD
# CHECK: BSTART.CUBE{{[[:space:]]+}}MAMULB
# CHECK: v.add
# CHECK: C.BSTOP
# CHECK-NOT: L{{.}}BSTOP
