# RUN: llvm-mc -triple=linx64 -filetype=obj %s -o - | llvm-objdump -r - | FileCheck %s

	.text
target:
	C.BSTOP
target2:
	C.BSTOP

	.section	.rodata.rel,"a",@progbits
table:
	.long	target-table
	.long	target2-table

# CHECK: RELOCATION RECORDS FOR [.rodata.rel]:
# CHECK: {{[0-9A-Fa-f]+}} R_LINX_32_PCREL{{[[:space:]]+}}.text
# CHECK: {{[0-9A-Fa-f]+}} R_LINX_32_PCREL{{[[:space:]]+}}.text+0x6
