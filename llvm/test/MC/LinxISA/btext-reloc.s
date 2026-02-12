# RUN: llvm-mc -triple=linx64 -filetype=obj %s -o - | llvm-objdump -r - | FileCheck %s

	.text
	.globl	foo
	.type	foo,@function
foo:
	BSTART.MSEQ 0
	B.TEXT extbody
	C.BSTART
	C.BSTOP
	.size	foo, .-foo

# CHECK: RELOCATION RECORDS FOR [.text]:
# CHECK: {{[0-9A-Fa-f]+}} R_LINX_B25_PCREL{{[[:space:]]+}}extbody

