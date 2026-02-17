# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=linx64 a.s -o a.o
# RUN: llvm-mc -filetype=obj -triple=linx64 b.s -o b.o
# RUN: ld.lld a.o b.o -o relax
# RUN: llvm-objdump -d --no-show-raw-insn --triple=linx64 relax | FileCheck %s --check-prefix=RELAX
# RUN: ld.lld --no-relax a.o b.o -o norelax
# RUN: llvm-objdump -d --no-show-raw-insn --triple=linx64 norelax | FileCheck %s --check-prefix=NORELAX

# RELAX-LABEL: <_start>:
# RELAX: c.setret
# RELAX-NEXT: setret

# NORELAX-LABEL: <_start>:
# NORELAX: hl.setret
# NORELAX-NEXT: hl.setret

#--- a.s
	.text
	.globl	_start
_start:
	setret near_target
	setret far_target
	c.bstop

#--- b.s
	.text
	.globl	near_target
near_target:
	c.bstop
	.space	128
	.globl	far_target
far_target:
	c.bstop
