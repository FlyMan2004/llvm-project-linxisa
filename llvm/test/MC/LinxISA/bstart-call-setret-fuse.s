# RUN: llvm-mc -triple=linx64 -filetype=obj %s -o - | llvm-objdump -d --triple=linx64 - | FileCheck %s

# CHECK-LABEL: <foo>:
# CHECK: BSTART{{[[:space:]]+}}CALL, 0x{{[0-9a-f]+}}, ra=0x6
# CHECK-NOT: setret

	.text
	.globl	foo
	.type	foo,@function
foo:
	# Fused syntax (textual sugar): still encodes as BSTART CALL + C.SETRET.
	BSTART	CALL, bar, ra=foo_ret
foo_ret:
	C.BSTOP
	.size	foo, .-foo

	.globl	bar
	.type	bar,@function
bar:
	C.BSTOP
	.size	bar, .-bar
