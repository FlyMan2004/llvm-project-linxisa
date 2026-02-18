# RUN: llvm-mc -triple=linx64 -filetype=obj %s -o - | llvm-objdump -d --triple=linx64 - | FileCheck %s

	.text
	.globl	test_v03_iot_dynamic_reg_roundtrip
	.type	test_v03_iot_dynamic_reg_roundtrip,@function
test_v03_iot_dynamic_reg_roundtrip:
	BSTART.MSEQ 0
	B.IOT [t#1.reuse], ->t<a0>
	C.BSTOP
	.size	test_v03_iot_dynamic_reg_roundtrip, .-test_v03_iot_dynamic_reg_roundtrip

# CHECK-LABEL: <test_v03_iot_dynamic_reg_roundtrip>:
# CHECK: BSTART.MSEQ
# CHECK: B.IOT
# CHECK-SAME: ->t<
# CHECK: C.BSTOP
