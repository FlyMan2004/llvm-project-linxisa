# RUN: llvm-mc -triple=linx64 -filetype=obj %s -o - | llvm-objdump -d --triple=linx64 - | FileCheck %s

# CHECK-LABEL: <foo>:
# CHECK: hl.ssrget {{(0x1f06|ETEMP0_ACRn)}}, ->x0
# CHECK: hl.ssrset x0, {{(0x1f06|ETEMP0_ACRn)}}

	.text
	.globl	foo
	.type	foo,@function
foo:
	hl.ssrget	0x1f06, ->x0
	hl.ssrset	x0, 0x1f06
	C.BSTOP
	.size	foo, .-foo
