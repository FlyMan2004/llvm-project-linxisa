# RUN: llvm-mc -triple=linx64 -filetype=obj %s -o - | llvm-objdump -d --triple=linx64 - | FileCheck %s

	.text
	.globl	test_v03_cube_function_canonical
	.type	test_v03_cube_function_canonical,@function
test_v03_cube_function_canonical:
	BSTART.CUBE MAMULB, FP16
	BSTART.CUBE TMATMUL.ACC, FP16
	BSTART.CUBE ACCCVT, FP32
	BSTART.CUBE 5, FP16
	C.BSTOP
	.size	test_v03_cube_function_canonical, .-test_v03_cube_function_canonical

# CHECK-LABEL: <test_v03_cube_function_canonical>:
# CHECK: BSTART.TMATMUL{{[[:space:]]+}}FP16
# CHECK: BSTART.TMATMUL.ACC{{[[:space:]]+}}FP16
# CHECK: BSTART.ACCCVT{{[[:space:]]+}}FP32
# CHECK: BSTART.CUBE{{[[:space:]]+}}5, FP16
# CHECK: C.BSTOP
