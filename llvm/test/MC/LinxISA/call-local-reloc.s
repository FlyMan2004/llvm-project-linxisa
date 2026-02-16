# RUN: llvm-mc -triple=linx64 -filetype=obj %s -o - | llvm-objdump -r - | FileCheck %s

	.text
	.globl caller
	.type caller,@function
caller:
	# Local returning call must keep relocations so the linker can recompute
	# both call and return immediates after relaxation/layout.
	BSTART CALL, callee, ra=.Lret
.Lret:
	C.BSTOP
	.size caller, .-caller

	.globl callee
	.type callee,@function
callee:
	C.BSTART.STD
	C.BSTOP
	.size callee, .-callee

# CHECK: RELOCATION RECORDS FOR [.text]:
# CHECK: R_LINX_B17_PCREL{{[[:space:]]+}}callee
# CHECK: R_LINX_CSETRET5_PCREL{{[[:space:]]+}}.Lret
