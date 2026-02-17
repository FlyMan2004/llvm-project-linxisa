# RUN: rm -rf %t && split-file %s %t && cd %t
# RUN: llvm-mc -filetype=obj -triple=linx64 a.s -o a.o
# RUN: llvm-mc -filetype=obj -triple=linx64 b.s -o b.o
# RUN: ld.lld a.o b.o -o relax
# RUN: llvm-objdump -d --no-show-raw-insn --triple=linx64 relax | FileCheck %s --check-prefix=RELAX
# RUN: ld.lld --no-relax a.o b.o -o norelax
# RUN: llvm-objdump -d --no-show-raw-insn --triple=linx64 norelax | FileCheck %s --check-prefix=NORELAX

# RELAX-LABEL: <_start>:
# RELAX: BSTART.STD{{[[:space:]]+}}COND

# NORELAX-LABEL: <_start>:
# NORELAX: HL.BSTART.STD{{[[:space:]]+}}COND

#--- a.s
	.text
	.globl	_start
_start:
	hl.bstart.std cond, target
	c.bstop

#--- b.s
	.text
	.globl	target
target:
	c.bstop
