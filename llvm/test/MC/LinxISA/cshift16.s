# RUN: llvm-mc -triple=linx64 -show-encoding %s | FileCheck %s --check-prefix=ENC
# RUN: llvm-mc -triple=linx64 -filetype=obj %s -o - | llvm-objdump -d --triple=linx64 - | FileCheck %s --check-prefix=DIS

	.text
foo:
	c.slli 3
	c.srli 7

# ENC: c.slli{{[[:space:]]+}}3,{{[[:space:]]+}}->t
# ENC: encoding: [0xec,0x10]
# ENC: c.srli{{[[:space:]]+}}7,{{[[:space:]]+}}->t
# ENC: encoding: [0xec,0x19]

# DIS-LABEL: <foo>:
# DIS: c.slli{{[[:space:]]+}}3,{{[[:space:]]+}}->t
# DIS: c.srli{{[[:space:]]+}}7,{{[[:space:]]+}}->t
