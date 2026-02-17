# RUN: llvm-mc -triple=linx64 -filetype=obj %s -o - | llvm-objdump -d --triple=linx64 - | FileCheck %s

	.text
	.globl	test_v03_par_snippet
	.type	test_v03_par_snippet,@function
test_v03_par_snippet:
	.long 0x22109181
	.long 0x180221a3
	.long 0x30007493
	.long 0x00858013
	.short 0x103c
	.short 0x503c
	.short 0x903c
	.long 0x22011181
	.long 0x88207513
	.short 0x0800
	.long 0x22519181
	.long 0x1800a4a3
	.long 0x20057493
	.short 0x103c
	.short 0x503c
	.long 0x22011181
	.long 0x88207513
	.short 0x103c
	.short 0x503c
	.short 0x903c
	.short 0x0800
	.long 0x22519181
	.long 0x1800a4a3
	.long 0x2004f493
	.short 0x103c
	.short 0x503c
	.long 0x22211181
	.long 0x88207513
	.short 0x103c
	.short 0x503c
	.short 0x903c
	.size	test_v03_par_snippet, .-test_v03_par_snippet

# CHECK-LABEL: <test_v03_par_snippet>:
# CHECK: BSTART.TLOAD{{[[:space:]]+}}FP16
# CHECK: B.ARG{{[[:space:]]+}}ND2ZN.normal, FP16, Null
# CHECK: B.IOTI{{[[:space:]]+\[\], last[[:space:]]+}}->t<
# CHECK: B.IOR{{[[:space:]]+\[a6,s0\],\[\]}}
# CHECK: C.B.DIMI{{[[:space:]]+}}64,{{[[:space:]]+}}->lb0
# CHECK: C.B.DIMI{{[[:space:]]+}}64,{{[[:space:]]+}}->lb1
# CHECK: C.B.DIMI{{[[:space:]]+}}64,{{[[:space:]]+}}->lb2
# CHECK: BSTART.TMATMUL{{[[:space:]]+}}FP16
# CHECK: B.IOTI{{[[:space:]]+\[t#1, t#3\.reuse\], last[[:space:]]+}}->acc<
# CHECK: C.BSTART.STD
# CHECK: BSTART.TEPL{{[[:space:]]+}}163, FP16
# CHECK: B.ARG{{[[:space:]]+}}DN2NZ.normal, FP32, Null
# CHECK: B.IOTI{{[[:space:]]+\[u#3\], last[[:space:]]+}}->t<
# CHECK: C.B.DIMI{{[[:space:]]+}}64,{{[[:space:]]+}}->lb0
# CHECK: C.B.DIMI{{[[:space:]]+}}64,{{[[:space:]]+}}->lb1
# CHECK: BSTART.TMATMUL{{[[:space:]]+}}FP16
# CHECK: B.IOTI{{[[:space:]]+\[t#1, t#3\.reuse\], last[[:space:]]+}}->acc<
# CHECK: C.B.DIMI{{[[:space:]]+}}64,{{[[:space:]]+}}->lb2
# CHECK: C.BSTART.STD
# CHECK: BSTART.TEPL{{[[:space:]]+}}163, FP16
# CHECK: B.ARG{{[[:space:]]+}}DN2NZ.normal, FP32, Null
# CHECK: B.IOTI{{[[:space:]]+\[u#2\], last[[:space:]]+}}->t<
# CHECK: BSTART.TMATMUL.ACC{{[[:space:]]+}}FP16
# CHECK: B.IOTI{{[[:space:]]+\[t#1, t#3\.reuse\], last[[:space:]]+}}->acc<
